#include <Arduino.h>


const int PIN_LED_BTN = 4;
const uint32_t BLINK_INTERVAL_MS = 1000;

void setup() {
    pinMode(PIN_LED_BTN, OUTPUT);
    digitalWrite(PIN_LED_BTN, LOW);
}

void loop() {
    digitalWrite(PIN_LED_BTN, HIGH);
    delay(BLINK_INTERVAL_MS);
    digitalWrite(PIN_LED_BTN, LOW);
    delay(BLINK_INTERVAL_MS);
}