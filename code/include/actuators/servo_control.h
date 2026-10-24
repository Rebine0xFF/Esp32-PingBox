#pragma once

void servoControlInit();
void servoControlSetPosition(int degrees);
void servoControlDetach();

// Call every loop(): advances the non-blocking flag motion.
void servoControlUpdate();
// True while a move or its settle hold is in progress.
bool servoControlIsBusy();
// An acknowledgment arrived: raise the flag, or shake it if already up.
void servoFlagAcknowledged();
void servoFlagLower();