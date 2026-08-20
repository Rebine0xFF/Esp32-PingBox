#pragma once
#include <Arduino.h>


void encoderInit();

// --- Time selection ---
int  encoderGetMinutes();   // (1–90 min)
uint32_t encoderGetLastChangeMs();

// --- Pause ---
// pause trigger
bool encoderSwitchPressed();
void encoderSetMinutes(int minutes);

// --- Menu navigation support ---
bool encoderSwitchIsDown();
// Raw encoder delta for menu nav
int encoderConsumeDelta();
void encoderResyncMenuDelta();