#include "network/discord_notifier.h"
#include "config.h"
#include "secrets.h"
#include "display/screen_info_data.h"
#include "network/time_manager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

enum class DiscordState { IDLE, WAITING_ACK };

// Fields touched by BOTH tasks (main loop + background worker) - guarded by _mutex.
struct DiscordShared {
    volatile DiscordState state = DiscordState::IDLE;
    bool sendRequested      = false;
    int  sendDurationMinutes = 0;
    int  sendHour            = 0;
    int  sendMinute          = 0;
};

static DiscordShared     _shared;
static SemaphoreHandle_t _mutex = nullptr;

// Fields only ever touched by the background worker task - no locking needed.
static String   _pendingMessageId  = "";
static int      _reactionBaseline  = 0;
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

    int code;
    if (strcmp(method, "POST") == 0)      code = http.POST(payload);
    else if (strcmp(method, "PUT") == 0)  code = http.PUT(payload);
    else                                   code = http.GET();

    response = http.getString();
    http.end();
    return code;
}

// Runs on the worker task. Builds "on mange dans X min (soit à HHhMM)".
static bool _doSendCallMessage(int duration_minutes, int hour, int minute) {
    int totalMinutes = hour * 60 + minute + duration_minutes;
    int endHour   = (totalMinutes / 60) % 24;
    int endMinute =  totalMinutes % 60;

    char content[160];
    snprintf(content, sizeof(content),
             "🔔 On mange dans %d minutes (soit à %dh%02d). Réagis avec ✅ pour dire que tu as vu.",
             duration_minutes, endHour, endMinute);

    JsonDocument sendDoc;
    sendDoc["content"] = content;
    String payload;
    serializeJson(sendDoc, payload);

    String url = String(DISCORD_API_BASE) + "/channels/" + DISCORD_CHANNEL_ID + "/messages";
    String response;
    int code = _discordRequest("POST", url, payload, response);
    if (code != 200 && code != 201) return false;

    JsonDocument respDoc;
    if (deserializeJson(respDoc, response) != DeserializationError::Ok) return false;

    const char* messageId = respDoc["id"];
    if (!messageId) return false;
    _pendingMessageId = messageId;

    String reactUrl = String(DISCORD_API_BASE) + "/channels/" + DISCORD_CHANNEL_ID
                     + "/messages/" + _pendingMessageId + "/reactions/" + DISCORD_ACK_EMOJI + "/@me";
    String reactResponse;
    int seedCode = _discordRequest("PUT", reactUrl, "", reactResponse);
    _reactionBaseline = (seedCode == 204) ? 1 : 0;

    return true;
}

// Runs on the worker task.
static void _doPollAck() {
    String url = String(DISCORD_API_BASE) + "/channels/" + DISCORD_CHANNEL_ID
               + "/messages/" + _pendingMessageId + "/reactions/" + DISCORD_ACK_EMOJI;
    String response;
    int code = _discordRequest("GET", url, "", response);
    if (code != 200) return;

    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) return;
    JsonArray arr = doc.as<JsonArray>();

    if ((int)arr.size() > _reactionBaseline) {
        char timeStr[6];
        timeManagerGetTimeString(timeStr, sizeof(timeStr));
        setLastAction(LastAction::APPROVED, timeStr);

        // Acknowledging just means "seen" - it stops OUR polling (nothing
        // more to check), but the on-screen countdown in main.cpp is fully
        // independent and keeps running regardless of this state.
        xSemaphoreTake(_mutex, portMAX_DELAY);
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

        xSemaphoreTake(_mutex, portMAX_DELAY);
        stateSnapshot = _shared.state;
        if (_shared.sendRequested) {
            doSend   = true;
            duration = _shared.sendDurationMinutes;
            hour     = _shared.sendHour;
            minute   = _shared.sendMinute;
            _shared.sendRequested = false;
        }
        xSemaphoreGive(_mutex);

        if (doSend) {
            if (_doSendCallMessage(duration, hour, minute)) {
                _pendingSinceMs = millis();
                _lastPollMs = millis();
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.state = DiscordState::WAITING_ACK;
                xSemaphoreGive(_mutex);
            }
        } else if (stateSnapshot == DiscordState::WAITING_ACK) {
            uint32_t now = millis();
            if (now - _pendingSinceMs > DISCORD_ACK_TIMEOUT_MS) {
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
}

bool discordSendCallMessage(int duration_minutes, int current_hour, int current_minute) {
    bool accepted = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!_shared.sendRequested) {
        _shared.sendRequested       = true;
        _shared.sendDurationMinutes = duration_minutes;
        _shared.sendHour            = current_hour;
        _shared.sendMinute          = current_minute;
        accepted = true;
    }
    xSemaphoreGive(_mutex);
    return accepted;
}

bool discordIsAckPending() {
    bool pending;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    pending = (_shared.state == DiscordState::WAITING_ACK);
    xSemaphoreGive(_mutex);
    return pending;
}