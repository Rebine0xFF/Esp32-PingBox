#include "input/buttons.h"
#include "config.h"

static uint32_t _lastSendPress = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;

static const uint32_t EMERGENCY_DEBOUNCE_MS = 50;

static const uint32_t LED_BLINK_INTERVAL_MS = 1000;
static uint32_t _lastBlinkMs   = 0;
static bool     _ledBlinkState = false;



void buttonsInit() {
    pinMode(PIN_BTN_SEND, INPUT_PULLUP);
    pinMode(PIN_SW_POWER, INPUT_PULLUP);
    pinMode(PIN_BTN_EMERGENCY, INPUT_PULLUP);
    pinMode(PIN_LED_BTN, OUTPUT);
    // LED off until buttonsSetLedReady() is called
    digitalWrite(PIN_LED_BTN, LOW);
}



bool buttonSendPressed() {
    if (digitalRead(PIN_BTN_SEND) == LOW) {
        uint32_t now = millis();
        if (now - _lastSendPress > BTN_DEBOUNCE_MS) {
            _lastSendPress = now;
            return true;
        }
    }
    return false;
}

bool buttonPowerPressed() {
    static uint32_t _lastPowerPress = 0;
    if (digitalRead(PIN_SW_POWER) == LOW) {
        uint32_t now = millis();
        if (now - _lastPowerPress > BTN_DEBOUNCE_MS) {
            _lastPowerPress = now;
            return true;
        }
    }
    return false;
}

bool buttonEmergencyActive() {
    static bool     _stableState  = false;
    static bool     _lastRawState = false;
    static uint32_t _lastEdgeMs   = 0;

    bool raw = (digitalRead(PIN_BTN_EMERGENCY) == HIGH);
    uint32_t now = millis();

    if (raw != _lastRawState) {
        _lastRawState = raw;
        _lastEdgeMs = now;
    }
    if (now - _lastEdgeMs > EMERGENCY_DEBOUNCE_MS) {
        _stableState = raw;
    }
    return _stableState;
}



void buttonsSetLedReady() {
    digitalWrite(PIN_LED_BTN, HIGH);
}

void buttonsLedUpdate(bool callActive) {
    if (!callActive) {
        // Idle: solid on; reset blink phase to a known state.
        digitalWrite(PIN_LED_BTN, HIGH);
        _ledBlinkState = true;
        return;
    }

    uint32_t now = millis();
    if (now - _lastBlinkMs >= LED_BLINK_INTERVAL_MS) {
        _lastBlinkMs = now;
        _ledBlinkState = !_ledBlinkState;
        digitalWrite(PIN_LED_BTN, _ledBlinkState ? HIGH : LOW);
    }
}