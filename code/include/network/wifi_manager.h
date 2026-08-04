#pragma once
#include <Arduino.h>


void wifiManagerInit();
void wifiManagerUpdate();
bool wifiManagerIsConnected();
void wifiManagerGetIPString(char* buffer, size_t bufferSize);