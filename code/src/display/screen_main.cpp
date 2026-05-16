#include "display/screen_main.h"
#include "config.h"
#include <U8g2lib.h>
#include <Wire.h>


static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    /* reset= */ U8X8_PIN_NONE
);

// ------------------------------------------------------------

void screenMainInit() {
    Wire.begin(PIN_SCREEN_MAIN_SDA, PIN_SCREEN_MAIN_SCL);
    display.begin();
    display.setBusClock(400000);
    display.setContrast(200);
    display.clearDisplay();
}

// ------------------------------------------------------------

static void _drawContent() {

}

// ------------------------------------------------------------

void screenMainUpdate() {

}
