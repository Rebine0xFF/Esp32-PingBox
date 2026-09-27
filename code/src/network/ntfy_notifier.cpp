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
#include <string.h>

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

// ------------------------------------------------------------
//  One-shot HTTPS requests - used only for sending (rare, user-triggered).
// ------------------------------------------------------------

static int _ntfyRequest(const char* method, const String& url, const String& body,
                         const char* title, const char* priority, const char* actions,
                         String& response) {
    uint32_t startMs = millis();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);
    // setTimeout() only bounds socket reads once connected - it does NOT
    // bound the mbedTLS handshake retry loop itself, which was observed
    // to hang up to ~60s despite the 5s timeout above. This is measured
    // in seconds, not milliseconds.
    client.setHandshakeTimeout(8);

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    bool began = http.begin(client, url);

    // Diagnostic: http.begin()/connect() runs a blocking DNS lookup that is
    // NOT bounded by setConnectTimeout(). If this massively exceeds our
    // configured timeouts, it confirms the stall happened in DNS, not in
    // the request/response itself.
    uint32_t beginDoneMs = millis();
    if (beginDoneMs - startMs > 6000) {
        LOG_WARN("NTFY", "http.begin()/DNS took %lu ms (way beyond the 5s connect timeout)",
                  (unsigned long)(beginDoneMs - startMs));
    }
    if (!began) return -1;

    if (title)    http.addHeader("Title", title);
    if (priority) http.addHeader("Priority", priority);
    if (actions)  http.addHeader("Actions", actions);
    http.addHeader("Connection", "close");

    int code = (strcmp(method, "POST") == 0) ? http.POST(body) : http.GET();
    response = (code > 0 && code != 204) ? http.getString() : "";

    http.end();
    return code;
}

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

// ------------------------------------------------------------
//  Ack detection - single persistent subscribe connection instead of
//  reconnecting every few seconds. ntfy keeps this connection open and
//  streams new events as they happen, so the recipient's ack tap shows
//  up here without the ESP32 ever polling for it.
// ------------------------------------------------------------

static WiFiClientSecure* _ackClient = nullptr;
static bool     _ackStreamOpen = false;
static uint32_t _lastAckConnectAttemptMs = 0;
static uint32_t _pendingSinceMs = 0;

// Tail of the previous read, kept so a marker split across two reads is
// still detected. Simple substring search, not a real chunked-HTTP
// parser - a pragmatic simplification, same spirit as the previous
// "any non-empty poll response = ack" approach.
static char _ackResidual[24] = {0};

static const char* ACK_EVENT_MARKER = "\"event\":\"message\"";

static void _ackStreamClose() {
    if (_ackClient != nullptr) {
        _ackClient->stop();
        delete _ackClient;
        _ackClient = nullptr;
    }
    _ackStreamOpen = false;
    _ackResidual[0] = '\0';
}

static bool _ackStreamOpenConnection() {
    // Fresh object on every attempt: reusing a single WiFiClientSecure
    // across many connect()/stop() cycles caused setSocketOption() to be
    // called on an already-closed file descriptor ("Bad file number").
    if (_ackClient != nullptr) {
        delete _ackClient;
        _ackClient = nullptr;
    }
    _ackClient = new WiFiClientSecure();
    _ackClient->setInsecure();
    _ackClient->setTimeout(5000);
    _ackClient->setHandshakeTimeout(8);   // seconds - bounds the handshake loop itself

    uint32_t startMs = millis();
    // Explicit 8s connect timeout: without it, connect() (which also does
    // the DNS lookup for NTFY_HOST) was observed to hang 10-21s on a
    // struggling network, well past the handshake timeout above.
    if (!_ackClient->connect(NTFY_HOST, 443, 8000)) {
        LOG_WARN("NTFY", "Ack stream connect failed after %lu ms", (unsigned long)(millis() - startMs));
        delete _ackClient;
        _ackClient = nullptr;
        return false;
    }

    String request = String("GET /") + NTFY_TOPIC_ACK + "/json HTTP/1.1\r\n"
                    + "Host: " + NTFY_HOST + "\r\n"
                    + "Connection: keep-alive\r\n\r\n";
    _ackClient->print(request);

    _ackStreamOpen = true;
    _ackResidual[0] = '\0';
    LOG_OK("NTFY", "Ack stream opened (%lu ms)", (unsigned long)(millis() - startMs));
    return true;
}

// Non-blocking: only drains bytes already sitting in the socket buffer,
// never waits for more. Safe to call every worker tick regardless of
// how much (or how little) traffic the stream is carrying.
static bool _ackStreamCheck() {
    if (!_ackStreamOpen || _ackClient == nullptr) return false;

    if (!_ackClient->connected()) {
        LOG_WARN("NTFY", "Ack stream dropped, will reopen");
        _ackStreamClose();
        return false;
    }

    bool found = false;
    while (_ackClient->available() > 0 && !found) {
        char chunk[128];
        int toRead = _ackClient->available();
        if (toRead > (int)sizeof(chunk) - 1) toRead = sizeof(chunk) - 1;

        int n = _ackClient->readBytes(chunk, toRead);
        if (n <= 0) break;
        chunk[n] = '\0';

        char combined[sizeof(_ackResidual) + sizeof(chunk)];
        snprintf(combined, sizeof(combined), "%s%s", _ackResidual, chunk);

        if (strstr(combined, ACK_EVENT_MARKER) != nullptr) {
            found = true;
        }

        size_t len  = strlen(chunk);
        size_t keep = (len < sizeof(_ackResidual) - 1) ? len : (sizeof(_ackResidual) - 1);
        snprintf(_ackResidual, sizeof(_ackResidual), "%s", chunk + (len - keep));
    }
    return found;
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
            // A fresh send always supersedes any ack we were still
            // waiting on for a previous message.
            _ackStreamClose();

            if (isEmergency) {
                _doSendEmergencyMessage();
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.state = NtfyState::IDLE;
                xSemaphoreGive(_mutex);
            } else if (_doSendCallMessage(duration, hour, minute, isUpdate)) {
                _pendingSinceMs = millis();
                _lastAckConnectAttemptMs = millis();
                _ackStreamOpenConnection();
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.state = NtfyState::WAITING_ACK;
                xSemaphoreGive(_mutex);
            } else {
                // Send failed: nothing to wait an ack for. Without this,
                // a leftover WAITING_ACK from a previous message made the
                // worker keep retrying a doomed ack-stream reconnect every
                // NTFY_ACK_RECONNECT_INTERVAL_MS, hammering an already
                // struggling network.
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.state = NtfyState::IDLE;
                xSemaphoreGive(_mutex);
            }
        } else if (stateSnapshot == NtfyState::WAITING_ACK) {
            uint32_t now = millis();
            if (now - _pendingSinceMs > NTFY_ACK_TIMEOUT_MS) {
                LOG_WARN("NTFY", "Ack wait timed out, nobody replied");
                _ackStreamClose();
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.state = NtfyState::IDLE;
                xSemaphoreGive(_mutex);
            } else if (_ackStreamCheck()) {
                LOG_OK("NTFY", "Acknowledgment received");
                _ackStreamClose();
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _shared.ackReceived = true;
                _shared.state = NtfyState::IDLE;
                xSemaphoreGive(_mutex);
            } else if (!_ackStreamOpen && now - _lastAckConnectAttemptMs >= NTFY_ACK_RECONNECT_INTERVAL_MS) {
                // Stream isn't open (first attempt failed, or it dropped) - retry.
                _lastAckConnectAttemptMs = now;
                _ackStreamOpenConnection();
            }
        }

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