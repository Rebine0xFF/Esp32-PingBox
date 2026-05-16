#pragma once
#include <Arduino.h>

// ------------------------------------------------------------
//  GPIO MAP
// ------------------------------------------------------------

// Displays — I²C
#define PIN_SCREEN_MAIN_SDA 21      // Screen 1 — Hardware I²C
#define PIN_SCREEN_MAIN_SCL 22
#define PIN_SCREEN_INFO_SDA 25      // Screen 2 — Software I²C
#define PIN_SCREEN_INFO_SCL 26

// Rotary encoder
#define PIN_ENC_CLK         32
#define PIN_ENC_DT          33
#define PIN_ENC_SW          14

// Servo
#define PIN_SERVO           18

// Buttons (input)
#define PIN_BTN_SEND        5
#define PIN_BTN_EMERGENCY   15

// Switches (input)
#define PIN_SW_MENU         27
#define PIN_SW_POWER        13

// LEDs (output) — illuminated button
#define PIN_LED_BTN         4

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