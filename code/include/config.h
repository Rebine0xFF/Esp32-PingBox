#pragma once
#include <Arduino.h>
#include "secrets.h"

// ------------------------------------------------------------
//  GPIO MAP
// ------------------------------------------------------------

// Displays - I²C
#define PIN_SCREEN_MAIN_SDA 21      // Screen 1 - Hardware I²C
#define PIN_SCREEN_MAIN_SCL 22
#define PIN_SCREEN_INFO_SDA 25      // Screen 2 - Software I²C
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

// LEDs (output) - illuminated button
#define PIN_LED_BTN         4

// ------------------------------------------------------------
//  SERVO POSITIONS (degrees, 0–180)
// ------------------------------------------------------------

constexpr int SERVO_POS_IN    = 0;
constexpr int SERVO_POS_OUT   = 90;

//////////////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------
//  NTP / TIME - France (CET/CEST automatique)
//  Custom raw-UDP NTP client (bypasses the precompiled lwIP SNTP
//  client, whose startup-delay/backoff behavior can't be tuned
//  through build flags with the Arduino framework on PlatformIO)
// ------------------------------------------------------------

constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char TZ_FRANCE[]    = "CET-1CEST,M3.5.0,M10.5.0/3";

constexpr uint16_t NTP_LOCAL_PORT           = 2390;
constexpr uint32_t NTP_REQUEST_TIMEOUT_MS   = 1500;      // max wait for a response
constexpr uint32_t NTP_RETRY_INTERVAL_MS    = 2000;      // delay between retries until synced
constexpr uint32_t NTP_RESYNC_INTERVAL_MS   = 3600000UL; // re-sync once an hour once synced

constexpr uint32_t TIME_UPDATE_INTERVAL_MS  = 1000;      // throttle for re-formatting the cache

// ------------------------------------------------------------
//  DISCORD
// ------------------------------------------------------------

constexpr char DISCORD_API_BASE[]  = "https://discord.com/api/v10";
constexpr char DISCORD_ACK_EMOJI[] = "%E2%9C%85";

constexpr uint32_t DISCORD_ACK_POLL_INTERVAL_MS = 3000;
constexpr uint32_t DISCORD_ACK_TIMEOUT_MS       = 600000;

// ------------------------------------------------------------
//  Miscellaneous
// ------------------------------------------------------------

constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;

// Info screen updates are deferred while the encoder is actively
// rotating, and fire once it has been idle for this long (ms).
constexpr uint32_t ENCODER_IDLE_MS = 500;