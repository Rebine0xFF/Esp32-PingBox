#pragma once
#include <Arduino.h>

// ============================================================
//  Time Manager - sync NTP via configTzTime (non-blocking)
//  Automatically handles CET/CEST for France
//
//  The RTC is read and formatted ONCE per TIME_UPDATE_INTERVAL_MS
//  inside timeManagerUpdate(), then cached. Getters only return
//  the cached copies — they never touch the RTC — so they are
//  cheap and safe to call every loop() iteration.
// ============================================================

void timeManagerInit();
void timeManagerUpdate();
bool timeManagerIsSynced();

int  timeManagerGetHour();
int  timeManagerGetMinute();

void timeManagerGetTimeString(char* buffer, size_t bufferSize);   // "14:32"
void timeManagerGetDayName(char* buffer, size_t bufferSize);      // "Mar"
void timeManagerGetDayNumber(char* buffer, size_t bufferSize);    // "04"