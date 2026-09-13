#include "network/ntfy_notifier.h"
#include "config.h"
#include "utils/logger.h"
#include "secrets.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <time.h>

enum class NtfyState { IDLE, WAITING_ACK };

// Fields touched by BOTH tasks (main loop + background worker) - guarded by _mutex.
struct NtfyShared {
    volatile NtfyState state = NtfyState::IDLE;
    bool sendRequested       = false;
    int  sendDurationMinutes = 0;
    int  sendHour            = 0;
    int  sendMinute          = 0;
    bool sendIsUpdate        = false;
    bool sendIsEmergency     = false;
    bool ackReceived         = false;
};

static NtfyShared        _shared;
static SemaphoreHandle_t _mutex = nullptr;

// Lock-free server status flag: written by the worker, read by the UI thread.
static std::atomic<int> _serverStatus{(int)NtfyServerStatus::PAUSED};

static void _setServerStatus(NtfyServerStatus s) {
    _serverStatus.store((int)s, std::memory_order_relaxed);
}

// Fields only used by the background worker task (no locking).
static time_t   _ackSinceEpoch  = 0;
static uint32_t _pendingSinceMs = 0;
static uint32_t _lastPollMs     = 0;

// ------------------------------------------------------------

// Single blocking HTTPS request. Runs ONLY on the worker task.
// Certificate validation is skipped (setInsecure()), same rationale
// as the previous notifier: no pinning against rotating certs.
static int _ntfyRequest(const char* method, const String& url, const String& body,
                         const char* title, const char* priority, const char* actions,
                         String& response) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    if (!http.begin(client, url)) return -1;

    if (title)    http.addHeader("Title", title);
    if (priority) http.addHeader("Priority", priority);
    if (actions)  http.addHeader("Actions", actions);
    // Force the server to close the connection right after the response,
    // same fix as before: avoids hanging on Keep-Alive idle sockets.
    http.addHeader("Connection", "close");

    int code = (strcmp(method, "POST") == 0) ? http.POST(body) : http.GET();

    // Never read a body on 204 - it hangs waiting for EOF (see previous notifier notes).
    response = (code > 0 && code != 204) ? http.getString() : "";

    http.end();
    return code;
}

// Runs on the worker task. Plain-text body, no JSON payload needed for ntfy.
// The Actions header attaches a button that POSTs "ACK" straight to the
// ack topic from the recipient's device - no ESP32 involvement required
// to arm it.
static bool _doSendCallMessage(int duration_minutes, int hour, int minute, bool isUpdate) {
    char body[168];
    const char* prefix = isUpdate ? "[UPDATE] " : "";

    if (duration_minutes <= 0) {
        snprintf(body, sizeof(body), "%sOn mange tout de suite !", prefix);
    } else {
        int totalMinutes = hour * 60 + minute + duration_minutes;
        int endHour   = (totalMinutes / 60) % 24;
        int endMinute =  totalMinutes % 60;
        snprintf(body, sizeof(body), "%sOn mange dans %d minutes, soit a %dh%02d.",
                 prefix, duration_minutes, endHour, endMinute);
    }

    String url     = String(NTFY_BASE_URL) + "/" + NTFY_TOPIC_APPEL;
    String actions = String("http, Compris, ") + NTFY_BASE_URL + "/" + NTFY_TOPIC_ACK
                    + ", method=POST, body=ACK, clear=true";
    String response;

    LOG_INFO("NTFY", "Sending call message (%d min)...", duration_minutes);
    int code = _ntfyRequest("POST", url, body, "PingBox", "default", actions.c_str(), response);
    if (code != 200) {
        _setServerStatus(NtfyServerStatus::ERROR);
        LOG_ERROR("NTFY", "Send failed, HTTP code=%d", code);
        return false;
    }
    _setServerStatus(NtfyServerStatus::OK);
    LOG_OK("NTFY", "Message sent");
    return true;
}

// Runs on the worker task. No ack action button here: the emergency
// workflow never waited for a reply, same as the previous notifier.
static bool _doSendEmergencyMessage() {
    String url = String(NTFY_BASE_URL) + "/" + NTFY_TOPIC_APPEL;
    String response;

    LOG_WARN("NTFY", "Sending EMERGENCY message...");
    int code = _ntfyRequest("POST", url, "URGENCE - VIENS TOUT DE SUITE",
                             "PingBox URGENT", "urgent", nullptr, response);
    if (code != 200) {
        _setServerStatus(NtfyServerStatus::ERROR);
        LOG_ERROR("NTFY", "Emergency send failed, HTTP code=%d", code);
        return false;
    }
    _setServerStatus(NtfyServerStatus::OK);
    LOG_OK("NTFY", "Emergency message sent");
    return true;
}

// Runs on the worker task. Any non-empty response on the ack topic since
// the reference timestamp means someone tapped "Compris" - no JSON
// parsing needed, since we fully control what gets posted there.
static void _doPollAck() {
    String url = String(NTFY_BASE_URL) + "/" + NTFY_TOPIC_ACK
               + "/json?poll=1&since=" + (uint32_t)_ackSinceEpoch;
    String response;
    int code = _ntfyRequest("GET", url, "", nullptr, nullptr, nullptr, response);
    if (code != 200) {
        _setServerStatus(NtfyServerStatus::ERROR);
        LOG_ERROR("NTFY", "Ack poll failed, HTTP code=%d", code);
        return;
    }
    _setServerStatus(NtfyServerStatus::OK);

    if (response.length() > 0) {
        LOG_OK("NTFY", "Acknowledgment received");
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _shared.ackReceived = true;
        _shared.state = NtfyState::IDLE;
        xSemaphoreGive(_mutex);
    }
}

// ------------------------------------------------------------
//  Background worker - owns every blocking HTTPS call.
// ------------------------------------------------------------
static void _ntfyTask(void*) {
    for (;;) {
        NtfyState stateSnapshot;
        bool doSend = false;
        int duration = 0, hour = 0, minute = 0;
        bool isUpdate = false, isEmergency = false;

        xSemaphoreTake(_mutex, portMAX_DELAY);
        stateSnapshot = _shared.state;
        if (_shared.sendRequested) {
            doSend      = true;
            duration    = _shared.sendDurationMinutes;
            hour        = _shared.sendHour;
            minute      = _shared.sendMinute;
            isUpdate    = _shared.sendIsUpdate;
            isEmergency = _shared.sendIsEmergency;
            _shared.sendRequested = false;
        }
        xSemaphoreGive(_mutex);

        if (doSend) {
            if (isEmergency) {
                _doSendEmergencyMessage();
            } else if (_doSendCallMessage(duration, hour, minute, isUpdate)) {
                _ackSinceEpoch  = time(nullptr);
                _pendingSinceMs = millis();
                _lastPollMs     = millis();
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.state = NtfyState::WAITING_ACK;
                xSemaphoreGive(_mutex);
            }
        } else if (stateSnapshot == NtfyState::WAITING_ACK) {
            uint32_t now = millis();
            if (now - _pendingSinceMs > NTFY_ACK_TIMEOUT_MS) {
                LOG_WARN("NTFY", "Ack wait timed out, nobody replied");
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.state = NtfyState::IDLE;
                xSemaphoreGive(_mutex);
            } else if (now - _lastPollMs >= NTFY_ACK_POLL_INTERVAL_MS) {
                _lastPollMs = now;
                _doPollAck();
            }
        }

        // Same breathing room between consecutive HTTPS actions as before.
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ------------------------------------------------------------

void ntfyNotifierInit() {
    _mutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(_ntfyTask, "ntfy_io", 8192, nullptr, 1, nullptr, 0);
    LOG_INFO("NTFY", "Background I/O task started on core 0");
}

bool ntfySendCallMessage(int duration_minutes, int current_hour, int current_minute, bool isUpdate) {
    bool accepted = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!_shared.sendRequested) {
        _shared.sendRequested       = true;
        _shared.sendDurationMinutes = duration_minutes;
        _shared.sendHour            = current_hour;
        _shared.sendMinute          = current_minute;
        _shared.sendIsUpdate        = isUpdate;
        _shared.sendIsEmergency     = false;
        accepted = true;
    }
    xSemaphoreGive(_mutex);

    if (!accepted) LOG_WARN("NTFY", "Send request rejected, a send is already queued");
    return accepted;
}

bool ntfySendEmergencyMessage(int current_hour, int current_minute) {
    bool accepted = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!_shared.sendRequested) {
        _shared.sendRequested       = true;
        _shared.sendDurationMinutes = 0;
        _shared.sendHour            = current_hour;
        _shared.sendMinute          = current_minute;
        _shared.sendIsUpdate        = false;
        _shared.sendIsEmergency     = true;
        accepted = true;
    }
    xSemaphoreGive(_mutex);

    if (!accepted) LOG_WARN("NTFY", "Emergency send rejected, a send is already queued");
    return accepted;
}

bool ntfyCheckAndClearAck() {
    bool ack = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_shared.ackReceived) {
        ack = true;
        _shared.ackReceived = false;
    }
    xSemaphoreGive(_mutex);
    return ack;
}

NtfyServerStatus ntfyGetServerStatus() {
    return (NtfyServerStatus)_serverStatus.load(std::memory_order_relaxed);
}