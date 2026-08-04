#pragma once
#include <Arduino.h>

// ============================================================
//  Time Manager - sync NTP via configTzTime (non-blocking)
//  Automatically handles CET/CEST for France
// ============================================================

void timeManagerInit();
void timeManagerUpdate();
bool timeManagerIsSynced();

int  timeManagerGetHour();
int  timeManagerGetMinute();

void timeManagerGetTimeString(char* buffer, size_t bufferSize);   // "14:32"
void timeManagerGetDayName(char* buffer, size_t bufferSize);      // "Mar"
void timeManagerGetDayNumber(char* buffer, size_t bufferSize);    // "04"