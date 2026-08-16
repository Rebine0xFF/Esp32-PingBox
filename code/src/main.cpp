#include <Arduino.h>
#include <string.h>

#include "display/screen_main.h"
#include "display/screen_info.h"
#include "display/screen_info_data.h"
#include "input/encoder.h"
#include "input/buttons.h"
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
    LOG_OK("MAIN", "Displays and inputs initialized");

    wifiManagerInit();
    timeManagerInit();
    discordNotifierInit(); // spins up the background Discord I/O task
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
    int current_hour    = timeManagerGetHour();
    int current_minute  = timeManagerGetMinute();

    // ------------------------------------------------------------
    //  Call countdown - owned by main.cpp, driven purely by the real
    //  clock. Independent from Discord's own send/ack state:
    //  acknowledging the message on Discord only updates the "last
    //  action" display, it never stops this countdown.
    // ------------------------------------------------------------
    enum class LoopCallState { IDLE, RUNNING, PAUSED };
    static LoopCallState _callState = LoopCallState::IDLE;
    static int  _callTargetTotalMinutes = 0;

    // Set when the send button is pressed while WiFi/NTP aren't ready yet.
    // The actual Discord call is retried every loop() until both are ready.
    static bool _discordSendPending  = false;
    static int  _pendingSendDuration = 0;
    static bool _pendingIsUpdate     = false;

    // True only for a timed (non-immediate) call queued before NTP synced.
    // Tells the flush block below it must also recompute _callTargetTotalMinutes
    // once real time is known, instead of leaving it based on the stale
    // (pre-sync) clock used at button-press time.
    static bool _pendingNeedsResync = false;

    int wheelDuration = encoderDuration;
    CallState renderState = CallState::NONE;

    if (_callState == LoopCallState::RUNNING) {
        int currentTotalMinutes = current_hour * 60 + current_minute;
        int remaining = _callTargetTotalMinutes - currentTotalMinutes;
        if (remaining < 0) remaining = 0;

        if (encoderSwitchPressed()) {
            // Pause: freeze on the current remaining value and unlock the
            // encoder, seeded from that value so it continues adjustment
            // from where the countdown stood.
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

            // The countdown just naturally ran out: fire the "meal is now"
            // notification, on top of the initial "meal in X minutes" one
            // sent at launch. Same immediate-message path as the 0-min case.
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

    screenMainUpdate(wheelDuration, current_hour, current_minute, renderState);
    buttonsLedUpdate(_callState == LoopCallState::RUNNING);
    wifiManagerUpdate();
    timeManagerUpdate();
    // Flush a send that was queued while WiFi/NTP weren't ready yet.
    // Uses the current (now synced) time
    if (_discordSendPending && wifiManagerIsConnected() && timeManagerIsSynced()) {
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

    // Check if background task received a Discord reaction (limit check to twice a second
    // to prevent FreeRTOS mutex starvation on the background task)
    static uint32_t lastAckCheck = 0;
    if (millis() - lastAckCheck > 500) {
        lastAckCheck = millis();
        if (discordCheckAndClearAck()) {
            LOG_OK("MAIN", "Discord acknowledgment received");
            char timeStr[6];
            timeManagerGetTimeString(timeStr, sizeof(timeStr));
            setLastAction(LastAction::APPROVED, timeStr);
        }
    }

    if (buttonSendPressed()) {
        // Resending while paused is an update to the already-sent message,
        // rather than a brand new call.
        bool isUpdate  = (_callState == LoopCallState::PAUSED);
        bool immediate = (encoderDuration <= 0);
        LOG_INFO("MAIN", "Send button pressed (duration=%d min, update=%d, immediate=%d)", encoderDuration, isUpdate, immediate);

        if (!immediate) {
            _callTargetTotalMinutes = current_hour * 60 + current_minute + encoderDuration;
            _callState = LoopCallState::RUNNING;

            // Reflect the new state (hourglass, synced wheel) right away instead
            // of waiting for the next loop() pass. Identical whether this is a
            // fresh call or an update resend after a pause.
            screenMainUpdate(encoderDuration, current_hour, current_minute, CallState::RUNNING);
        } else {
            // Immediate (0 min)
            _callState = LoopCallState::IDLE;
        }

        // Reflect physical action instantly on Info Screen either way
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
    //  Screen Info (Software I2C - expensive bit-banging)
    //
    //  Rules:
    //  1. Only update when something changes (time, status, action, etc.)
    //  2. Deferred until the encoder has been idle for ENCODER_IDLE_MS,
    //     to avoid stuttering the animated main wheel.
    // ------------------------------------------------------------
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