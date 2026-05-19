#include "display/screen_info.h"
#include "display/screen_info_data.h"
#include "config.h"
#include <Arduino.h>
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
    display.clearDisplay();
    display.clearBuffer();
    display.sendBuffer();
}

// ------------------------------------------------------------

static void _drawContent() {

    // [BEGIN lopaka generated]
    static const unsigned char image_action_rect_deco_bits[] = {0x07,0x00,0x00,0x00,0x00,0x00,0x80,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x07,0x00,0x00,0x00,0x00,0x00,0x80,0x03};
    static const unsigned char image_calendar_lower_deco_bits[] = {0x01,0x00,0x04,0x03,0x00,0x06};
    static const unsigned char image_calendar_upper_deco_bits[] = {0x03,0x00,0x06,0x01,0x00,0x04};

    display.setFontMode(1);
    display.setBitmapMode(1);
    // action rect
    display.drawFrame(68, 39, 60, 25);

    // action rect deco
    display.drawXBM(69, 40, 58, 23, image_action_rect_deco_bits);

    // last text
    display.setFont(u8g2_font_haxrcorp4089_tr);
    display.drawStr(68, 29, "Derniere");

    // action text
    display.drawStr(68, 37, "action :");

    // calendar top line
    display.drawLine(105, 0, 123, 0);

    // calendar bottom line
    display.drawLine(123, 16, 105, 16);

    // calendar right line
    display.drawLine(124, 1, 124, 15);

    // calendar left line
    display.drawLine(104, 1, 104, 15);

    // calendar lower deco
    display.drawXBM(105, 14, 19, 2, image_calendar_lower_deco_bits);

    // calendar upper deco
    display.drawXBM(105, 1, 19, 2, image_calendar_upper_deco_bits);

    // separation line
    display.drawLine(63, 63, 63, 0);

    // server text
    display.setFont(u8g2_font_profont10_tr);
    display.drawStr(0, 47, "Serveur:");

    // wifi text
    display.drawStr(0, 56, "Wifi:");

    // arrow vert line
    display.drawLine(0, 57, 0, 61);

    // arrow lower line
    display.drawLine(4, 62, 3, 63);

    // arrow hori line
    display.drawLine(1, 61, 5, 61);

    // arrow upper line
    display.drawLine(4, 60, 3, 59);

    // time underline
    display.drawFrame(1, 16, 60, 2);

    // IP ADRESS
    display.setFont(u8g2_font_4x6_tr);
    display.drawStr(9, 64, IP_ADRESS_text);

    // TIME
    display.setFont(u8g2_font_profont22_tr);
    display.drawStr(2, 14, TIME_text);

    // DAY
    display.setFont(u8g2_font_timR14_tr);
    display.drawStr(66, 14, DAY_text);

    // DAY NUMBER
    display.setFont(u8g2_font_t0_17b_tr);
    display.drawStr(105, 14, DAY_NUMBER_text);

    // LAST ACTION
    display.setFont(u8g2_font_profont10_tr);
    display.drawStr(93, 54, LAST_ACTION_text);

    // LAST ACTION ICON
    display.drawXBM(72, 44, 15, 16, image_LAST_ACTION_ICON_bits);

    // WIFI STATUS
    display.drawXBM(27, 50, image_WIFI_STATUS_bits->w, image_WIFI_STATUS_bits->h, image_WIFI_STATUS_bits->data);

    // SERVER STATUS
    display.drawXBM(42, 41, image_SERVER_STATUS_bits->w, image_SERVER_STATUS_bits->h, image_SERVER_STATUS_bits->data);

    // WIFI ICON
    display.drawXBM(2, 22, 19, 16, image_WIFI_ICON_bits);

    // STATUS FACE
    display.drawXBM(33, 22, 29, 14, image_STATUS_FACE_bits);
    // [END lopaka generated]

}

// ------------------------------------------------------------

void screenInfoUpdate() {
    display.clearBuffer();
    _drawContent();
    display.sendBuffer();
    // Test loop: cycle through statuses
    static uint8_t _test_state = 0;
    static unsigned long _last_change = 0;
    const unsigned long _interval_ms = 3000;
    unsigned long _now = millis();
    if ((_now - _last_change) >= _interval_ms) {
        _last_change = _now;
        Status wifi = static_cast<Status>(_test_state % 3);
        Status server = static_cast<Status>((_test_state + 1) % 3);
        setWifiStatus(wifi);
        setServerStatus(server);
        _test_state++;
    }
}