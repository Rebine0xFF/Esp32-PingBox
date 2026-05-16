#pragma once
#include <Arduino.h>

// ------------------------------------------------------------
//  GPIO MAP
// ------------------------------------------------------------

// Displays — I²C
#define PIN_OLED_MAIN_SDA   21      // Screen 1 — Hardware I²C
#define PIN_OLED_MAIN_SCL   22
#define PIN_OLED_INFO_SDA   25      // Screen 2 — Software I²C
#define PIN_OLED_INFO_SCL   26

// Rotary encoder
#define PIN_ENC_CLK         27
#define PIN_ENC_DT          12
#define PIN_ENC_SW          13

// Servo
#define PIN_SERVO           13

// Buttons (input)
#define PIN_BTN_SEND        18
#define PIN_BTN_EMERGENCY   19

// Switches (input)
#define PIN_SW_MENU         23
#define PIN_SW_POWER        5

// LEDs (output) — illuminated button
#define PIN_LED_BTN         16

// ------------------------------------------------------------
//  SERVO POSITIONS (degrees, 0–180)
// ------------------------------------------------------------

constexpr int SERVO_POS_IN    = 0;
constexpr int SERVO_POS_OUT   = 90;

// ------------------------------------------------------------
//  WIFI — fill before flashing
// ------------------------------------------------------------

constexpr char WIFI_SSID[]      = "YOUR_SSID";
constexpr char WIFI_PASSWORD[]  = "YOUR_PASSWORD";