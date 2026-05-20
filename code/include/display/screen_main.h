#pragma once

// ============================================================
//  Screen Main — Hardware I²C (fast)
//  Animated content: timer, visual feedback
// ============================================================

void screenMainInit();
void screenMainUpdate(int duration_minutes, int current_hour, int current_minute);
