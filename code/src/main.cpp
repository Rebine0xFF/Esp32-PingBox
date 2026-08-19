#include <Arduino.h>
#include <string.h>
#include <esp_sleep.h>

#include "display/screen_main.h"
#include "display/screen_info.h"
#include "display/screen_info_data.h"
#include "input/encoder.h"
#include "input/buttons.h"
#include "actuators/servo_control.h"
#include "network/wifi_manager.h"
#include "network/time_manager.h"
#include "network/discord_notifier.h"
#include "config.h"
#include "utils/logger.h"

// ============================================================
//  SETUP
// ============================================================

void setup() {
    LOG_BEGIN();
    LOG_LINE();
    LOG_INFO("MAIN", "PingBox booting up...");

    screenMainInit();
    screenInfoInit();
    encoderInit();
    buttonsInit();
    servoControlInit();
    LOG_OK("MAIN", "Displays, inputs and actuators initialized");

    wifiManagerInit();
    timeManagerInit();
    discordNotifierInit();
    LOG_OK("MAIN", "Network stack initialized (WiFi/NTP/Discord)");

    buttonsSetLedReady();
    LOG_LINE();
    LOG_INFO("MAIN", "Setup complete, entering main loop");
}

// ============================================================
//  LOOP
// ============================================================

void loop() {

    int encoderDuration = encoderGetMinutes();
    bool encPressed     = encoderSwitchPressed();
    int current_hour    = timeManagerGetHour();
    int current_minute  = timeManagerGetMinute();

    // ------------------------------------------------------------
    //  Power Management (Deep Sleep)
    // ------------------------------------------------------------
    if (buttonPowerPressed()) {
        LOG_INFO("MAIN", "Power button pressed, initiating shutdown sequence...");

        // 0. TODO : Servo position reset
        // servoControlSetPosition(SERVO_POS_IN)
        // wait(1s) to end the movement
        // servoControlDetach()
        
        // 1. Turn off displays to prevent burn-in and save power
        screenMainSleep();
        screenInfoSleep();
        
        // 2. Turn off LED
        digitalWrite(PIN_LED_BTN, LOW);

        // 3. Configure wakeup source on GPIO 13 (LOW level)
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);
        
        LOG_INFO("MAIN", "Entering deep sleep now. Good night!");
        Serial.flush();
        
        // 4. Halt CPU
        esp_deep_sleep_start();
    }

    // ------------------------------------------------------------
    //  Call countdown: driven by the real clock and independent of Discord acks.
    //  Discord acknowledgments update the "last action" display only.
    // ------------------------------------------------------------
    enum class LoopCallState { IDLE, RUNNING, PAUSED };
    static LoopCallState _callState = LoopCallState::IDLE;
    static int  _callTargetTotalMinutes = 0;

    // Set when the send button is pressed while WiFi/NTP aren't ready yet.
    // The actual Discord call is retried every loop() until both are ready.
    static bool _discordSendPending  = false;
    static int  _pendingSendDuration = 0;
    static bool _pendingIsUpdate     = false;

    // Set when a timed call was queued before NTP sync. Flush will recompute
    // the call target using the synced clock instead of the stale pre-sync time.
    static bool _pendingNeedsResync = false;

    // ------------------------------------------------------------
    //  Emergency switch (latching, Normally Closed, PIN_BTN_EMERGENCY).
    //  Rest position = contact closed = pin LOW. Tripped = contact open
    //  = pin HIGH (via internal pull-up). While tripped, the encoder and
    //  the send button are completely inert and any countdown is dropped.
    // ------------------------------------------------------------
    static bool _emergencyActive = false;
    static bool _forceMainRedraw = false;
    bool emergencyRaw = buttonEmergencyActive();

    if (emergencyRaw && !_emergencyActive) {
        // Rising edge: switch just tripped -> enter emergency mode
        _emergencyActive = true;
        LOG_WARN("MAIN", "EMERGENCY switch triggered!");

        _callState = LoopCallState::IDLE;   // drop any countdown, it is not resumed
        _discordSendPending = false;

        digitalWrite(PIN_LED_BTN, LOW);
        screenInfoUpdateEmergency();
        discordSendEmergencyMessage(current_hour, current_minute);

    } else if (!emergencyRaw && _emergencyActive) {
        // Falling edge: switch was reset back to its rest position
        _emergencyActive = false;
        _forceMainRedraw = true;
        LOG_OK("MAIN", "Emergency switch reset, resuming normal operation");

        char timeStr[6];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", current_hour, current_minute);
        setLastAction(LastAction::URGENT, timeStr);
    }

    int wheelDuration = encoderDuration;
    CallState renderState = CallState::NONE;

    if (_emergencyActive) {
        wheelDuration = 0;
        renderState = CallState::EMERGENCY;
    } else {
        // Global action: if encoder is pressed while NOT running, reset time to 0
        if (encPressed && _callState != LoopCallState::RUNNING) {
            encoderSetMinutes(0);
            encoderDuration = 0;
            wheelDuration = 0;
        }

        if (_callState == LoopCallState::RUNNING) {
            int currentTotalMinutes = current_hour * 60 + current_minute;
            int remaining = _callTargetTotalMinutes - currentTotalMinutes;
            if (remaining < 0) remaining = 0;

            if (encPressed) {
                // Pause: freeze remaining time and seed encoder for adjustments.
                _callState = LoopCallState::PAUSED;
                encoderSetMinutes(remaining);
                wheelDuration = remaining;
                renderState = CallState::PAUSED;
                LOG_INFO("MAIN", "Countdown paused at %d min remaining", remaining);
            } else if (remaining <= 0) {
                _callState = LoopCallState::IDLE; // target time reached
                wheelDuration = 0;
                renderState = CallState::NONE;
                LOG_INFO("MAIN", "Countdown reached zero, sending meal-time notification");

                // Countdown reached zero: send immediate "meal is now" notification (reminder)
                char timeStr[6];
                snprintf(timeStr, sizeof(timeStr), "%02d:%02d", current_hour, current_minute);
                setLastAction(LastAction::CALLED, timeStr);

                if (wifiManagerIsConnected() && timeManagerIsSynced()) {
                    discordSendCallMessage(0, current_hour, current_minute, false);
                } else {
                    LOG_WARN("MAIN", "Network/time not ready, queuing send for later flush");
                    _discordSendPending  = true;
                    _pendingSendDuration = 0;
                    _pendingNeedsResync  = false;
                    _pendingIsUpdate     = false;
                }
            } else {
                wheelDuration = remaining;
                renderState = CallState::RUNNING;
            }
        } else if (_callState == LoopCallState::PAUSED) {
            // Frozen: the encoder freely adjusts the paused duration.
            wheelDuration = encoderDuration;
            renderState = CallState::PAUSED;
        }
        // LoopCallState::IDLE: wheelDuration/renderState already default to the
        // encoder value / CallState::NONE set above.
    }


    // --- Main screen rendering optimization ---
    static int lastWheelDuration = -1;
    static int lastHour = -1;
    static int lastMinute = -1;
    static CallState lastRenderState = CallState::NONE;
    static uint32_t lastAnimFrameMs = 0;

    bool mainScreenChanged = (wheelDuration != lastWheelDuration) ||
                             (current_hour != lastHour) ||
                             (current_minute != lastMinute) ||
                             (renderState != lastRenderState) ||
                             _forceMainRedraw;

    // Force redraw every 400ms for the hourglass animation when running
    if (renderState == CallState::RUNNING) {
        uint32_t now = millis();
        if (now - lastAnimFrameMs >= 400) {
            mainScreenChanged = true;
            lastAnimFrameMs = now;
        }
    } else if (renderState == CallState::EMERGENCY) {
        // Faster refresh for a smooth blink + rotating-arrow animation
        uint32_t now = millis();
        if (now - lastAnimFrameMs >= 200) {
            mainScreenChanged = true;
            lastAnimFrameMs = now;
        }
    }

    if (mainScreenChanged) {
        screenMainUpdate(wheelDuration, current_hour, current_minute, renderState);
        lastWheelDuration = wheelDuration;
        lastHour = current_hour;
        lastMinute = current_minute;
        lastRenderState = renderState;
        _forceMainRedraw = false;
    }

    // ------------------------------------------

    if (_emergencyActive) {
        digitalWrite(PIN_LED_BTN, LOW);
    } else {
        buttonsLedUpdate(_callState == LoopCallState::RUNNING);
    }
    wifiManagerUpdate();
    timeManagerUpdate();
    // Flush a send that was queued while WiFi/NTP weren't ready yet.
    // Uses the current (now synced) time
    if (!_emergencyActive && _discordSendPending && wifiManagerIsConnected() && timeManagerIsSynced()) {
        _discordSendPending = false;
        LOG_INFO("MAIN", "Flushing queued Discord send (network/time now ready)");

        int syncedHour   = timeManagerGetHour();
        int syncedMinute = timeManagerGetMinute();

        if (_pendingNeedsResync) {
            // Correct the countdown target now that real time is known,
            // instead of leaving it computed from the pre-sync (00:00-ish) clock
            _callTargetTotalMinutes = syncedHour * 60 + syncedMinute + _pendingSendDuration;
            _pendingNeedsResync = false;
        }

        discordSendCallMessage(_pendingSendDuration, syncedHour, syncedMinute, _pendingIsUpdate);
    }

    // Poll for Discord ACK at 500ms intervals to avoid mutex contention with the background task.
    static uint32_t lastAckCheck = 0;
    if (!_emergencyActive && millis() - lastAckCheck > 500) {
        lastAckCheck = millis();
        if (discordCheckAndClearAck()) {
            LOG_OK("MAIN", "Discord acknowledgment received");
            char timeStr[6];
            timeManagerGetTimeString(timeStr, sizeof(timeStr));
            setLastAction(LastAction::APPROVED, timeStr);
        }
    }

    if (!_emergencyActive && buttonSendPressed()) {
        // Resending while paused is an update to the already-sent message,
        // rather than a brand new call.
        bool isUpdate  = (_callState == LoopCallState::PAUSED);
        bool immediate = (encoderDuration <= 0);
        LOG_INFO("MAIN", "Send button pressed (duration=%d min, update=%d, immediate=%d)", encoderDuration, isUpdate, immediate);

        if (!immediate) {
            _callTargetTotalMinutes = current_hour * 60 + current_minute + encoderDuration;
            _callState = LoopCallState::RUNNING;

            // Update main display immediately to show running hourglass.
            screenMainUpdate(encoderDuration, current_hour, current_minute, CallState::RUNNING);
        } else {
            // Immediate (0 min)
            _callState = LoopCallState::IDLE;
        }

        // Update 'last action' display immediately.
        char timeStr[6];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", current_hour, current_minute);
        setLastAction(LastAction::CALLED, timeStr);

        if (wifiManagerIsConnected() && timeManagerIsSynced()) {
            discordSendCallMessage(encoderDuration, current_hour, current_minute, isUpdate);
        } else {
            // Network/time not ready yet: queue it, the pending-send block
            // below will flush it as soon as both become ready.
            _discordSendPending  = true;
            _pendingSendDuration = encoderDuration;
            _pendingNeedsResync  = !immediate;
            _pendingIsUpdate     = isUpdate;
        }
    }

    // ------------------------------------------------------------
    //  Info screen (software I2C - slow): update only on change and
    //  deferred until encoder idle to avoid stuttering the main animation.
    // ------------------------------------------------------------
    if (!_emergencyActive) {
        static char lastTime[6]   = "";
        static char lastDay[4]    = "";
        static char lastDayNum[3] = "";
        static char lastIP[16]    = "";
        static bool lastWifiOk    = false;
        static uint32_t lastActionVersionSeen = 0;
        static DiscordServerStatus lastServerStatus = DiscordServerStatus::PAUSED;
        static bool infoUpdatePending = false;

        char newTime[6], newDay[4], newDayNum[3], newIP[16];
        timeManagerGetTimeString(newTime, sizeof(newTime));
        timeManagerGetDayName(newDay, sizeof(newDay));
        timeManagerGetDayNumber(newDayNum, sizeof(newDayNum));
        wifiManagerGetIPString(newIP, sizeof(newIP));
        bool wifiOk = wifiManagerIsConnected();
        uint32_t currentActionVersion = getLastActionVersion();
        DiscordServerStatus discordStatus = discordGetServerStatus();

        bool changed = strcmp(newTime, lastTime) != 0
                    || strcmp(newDay, lastDay) != 0
                    || strcmp(newDayNum, lastDayNum) != 0
                    || strcmp(newIP, lastIP) != 0
                    || wifiOk != lastWifiOk
                    || currentActionVersion != lastActionVersionSeen
                    || discordStatus != lastServerStatus;

        if (changed) {
            snprintf(TIME_text, sizeof(TIME_text), "%s", newTime);
            snprintf(DAY_text, sizeof(DAY_text), "%s", newDay);
            snprintf(DAY_NUMBER_text, sizeof(DAY_NUMBER_text), "%s", newDayNum);
            snprintf(IP_ADRESS_text, sizeof(IP_ADRESS_text), "%s", newIP);

            strcpy(lastTime, newTime);
            strcpy(lastDay, newDay);
            strcpy(lastDayNum, newDayNum);
            strcpy(lastIP, newIP);
            lastWifiOk = wifiOk;
            lastActionVersionSeen = currentActionVersion;
            lastServerStatus = discordStatus;

            // WiFi icon + small status glyph
            setWifiIcon(wifiOk);
            setWifiStatus(wifiOk ? Status::OK : Status::ERROR);

            // Discord server status glyph (PAUSED = no interaction yet, not an error)
            Status serverStatus = Status::PAUSED;
            switch (discordStatus) {
                case DiscordServerStatus::OK:    serverStatus = Status::OK;    break;
                case DiscordServerStatus::ERROR: serverStatus = Status::ERROR; break;
                default:                         serverStatus = Status::PAUSED; break;
            }
            setServerStatus(serverStatus);

            // Global face: happy unless WiFi is down or the last Discord call failed
            setStatusFace(wifiOk && discordStatus != DiscordServerStatus::ERROR);

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
}