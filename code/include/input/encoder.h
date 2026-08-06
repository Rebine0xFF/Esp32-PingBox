#pragma once
#include <Arduino.h>

void encoderInit();
int  encoderGetMinutes();   // (1–90 min)
uint32_t encoderGetLastChangeMs();