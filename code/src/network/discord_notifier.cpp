#include "network/discord_notifier.h"
#include "config.h"
#include "secrets.h"
#include "input/encoder.h"
#include "display/screen_info_data.h"
#include "network/time_manager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

enum class DiscordState { IDLE, WAITING_ACK };
static DiscordState _state = DiscordState::IDLE;

static String   _pendingMessageId  = "";
static int      _reactionBaseline  = 0;   // 1 if our own seed reaction succeeded, else 0
static uint32_t _pendingSinceMs    = 0;
static uint32_t _lastPollMs        = 0;

// ------------------------------------------------------------

// Performs a single blocking HTTPS request against the Discord REST API.
// Returns the HTTP status code, or a negative HTTPClient error code on
// transport failure. Fills `response` with the response body on return.
//
// NOTE: certificate validation is skipped (setInsecure()) : accepted
// trade-off to avoid maintaining a pinned cert against Cloudflare's
// rotating certificates.
static int _discordRequest(const char* method, const String& url, const String& payload, String& response) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, url)) return -1;

    http.addHeader("Authorization", String("Bot ") + DISCORD_BOT_TOKEN);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "PingBox (https://github.com/Rebine0xFF/Esp32-PingBox, 1.0)");

    int code;
    if (strcmp(method, "POST") == 0)      code = http.POST(payload);
    else if (strcmp(method, "PUT") == 0)  code = http.PUT(payload);
    else                                   code = http.GET();

    response = http.getString();
    http.end();
    return code;
}

// ------------------------------------------------------------

void discordNotifierInit() {
    _state = DiscordState::IDLE;
    _pendingMessageId = "";
}

bool discordSendCallMessage(int duration_minutes) {
    if (_state != DiscordState::IDLE) return false; // already pending an ack

    char content[128];
    snprintf(content, sizeof(content),
             "🔔 On mange dans %d minutes. Réagis avec ✅ pour dire que tu as vu.",
             duration_minutes);

    JsonDocument sendDoc;
    sendDoc["content"] = content;
    String payload;
    serializeJson(sendDoc, payload);

    String url = String(DISCORD_API_BASE) + "/channels/" + DISCORD_CHANNEL_ID + "/messages";
    String response;
    int code = _discordRequest("POST", url, payload, response);
    if (code != 200 && code != 201) return false;

    JsonDocument respDoc;
    if (deserializeJson(respDoc, response) != DeserializationError::Ok) return false;

    const char* messageId = respDoc["id"];
    if (!messageId) return false;
    _pendingMessageId = messageId;

    // Seed the bot's own checkmark reaction:
    String reactUrl = String(DISCORD_API_BASE) + "/channels/" + DISCORD_CHANNEL_ID
                     + "/messages/" + _pendingMessageId + "/reactions/" + DISCORD_ACK_EMOJI + "/@me";
    String reactResponse;
    int seedCode = _discordRequest("PUT", reactUrl, "", reactResponse);
    _reactionBaseline = (seedCode == 204) ? 1 : 0; // account for the seed failing

    _state = DiscordState::WAITING_ACK;
    _pendingSinceMs = millis();
    _lastPollMs = millis();

    char timeStr[6];
    timeManagerGetTimeString(timeStr, sizeof(timeStr));
    setLastAction(LastAction::CALLED, timeStr);

    return true;
}

void discordNotifierUpdate() {
    if (_state != DiscordState::WAITING_ACK) return;

    uint32_t now = millis();

    if (now - _pendingSinceMs > DISCORD_ACK_TIMEOUT_MS) {
        _state = DiscordState::IDLE; // nobody answered = stop polling
        return;
    }

    // Defer polling while the encoder is actively being turned, to avoid
    // stuttering the animated wheel -> same pattern used for screenInfo!
    if (now - encoderGetLastChangeMs() < ENCODER_IDLE_MS) return;

    if (now - _lastPollMs < DISCORD_ACK_POLL_INTERVAL_MS) return;
    _lastPollMs = now;

    String url = String(DISCORD_API_BASE) + "/channels/" + DISCORD_CHANNEL_ID
               + "/messages/" + _pendingMessageId + "/reactions/" + DISCORD_ACK_EMOJI;
    String response;
    int code = _discordRequest("GET", url, "", response);
    if (code != 200) return;

    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) return;
    JsonArray arr = doc.as<JsonArray>();

    // Anything beyond our own seeded reaction means a human acknowledged it.
    if ((int)arr.size() > _reactionBaseline) {
        _state = DiscordState::IDLE;

        char timeStr[6];
        timeManagerGetTimeString(timeStr, sizeof(timeStr));
        setLastAction(LastAction::APPROVED, timeStr);
    }
}

bool discordIsAckPending() {
    return _state == DiscordState::WAITING_ACK;
}