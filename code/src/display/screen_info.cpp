#include "display/screen_info.h"
#include "config.h"
#include <U8g2lib.h>


static U8G2_SH1106_128X64_NONAME_F_SW_I2C display(
    U8G2_R0,
    /* clock= */ PIN_SCREEN_INFO_SCL,
    /* data=  */ PIN_SCREEN_INFO_SDA,
    /* reset= */ U8X8_PIN_NONE
);

// ------------------------------------------------------------

void screenInfoInit() {
    display.begin();
    display.setBusClock(100000);
    display.setContrast(180);
    display.clearDisplay();
}

// ------------------------------------------------------------

static void _drawContent() {

}

// ------------------------------------------------------------

void screenInfoUpdate() {

}
