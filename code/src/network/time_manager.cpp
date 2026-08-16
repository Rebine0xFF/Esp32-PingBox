#include "network/time_manager.h"
#include "config.h"
#include "utils/logger.h"
#include "network/wifi_manager.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <sys/time.h>

// ------------------------------------------------------------
//  Raw NTP request/response (RFC 5905) - minimal client
// ------------------------------------------------------------
static const int NTP_PACKET_SIZE = 48;
static const uint32_t SEVENTY_YEARS_UNIX_OFFSET = 2208988800UL; // 1900 -> 1970

static WiFiUDP _udp;
static uint8_t _packetBuffer[NTP_PACKET_SIZE];

// Resolve NTP hostname once and cache the IP to avoid blocking DNS lookups on retries.
static IPAddress _serverIP;
static bool _serverResolved = false;

enum class SntpState { IDLE, WAITING_RESPONSE };
static SntpState _state = SntpState::IDLE;

static bool _synced          = false;
static uint32_t _requestSentMs = 0;
static uint32_t _lastAttemptMs = 0;
static uint32_t _lastSyncMs    = 0;
static uint32_t _lastFormatMs  = 0;

// Cached, already-formatted values ; only touched inside timeManagerUpdate()
static int  _cachedHour   = 0;
static int  _cachedMinute = 0;
static char _cachedTime[6]   = "--:--";
static char _cachedDay[4]    = "---";
static char _cachedDayNum[3] = "00";

static const char* _dayNamesFr[7] = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};

// ------------------------------------------------------------

// Attempts to resolve the NTP server hostname once. This is the only
// call in this module that can block on DNS ; it happens at most once
// per NTP_RETRY_INTERVAL_MS window until it succeeds, then never again.
static bool _resolveServerIfNeeded() {
    if (_serverResolved) return true;

    if (WiFi.hostByName(NTP_SERVER_1, _serverIP)) {
        _serverResolved = true;
    }
    return _serverResolved;
}

static void _sendNtpRequest() {
    memset(_packetBuffer, 0, NTP_PACKET_SIZE);

    // Standard client request header: LI=3 (unsynced), VN=4, Mode=3 (client)
    _packetBuffer[0] = 0b11100011;
    _packetBuffer[1] = 0;      // Stratum
    _packetBuffer[2] = 6;      // Polling interval
    _packetBuffer[3] = 0xEC;   // Peer clock precision
    _packetBuffer[12] = 49;
    _packetBuffer[13] = 0x4E;
    _packetBuffer[14] = 49;
    _packetBuffer[15] = 52;

    // Sends by IP - no DNS lookup, non-blocking
    if (_udp.beginPacket(_serverIP, 123)) {
        _udp.write(_packetBuffer, NTP_PACKET_SIZE);
        _udp.endPacket();
    }
}

// Returns true if a valid NTP response was read and the system
// clock was set from it.
static bool _tryReadNtpResponse() {
    int packetSize = _udp.parsePacket();
    if (packetSize < NTP_PACKET_SIZE) return false;

    _udp.read(_packetBuffer, NTP_PACKET_SIZE);

    uint32_t secsSince1900 = ((uint32_t)_packetBuffer[40] << 24)
                            | ((uint32_t)_packetBuffer[41] << 16)
                            | ((uint32_t)_packetBuffer[42] << 8)
                            |  (uint32_t)_packetBuffer[43];

    if (secsSince1900 == 0) return false; // malformed / empty response

    time_t epoch = (time_t)(secsSince1900 - SEVENTY_YEARS_UNIX_OFFSET);

    struct timeval tv;
    tv.tv_sec  = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    return true;
}

// Refreshes the formatted cache from the current system clock.
// Only overwrites the cache once we have synced at least once, so we
// keep showing "--:--" before the first sync instead of epoch 1970.
static void _refreshCacheFromSystemClock() {
    if (!_synced) return;

    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Only format strings and update state if the minute actually changed
    // This saves CPU cycles on the main thread compared to running snprintf every second
    if (_cachedMinute != timeinfo.tm_min || _cachedHour != timeinfo.tm_hour) {
        _cachedHour   = timeinfo.tm_hour;
        _cachedMinute = timeinfo.tm_min;

        snprintf(_cachedTime, sizeof(_cachedTime), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        snprintf(_cachedDay, sizeof(_cachedDay), "%s", _dayNamesFr[timeinfo.tm_wday]);
        snprintf(_cachedDayNum, sizeof(_cachedDayNum), "%02d", timeinfo.tm_mday);
    }
}

// ------------------------------------------------------------

void timeManagerInit() {
    _synced = false;
    _serverResolved = false;
    _state = SntpState::IDLE;
    _requestSentMs = _lastAttemptMs = _lastSyncMs = _lastFormatMs = 0;

    setenv("TZ", TZ_FRANCE, 1);
    tzset();

    _udp.begin(NTP_LOCAL_PORT);
}

void timeManagerUpdate() {
    uint32_t now = millis();

    switch (_state) {
        case SntpState::IDLE: {
            bool needsSync = !_synced || (now - _lastSyncMs >= NTP_RESYNC_INTERVAL_MS);
            bool canAttempt = (now - _lastAttemptMs >= NTP_RETRY_INTERVAL_MS);

            if (needsSync && canAttempt && wifiManagerIsConnected()) {
                _lastAttemptMs = now;

                if (_resolveServerIfNeeded()) {
                    LOG_INFO("NTP", "Sending sync request to %s", NTP_SERVER_1);
                    _sendNtpRequest();
                    _requestSentMs = now;
                    _state = SntpState::WAITING_RESPONSE;
                } else {
                    LOG_WARN("NTP", "DNS resolution of %s failed, retrying later", NTP_SERVER_1);
                }
            }
            break;
        }

        case SntpState::WAITING_RESPONSE: {
            if (_tryReadNtpResponse()) {
                bool firstSync = !_synced;
                _synced = true;
                _lastSyncMs = now;
                _state = SntpState::IDLE;
                _refreshCacheFromSystemClock();
                LOG_OK("NTP", "%s, time is now %s", firstSync ? "Initial sync done" : "Re-synced", _cachedTime);
            } else if (now - _requestSentMs > NTP_REQUEST_TIMEOUT_MS) {
                _state = SntpState::IDLE;
                LOG_WARN("NTP", "Sync request timed out, will retry");
            }
            break;
        }
    }

    if (now - _lastFormatMs >= TIME_UPDATE_INTERVAL_MS) {
        _lastFormatMs = now;
        _refreshCacheFromSystemClock();
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