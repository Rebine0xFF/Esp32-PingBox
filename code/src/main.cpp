#include <Arduino.h>

#include "display/screen_main.h"
#include "display/screen_info.h"



// ============================================================
//  SETUP
// ============================================================

void setup() {

    screenMainInit();
    screenInfoInit();

}

// ============================================================
//  LOOP
// ============================================================

void loop() {
    screenInfoUpdate();
    delay(3000);
}