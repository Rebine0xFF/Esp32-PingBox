#include "input/encoder.h"
#include "config.h"
#include <ESP32Encoder.h>

static ESP32Encoder enc;

static long _lastRawCountForMenu = 0;
static int _duration_minutes = 0;   // default at startup
static uint32_t _lastChangeMs = 0;  // timestamp of the last detected rotation

static const int MIN_MINUTES = 0;
static const int MAX_MINUTES = 90;

// Edge-detection state for the built-in switch.
static bool     _switchWasDown    = false;
static uint32_t _lastSwitchEdgeMs = 0;
static const uint32_t SWITCH_DEBOUNCE_MS = 200;

void encoderInit() {
    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    // SingleEdge = 1 physical click = 1 increment -> 1 minute per notch
    enc.attachHalfQuad(PIN_ENC_CLK, PIN_ENC_DT);
    enc.setCount(_duration_minutes);
    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    _lastRawCountForMenu = enc.getCount();
}

int encoderGetMinutes() {
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


// ------------------------------------------------------------
//  Menu navigation support
// ------------------------------------------------------------

bool encoderSwitchIsDown() {
    // Raw level only; menu handles press timing
    return digitalRead(PIN_ENC_SW) == LOW;
}

int encoderConsumeDelta() {
    long raw = enc.getCount();
    int delta = (int)(raw - _lastRawCountForMenu);
    _lastRawCountForMenu = raw;
    return delta;
}

void encoderResyncMenuDelta() {
    _lastRawCountForMenu = enc.getCount();
}