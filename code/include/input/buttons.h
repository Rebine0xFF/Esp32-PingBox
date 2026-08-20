#pragma once

void buttonsInit();

bool buttonSendPressed();
bool buttonPowerPressed();
bool buttonMenuPressed();
bool buttonEmergencyActive();

void buttonsSetLedReady();
void buttonsLedUpdate(bool callActive);