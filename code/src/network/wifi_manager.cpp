#include "network/wifi_manager.h"
#include "config.h"
#include <WiFi.h>


static uint32_t _lastAttemptMs = 0;

void wifiManagerInit() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    _lastAttemptMs = millis();
}

void wifiManagerUpdate() {
    if (WiFi.status() == WL_CONNECTED) return;

    uint32_t now = millis();
    if (now - _lastAttemptMs > WIFI_RECONNECT_INTERVAL_MS) {
        _lastAttemptMs = now;
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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