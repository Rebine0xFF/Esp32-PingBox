#include "network/discord_notifier.h"
#include "config.h"
#include "utils/logger.h"
#include "secrets.h"
#include "display/screen_info_data.h"
#include "network/time_manager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>

enum class DiscordState { IDLE, WAITING_ACK };

// Fields touched by BOTH tasks (main loop + background worker) - guarded by _mutex.
struct DiscordShared {
    volatile DiscordState state = DiscordState::IDLE;
    bool sendRequested       = false;
    int  sendDurationMinutes = 0;
    int  sendHour            = 0;
    int  sendMinute          = 0;
    bool sendIsUpdate        = false;
    bool ackReceived         = false;
};

static DiscordShared     _shared;
static SemaphoreHandle_t _mutex = nullptr;

// Lock-free status flag, kept separate from the mutex-guarded _shared
// struct : only the Discord task writes it, only the UI thread reads it,
// so a single atomic int is enough to guarantee cross-core visibility.
static std::atomic<int> _serverStatus{(int)DiscordServerStatus::PAUSED};

static void _setServerStatus(DiscordServerStatus s) {
    _serverStatus.store((int)s, std::memory_order_relaxed);
}

// Fields only ever touched by the background worker task - no locking needed.
static String   _pendingMessageId  = "";
static uint32_t _pendingSinceMs    = 0;
static uint32_t _lastPollMs        = 0;

// ------------------------------------------------------------

// Single blocking HTTPS request. Runs ONLY on the worker task.
// NOTE: certificate validation is skipped (setInsecure()) : accepted
// trade-off to avoid maintaining a pinned cert against Cloudflare's
// rotating certificates.
static int _discordRequest(const char* method, const String& url, const String& payload, String& response) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    if (!http.begin(client, url)) return -1;

    http.addHeader("Authorization", String("Bot ") + DISCORD_BOT_TOKEN);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "PingBox (https://github.com/Rebine0xFF/Esp32-PingBox, 1.0)");
    
    // Force the server to close the TCP connection immediately after the response.
    // Prevents HTTPClient::getString() from hanging until Cloudflare's Keep-Alive timeout.
    http.addHeader("Connection", "close");

    int code;
    if (strcmp(method, "POST") == 0)      code = http.POST(payload);
    else if (strcmp(method, "PUT") == 0)  code = http.PUT(payload);
    else                                   code = http.GET();

    // Do not attempt to read a body for a "204 No Content" response.
    // Calling getString() on a 204 causes the ESP32 to hang waiting for EOF
    // until the underlying TCP socket times out (approx. 7 minutes).
    if (code > 0 && code != 204) {
        response = http.getString();
    } else {
        response = "";
    }
    
    http.end();
    return code;
}

// Runs on the worker task. Builds either "on mange tout de suite" (0 min)
// or "on mange dans X min (soit à HHhMM)". isUpdate prefixes the message
// with "[UPDATE]" when resending after a pause/adjustment.
static bool _doSendCallMessage(int duration_minutes, int hour, int minute, bool isUpdate) {
    char content[168];
    const char* prefix = isUpdate ? "[UPDATE] " : "";

    if (duration_minutes <= 0) {
        snprintf(content, sizeof(content),
                 "%s🚨 On mange tout de suite! (réponse ici = compris)",
                 prefix);
    } else {
        int totalMinutes = hour * 60 + minute + duration_minutes;
        int endHour   = (totalMinutes / 60) % 24;
        int endMinute =  totalMinutes % 60;

        snprintf(content, sizeof(content),
                 "%s🔔 On mange dans %d minutes, soit à %dh%02d. (réponse ici = compris)",
                 prefix, duration_minutes, endHour, endMinute);
    }

    JsonDocument sendDoc;
    sendDoc["content"] = content;
    String payload;
    serializeJson(sendDoc, payload);

    String url = String(DISCORD_API_BASE) + "/channels/" + DISCORD_CHANNEL_ID + "/messages";
    String response;
    LOG_INFO("DISCORD", "Sending call message (%d min)...", duration_minutes);
    int code = _discordRequest("POST", url, payload, response);
    if (code != 200 && code != 201) {
        _setServerStatus(DiscordServerStatus::ERROR);
        LOG_ERROR("DISCORD", "Send failed, HTTP code=%d", code);
        return false;
    }
    _setServerStatus(DiscordServerStatus::OK);

    JsonDocument respDoc;
    if (deserializeJson(respDoc, response) != DeserializationError::Ok) {
        LOG_ERROR("DISCORD", "Failed to parse send response JSON");
        return false;
    }

    const char* messageId = respDoc["id"];
    if (!messageId) {
        LOG_ERROR("DISCORD", "No message id in send response");
        return false;
    }
    _pendingMessageId = messageId;
    LOG_OK("DISCORD", "Message sent, id=%s", messageId);

    return true;
}

// Runs on the worker task.
static void _doPollAck() {
    String url = String(DISCORD_API_BASE) + "/channels/" + DISCORD_CHANNEL_ID
               + "/messages?after=" + _pendingMessageId + "&limit=5";
    String response;
    int code = _discordRequest("GET", url, "", response);
    if (code != 200) {
        _setServerStatus(DiscordServerStatus::ERROR);
        LOG_ERROR("DISCORD", "Ack poll failed, HTTP code=%d", code);
        return;
    }
    _setServerStatus(DiscordServerStatus::OK);

    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) {
        LOG_ERROR("DISCORD", "Failed to parse ack poll response JSON");
        return;
    }
    JsonArray arr = doc.as<JsonArray>();

    bool humanReplyFound = false;
    for (JsonObject msg : arr) {
        // The "bot" field is only present (and true) for bot authors
        bool isBot = msg["author"]["bot"] | false;
        if (!isBot) {
            humanReplyFound = true;
            break;
        }
    }

    if (humanReplyFound) {
        LOG_OK("DISCORD", "Human reply detected, acknowledgment confirmed");
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _shared.ackReceived = true;
        _shared.state = DiscordState::IDLE;
        xSemaphoreGive(_mutex);
    }
}

// ------------------------------------------------------------
//  Background worker - owns every blocking Discord HTTPS call.
// ------------------------------------------------------------
static void _discordTask(void*) {
    for (;;) {
        DiscordState stateSnapshot;
        bool doSend = false;
        int duration = 0, hour = 0, minute = 0;
        bool isUpdate = false;

        xSemaphoreTake(_mutex, portMAX_DELAY);
        stateSnapshot = _shared.state;
        if (_shared.sendRequested) {
            doSend   = true;
            duration = _shared.sendDurationMinutes;
            hour     = _shared.sendHour;
            minute   = _shared.sendMinute;
            isUpdate = _shared.sendIsUpdate;
            _shared.sendRequested = false;
        }
        xSemaphoreGive(_mutex);

        if (doSend) {
            if (_doSendCallMessage(duration, hour, minute, isUpdate)) {
                _pendingSinceMs = millis();
                _lastPollMs = millis();
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.state = DiscordState::WAITING_ACK;
                xSemaphoreGive(_mutex);
            }
        } else if (stateSnapshot == DiscordState::WAITING_ACK) {
            uint32_t now = millis();
            if (now - _pendingSinceMs > DISCORD_ACK_TIMEOUT_MS) {
                LOG_WARN("DISCORD", "Ack wait timed out, nobody replied");
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.state = DiscordState::IDLE; // nobody answered = stop polling
                xSemaphoreGive(_mutex);
            } else if (now - _lastPollMs >= DISCORD_ACK_POLL_INTERVAL_MS) {
                _lastPollMs = now;
                _doPollAck();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200)); // worker cadence ; real timing is millis()-based above
    }
}

// ------------------------------------------------------------

void discordNotifierInit() {
    _mutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(_discordTask, "discord_io", 8192, nullptr, 1, nullptr, 0);
    LOG_INFO("DISCORD", "Background I/O task started on core 0");
}

bool discordSendCallMessage(int duration_minutes, int current_hour, int current_minute, bool isUpdate) {
    bool accepted = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!_shared.sendRequested) {
        _shared.sendRequested       = true;
        _shared.sendDurationMinutes = duration_minutes;
        _shared.sendHour            = current_hour;
        _shared.sendMinute          = current_minute;
        _shared.sendIsUpdate        = isUpdate;
        accepted = true;
    }
    xSemaphoreGive(_mutex);

    if (!accepted) {
        LOG_WARN("DISCORD", "Send request rejected, a send is already queued");
    }

    return accepted;
}

bool discordIsAckPending() {
    bool pending;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    pending = (_shared.state == DiscordState::WAITING_ACK);
    xSemaphoreGive(_mutex);
    return pending;
}

bool discordCheckAndClearAck() {
    bool ack = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_shared.ackReceived) {
        ack = true;
        _shared.ackReceived = false;
    }
    xSemaphoreGive(_mutex);
    return ack;
}

DiscordServerStatus discordGetServerStatus() {
    return (DiscordServerStatus)_serverStatus.load(std::memory_order_relaxed);
}