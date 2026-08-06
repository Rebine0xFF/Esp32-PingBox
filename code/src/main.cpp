#include <Arduino.h>
#include <string.h>

#include "display/screen_main.h"
#include "display/screen_info.h"
#include "display/screen_info_data.h"
#include "input/encoder.h"
#include "input/buttons.h"
#include "network/wifi_manager.h"
#include "network/time_manager.h"
#include "config.h"

// ============================================================
//  SETUP
// ============================================================

void setup() {
    screenMainInit();
    screenInfoInit();
    encoderInit();
    buttonsInit();

    wifiManagerInit();
    timeManagerInit();
}

// ============================================================
//  LOOP
// ============================================================

void loop() {

    int duration = encoderGetMinutes();

    wifiManagerUpdate();
    timeManagerUpdate();

    int current_hour   = timeManagerGetHour();
    int current_minute = timeManagerGetMinute();

    screenMainUpdate(duration, current_hour, current_minute);

    // ------------------------------------------------------------
    //  Screen Info (Software I2C - expensive bit-banging)
    //
    //  Rules:
    //  1. Only update when something changes (time, status, action, etc.)
    //  2. Never update while screenMain is being actively adjusted -
    //     the redraw is deferred until the encoder has been idle for
    //     ENCODER_IDLE_MS, to avoid stuttering the animated wheel.
    // ------------------------------------------------------------
    static char lastTime[6]   = "";
    static char lastDay[4]    = "";
    static char lastDayNum[3] = "";
    static char lastIP[16]    = "";
    static bool lastWifiOk    = false;
    static bool infoUpdatePending = false;

    char newTime[6], newDay[4], newDayNum[3], newIP[16];
    timeManagerGetTimeString(newTime, sizeof(newTime));
    timeManagerGetDayName(newDay, sizeof(newDay));
    timeManagerGetDayNumber(newDayNum, sizeof(newDayNum));
    wifiManagerGetIPString(newIP, sizeof(newIP));
    bool wifiOk = wifiManagerIsConnected();

    bool changed = strcmp(newTime, lastTime) != 0
                || strcmp(newDay, lastDay) != 0
                || strcmp(newDayNum, lastDayNum) != 0
                || strcmp(newIP, lastIP) != 0
                || wifiOk != lastWifiOk;

    if (changed) {
        // Cheap: just copies into RAM buffers, no I2C traffic yet
        snprintf(TIME_text, sizeof(TIME_text), "%s", newTime);
        snprintf(DAY_text, sizeof(DAY_text), "%s", newDay);
        snprintf(DAY_NUMBER_text, sizeof(DAY_NUMBER_text), "%s", newDayNum);
        snprintf(IP_ADRESS_text, sizeof(IP_ADRESS_text), "%s", newIP);

        strcpy(lastTime, newTime);
        strcpy(lastDay, newDay);
        strcpy(lastDayNum, newDayNum);
        strcpy(lastIP, newIP);
        lastWifiOk = wifiOk;

        setWifiStatus(wifiOk ? Status::OK : Status::ERROR);

        // Defer the actual (expensive) redraw instead of firing it now
        infoUpdatePending = true;
    }

    if (infoUpdatePending) {
        uint32_t idleFor = millis() - encoderGetLastChangeMs();
        if (idleFor >= ENCODER_IDLE_MS) {
            screenInfoUpdate();
            infoUpdatePending = false;
        }
    }
}