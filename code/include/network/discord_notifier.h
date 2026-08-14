#pragma once
#include <Arduino.h>

// ============================================================
//  Discord Notifier - sends a call message to a Discord channel
//  and polls for a checkmark reaction to detect acknowledgment.
//
//  All Discord HTTPS calls (POST/PUT/GET) run on a dedicated
//  FreeRTOS task pinned to the other core (see discordNotifierInit()).
//  TLS handshakes with Cloudflare commonly take ~1s on the ESP32,
//  and the Arduino HTTPClient has no async mode - running these
//  calls directly in loop() would freeze the screens for their
//  entire duration, repeatedly, for as long as a message is
//  pending acknowledgment. Backgrounding them keeps rendering
//  perfectly smooth regardless of network latency.
//
//  The on-screen countdown (main.cpp) is intentionally independent
//  from this module's internal state: acknowledging a message on
//  Discord only updates the "last action" display, it never stops
//  the countdown.
// ============================================================

enum class DiscordServerStatus { PAUSED, OK, ERROR };

void discordNotifierInit();   // creates the background task ; call once from setup()

// Queues a new call message + seed reaction, built from the given
// duration and current clock time (captured here rather than having
// the background task read time_manager's cache from another task).
// Returns immediately (never blocks). Returns false only if a send
// is already queued and not yet picked up by the background task.
bool discordSendCallMessage(int duration_minutes, int current_hour, int current_minute);

// True while a send/ack-poll cycle is in progress on the Discord side.
bool discordIsAckPending();

// Checks if an ACK was received since last call, and clears the flag.
// Called safely from the main UI thread.
bool discordCheckAndClearAck();

// Status of the last Discord HTTP call (send or ack poll).
// safe to call every loop()
DiscordServerStatus discordGetServerStatus();