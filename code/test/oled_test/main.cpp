#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// SCREEN 1: Hardware I2C (Pins 21/22) - Optimized for SPEED
U8G2_SH1106_128X64_NONAME_F_HW_I2C display1(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// SCREEN 2: Software I2C (Pins 25/26) - Optimized for STATIC content
U8G2_SH1106_128X64_NONAME_F_SW_I2C display2(U8G2_R0, /* clock=*/ 26, /* data=*/ 25, /* reset=*/ U8X8_PIN_NONE);

// Animation variables
int xPos = 0;
int direction = 1;

void setup() {
    // 1. Start Hardware I2C at 400kHz (Fast Mode)
    Wire.begin(21, 22);
    display1.begin();
    display1.setBusClock(400000); 

    // 2. Start Software I2C
    display2.begin();

    // 3. PRE-RENDER SCREEN 2 (Static Text)
    // We do this in setup so we don't have to waste time in loop()
    display2.clearBuffer();
    display2.setFont(u8g2_font_ncenB12_tr);
    display2.drawStr(15, 30, "MONITOR A");
    display2.setFont(u8g2_font_6x10_tf);
    display2.drawStr(20, 50, "Status: Static");
    display2.drawFrame(0, 0, 128, 64);
    display2.sendBuffer(); 
}

void loop() {
    // ---------------------------------------------------
    // ANIMATION ON SCREEN 1 (Hardware Bus)
    // ---------------------------------------------------
    display1.clearBuffer();
    
    // Draw moving elements
    display1.setFont(u8g2_font_ncenB14_tr);
    display1.drawStr(xPos, 35, "FAST");
    
    // Draw a moving bar at the bottom
    display1.drawBox(0, 55, xPos + 20, 5);
    
    display1.sendBuffer();

    // Logic for movement
    xPos += (2 * direction);
    if (xPos > 80 || xPos < 0) {
        direction = -direction;
    }

    // Notice: We are NOT calling display2.sendBuffer() here.
    // This keeps the loop running at maximum speed.

    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 3000) { // Update only once per 3 second
        display2.clearBuffer();
        display2.setCursor(20, 50);
        display2.print(millis() / 1000); // Show seconds uptime
        display2.sendBuffer();
        lastUpdate = millis();
    }
}