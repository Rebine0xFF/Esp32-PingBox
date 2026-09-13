#pragma once
#include <Arduino.h>

// ============================================================
//  Ntfy Notifier: sends a call message to ntfy.sh and detects
//  acknowledgment via a dedicated ack topic.
// ============================================================

enum class NtfyServerStatus { PAUSED, OK, ERROR };

void ntfyNotifierInit();

// Queues a new call message + ack action button. Returns immediately
// (never blocks). Returns false only if a send is already queued and
// not yet picked up by the background task.
bool ntfySendCallMessage(int duration_minutes, int current_hour, int current_minute, bool isUpdate);

// Queues a distinctive emergency alert. No ack tracking, same as the
// previous notifier: the emergency workflow doesn't wait for a reply.
bool ntfySendEmergencyMessage(int current_hour, int current_minute);

// Checks if an ACK was received since last call, and clears the flag.
// Safe to call from the main UI thread.
bool ntfyCheckAndClearAck();


NtfyServerStatus ntfyGetServerStatus();