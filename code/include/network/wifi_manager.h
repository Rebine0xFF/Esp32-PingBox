#pragma once
#include <Arduino.h>

// ============================================================
//  WiFi Manager - non-blocking connection & reconnection
// ============================================================

void wifiManagerInit();
void wifiManagerUpdate();
bool wifiManagerIsConnected();
void wifiManagerGetIPString(char* buffer, size_t bufferSize);