#include <Arduino.h>

#include "display/screen_main.h"
#include "display/screen_info.h"
#include "input/encoder.h"
#include "input/buttons.h"



// ============================================================
//  SETUP
// ============================================================

void setup() {

    screenMainInit();
    screenInfoInit();
    encoderInit();
    buttonsInit();
}

// ============================================================
//  LOOP
// ============================================================

void loop() {

    int duration = encoderGetMinutes();
    // Replace by NTP or RTC soon
    int current_hour   = 19;
    int current_minute = 0;

    screenMainUpdate(duration, current_hour, current_minute);
    // screenInfoUpdate();
}