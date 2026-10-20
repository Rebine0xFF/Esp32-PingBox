#pragma once
#include <Arduino.h>

// ============================================================
//  Ntfy Notifier: sends a call message to ntfy.sh and detects
//  acknowledgment via a dedicated ack topic.
//
//  The "meal is now" ping is NOT sent when the countdown hits
//  zero. It is armed with ntfy's delayed delivery at the moment
//  the manual send succeeds, then cancelled or replaced if the
//  user pauses, updates, or trips the emergency switch.
// ============================================================

enum class NtfyServerStatus { PAUSED, OK, ERROR };

void ntfyNotifierInit();

// Queues a call message + ack action. When duration_minutes >= 1,
// also arms (or replaces) the delayed "meal is now" reminder.
// Returns false only if a send is already queued.
bool ntfySendCallMessage(int duration_minutes, int current_hour, int current_minute, bool isUpdate);

// Queues a distinctive emergency alert. No ack tracking.
// Also cancels any armed meal reminder.
bool ntfySendEmergencyMessage(int current_hour, int current_minute);

// Drops the armed meal reminder. Always accepted. A send queued
// afterwards replaces the reminder and clears this request.
void ntfyCancelReadyMessage();

// Checks if an ACK was received since last call, and clears the flag.
// Safe to call from the main UI thread.
bool ntfyCheckAndClearAck();

NtfyServerStatus ntfyGetServerStatus();