#include "network/wifi_manager.h"
#include "config.h"
#include "utils/logger.h"
#include <WiFi.h>


static uint32_t _lastAttemptMs = 0;

static volatile bool     _associated        = false;
static volatile uint32_t _associatedSinceMs = 0;

static void _onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            _associatedSinceMs = millis();
            _associated = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            _associated = false;
            break;
        default:
            break;
    }
}

void wifiManagerInit() {
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(_onWifiEvent);
    WiFi.disconnect();
    delay(50);
    WiFi.setSleep(false);
    LOG_INFO("WIFI", "Connecting to SSID '%s'...", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    _lastAttemptMs = millis();
}

void wifiManagerUpdate() {
    static bool _wasConnected = false;

    if (WiFi.status() == WL_CONNECTED) {
        if (!_wasConnected) {
            _wasConnected = true;
            WiFi.setSleep(false);
            LOG_OK("WIFI", "Connected, IP=%s", WiFi.localIP().toString().c_str());
        }
        return;
    }

    if (_wasConnected) {
        _wasConnected = false;
        LOG_WARN("WIFI", "Connection lost, will retry reconnection");
    }

    uint32_t now = millis();

    // Associated with the AP but no IP yet: DHCP is still running.
    // Do not disconnect, that would restart the whole association.
    if (_associated && (now - _associatedSinceMs) < WIFI_DHCP_TIMEOUT_MS) {
        return;
    }

    if (now - _lastAttemptMs > WIFI_RECONNECT_INTERVAL_MS) {
        _lastAttemptMs = now;
        LOG_INFO("WIFI", "Reconnect attempt...");
        WiFi.disconnect();
        WiFi.reconnect(); // Faster reconnection using internal saved credentials
    }
}

bool wifiManagerIsConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void wifiManagerGetIPString(char* buffer, size_t bufferSize) {
    if (wifiManagerIsConnected()) {
        IPAddress ip = WiFi.localIP();
        snprintf(buffer, bufferSize, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    } else {
        snprintf(buffer, bufferSize, "0.0.0.0");
    }
}