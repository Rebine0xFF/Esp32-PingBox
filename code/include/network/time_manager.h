#pragma once
#include <Arduino.h>

// ============================================================
//  Time Manager - custom raw-UDP NTP client (non-blocking)
//  Handles CET/CEST automatically for France.
//
//  Bypasses the precompiled lwIP SNTP client on purpose: with the
//  Arduino framework on PlatformIO, lwip/sntp.c ships as a prebuilt
//  static library, so its internal random startup delay and
//  exponential retry backoff cannot be tuned via build flags.
//  This module sends its own minimal NTP request instead, giving
//  full control over timing.
//
//  The RTC is formatted into cached strings inside timeManagerUpdate(),
//  throttled to TIME_UPDATE_INTERVAL_MS. Getters only return the
//  cached copies ; they never touch the RTC ; so they are cheap and
//  safe to call every loop() iteration.
// ============================================================

void timeManagerInit();
void timeManagerUpdate();          // call every loop() - internally non-blocking
bool timeManagerIsSynced();

int  timeManagerGetHour();
int  timeManagerGetMinute();

void timeManagerGetTimeString(char* buffer, size_t bufferSize);   // "14:32"
void timeManagerGetDayName(char* buffer, size_t bufferSize);      // "Mar"
void timeManagerGetDayNumber(char* buffer, size_t bufferSize);    // "04"