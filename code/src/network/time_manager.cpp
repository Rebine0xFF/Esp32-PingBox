#include "network/time_manager.h"
#include "config.h"
#include "network/wifi_manager.h"
#include <time.h>

static bool _ntpStarted = false;
static bool _synced     = false;
static uint32_t _lastUpdateMs = 0;

// Cached, already-formatted values ; only touched inside timeManagerUpdate()
static int  _cachedHour   = 0;
static int  _cachedMinute = 0;
static char _cachedTime[6]   = "--:--";
static char _cachedDay[4]    = "---";
static char _cachedDayNum[3] = "00";

static const char* _dayNamesFr[7] = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};

// Single non-blocking attempt to read the RTC (ms=0 -> no busy wait)
static bool _readLocalTime(struct tm* info) {
    return getLocalTime(info, 0);
}

void timeManagerInit() {
    _ntpStarted = false;
    _synced = false;
    _lastUpdateMs = 0;
}

void timeManagerUpdate() {
    uint32_t now = millis();
    if (now - _lastUpdateMs < TIME_UPDATE_INTERVAL_MS) return;
    _lastUpdateMs = now;

    if (!wifiManagerIsConnected()) {
        _synced = false;
        return;
    }

    if (!_ntpStarted) {
        configTzTime(TZ_FRANCE, NTP_SERVER_1, NTP_SERVER_2);
        _ntpStarted = true;
    }

    struct tm timeinfo;
    if (_readLocalTime(&timeinfo)) {
        _synced = true;

        // Refresh the cache only on a successful read.
        // On a transient failure, keep displaying the last known
        // good value instead of falling back to "--:--".
        _cachedHour   = timeinfo.tm_hour;
        _cachedMinute = timeinfo.tm_min;

        snprintf(_cachedTime, sizeof(_cachedTime), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        snprintf(_cachedDay, sizeof(_cachedDay), "%s", _dayNamesFr[timeinfo.tm_wday]);
        snprintf(_cachedDayNum, sizeof(_cachedDayNum), "%02d", timeinfo.tm_mday);
    } else {
        _synced = false;
        // Do not touch the cache ; keep last known good display values.
    }
}

bool timeManagerIsSynced() { return _synced; }

int timeManagerGetHour()   { return _cachedHour; }
int timeManagerGetMinute() { return _cachedMinute; }

void timeManagerGetTimeString(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%s", _cachedTime);
}

void timeManagerGetDayName(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%s", _cachedDay);
}

void timeManagerGetDayNumber(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%s", _cachedDayNum);
}