#include "input/buttons.h"
#include "config.h"

static uint32_t _lastSendPress = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;


void buttonsInit() {
    pinMode(PIN_BTN_SEND, INPUT_PULLUP);
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
