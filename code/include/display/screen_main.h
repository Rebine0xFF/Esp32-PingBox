#pragma once

// ============================================================
//  Screen Main — Hardware I²C (fast)
//  Animated content: timer, visual feedback
// ============================================================

// NONE      - no call in progress, wheel follows the encoder.
// RUNNING   - countdown active, hourglass animation shown.
// PAUSED    - countdown frozen, pause icon shown instead of the hourglass.
// EMERGENCY - emergency latch tripped, wheel replaced by the alert screen.
enum class CallState { NONE, RUNNING, PAUSED, EMERGENCY };

void screenMainInit();
void screenMainUpdate(int duration_minutes, int current_hour, int current_minute, CallState callState);
void screenMainSleep();