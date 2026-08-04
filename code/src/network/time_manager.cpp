#include "network/time_manager.h"
#include "config.h"
#include "network/wifi_manager.h"
#include <time.h>

static bool _ntpStarted = false;
static bool _synced     = false;
static uint32_t _lastCheckMs = 0;

static const char* _dayNamesFr[7] = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};

static bool _readLocalTime(struct tm* info) {
    // ms=0 -> single attempt, does not block the loop
    return getLocalTime(info, 0);
}

void timeManagerInit() {
    _ntpStarted = false;
    _synced = false;
}

void timeManagerUpdate() {
    uint32_t now = millis();
    if (now - _lastCheckMs < TIME_UPDATE_INTERVAL_MS) return;
    _lastCheckMs = now;

    if (!wifiManagerIsConnected()) {
        _synced = false;
        return;
    }

    if (!_ntpStarted) {
        configTzTime(TZ_FRANCE, NTP_SERVER_1, NTP_SERVER_2);
        _ntpStarted = true;
    }

    struct tm timeinfo;
    _synced = _readLocalTime(&timeinfo);
}

bool timeManagerIsSynced() { return _synced; }

int timeManagerGetHour() {
    struct tm t;
    return _readLocalTime(&t) ? t.tm_hour : 0;
}

int timeManagerGetMinute() {
    struct tm t;
    return _readLocalTime(&t) ? t.tm_min : 0;
}

void timeManagerGetTimeString(char* buffer, size_t bufferSize) {
    struct tm t;
    if (_readLocalTime(&t)) snprintf(buffer, bufferSize, "%02d:%02d", t.tm_hour, t.tm_min);
    else                    snprintf(buffer, bufferSize, "--:--");
}

void timeManagerGetDayName(char* buffer, size_t bufferSize) {
    struct tm t;
    if (_readLocalTime(&t)) snprintf(buffer, bufferSize, "%s", _dayNamesFr[t.tm_wday]);
    else                    snprintf(buffer, bufferSize, "---");
}

void timeManagerGetDayNumber(char* buffer, size_t bufferSize) {
    struct tm t;
    if (_readLocalTime(&t)) snprintf(buffer, bufferSize, "%02d", t.tm_mday);
    else                    snprintf(buffer, bufferSize, "00");
}