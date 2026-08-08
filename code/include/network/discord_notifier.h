#pragma once
#include <Arduino.h>

// ============================================================
//  Discord Notifier - sends a call message to a Discord channel
//  and polls for a checkmark reaction to detect acknowledgment.
//
//  Fully self-contained on the ESP32: no external bot/service
//  required. Uses the Discord REST API directly over HTTPS with
//  a bot token (no Gateway/WebSocket connection).
// ============================================================

void discordNotifierInit();
void discordNotifierUpdate();   // call every loop() ; internally throttled, non-blocking between calls

// Sends a new call message and seeds it with the bot's own checkmark
// reaction. Blocking (~1-2s, two HTTPS calls) ; acceptable since this
// only runs once per physical button press, not every loop().
// Ignored (returns false) if a message is already pending acknowledgment.
bool discordSendCallMessage(int duration_minutes);

bool discordIsAckPending();