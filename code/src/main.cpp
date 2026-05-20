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
    screenMainUpdate(30, 19, 0);   // 30 min sélectionnées, il est 19h00 → affiche "→ 19h30"
    delay(3000);
}