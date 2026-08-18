#pragma once

void buttonsInit();

bool buttonSendPressed();
bool buttonPowerPressed();
bool buttonEmergencyActive();

void buttonsSetLedReady();
void buttonsLedUpdate(bool callActive);