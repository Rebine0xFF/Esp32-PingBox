#include "input/encoder.h"
#include "config.h"
#include <ESP32Encoder.h>

static ESP32Encoder enc;

static int _duration_minutes = 0;   // default at startup
static uint32_t _lastChangeMs = 0;  // timestamp of the last detected rotation

static const int MIN_MINUTES = 0;
static const int MAX_MINUTES = 90;

// Edge-detection state for the built-in switch (separate from hold-to-reset logic).
static bool     _switchWasDown    = false;
static uint32_t _lastSwitchEdgeMs = 0;
static const uint32_t SWITCH_DEBOUNCE_MS = 200;

// True once encoderSwitchPressed() has claimed the current press.
// Prevents the hold-to-reset logic from re-triggering while the press is held.
// Cleared on release.
static bool _switchClaimedThisPress = false;


void encoderInit() {

    ESP32Encoder::useInternalWeakPullResistors = puType::up;

    // SingleEdge = 1 physical click = 1 increment -> 1 minute per notch
    enc.attachHalfQuad(PIN_ENC_CLK, PIN_ENC_DT);

    enc.setCount(_duration_minutes);

    pinMode(PIN_ENC_SW, INPUT_PULLUP);
}


int encoderGetMinutes() {

    bool switchDown = (digitalRead(PIN_ENC_SW) == LOW);

    if (switchDown && !_switchClaimedThisPress) {
        enc.setCount(0);
    }
    if (!switchDown) {
        _switchClaimedThisPress = false; // released: next press starts a fresh session
    }

    int raw = (int)enc.getCount();

    if (raw < MIN_MINUTES) {
        enc.setCount(MIN_MINUTES);
        raw = MIN_MINUTES;
    }
    if (raw > MAX_MINUTES) {
        enc.setCount(MAX_MINUTES);
        raw = MAX_MINUTES;
    }

    // Track rotation activity to defer info screen redraws
    if (raw != _duration_minutes) {
        _lastChangeMs = millis();
    }

    _duration_minutes = raw;
    return _duration_minutes;
}

uint32_t encoderGetLastChangeMs() {
    return _lastChangeMs;
}

bool encoderSwitchPressed() {
    bool down = (digitalRead(PIN_ENC_SW) == LOW);
    bool pressed = false;

    if (down && !_switchWasDown) {
        uint32_t now = millis();
        if (now - _lastSwitchEdgeMs > SWITCH_DEBOUNCE_MS) {
            _lastSwitchEdgeMs = now;
            pressed = true;
            _switchClaimedThisPress = true; // suppress encoderGetMinutes()'s reset for the rest of this press
        }
    }
    _switchWasDown = down;
    return pressed;
}

void encoderSetMinutes(int minutes) {
    if (minutes < MIN_MINUTES) minutes = MIN_MINUTES;
    if (minutes > MAX_MINUTES) minutes = MAX_MINUTES;

    enc.setCount(minutes);
    _duration_minutes = minutes;
}