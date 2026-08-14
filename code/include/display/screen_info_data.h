#pragma once
#include <stdint.h>

extern char TIME_text[6];
extern char DAY_text[4];
extern char DAY_NUMBER_text[3];
extern char IP_ADRESS_text[16];


//////////////////////////////////////////
extern char LAST_ACTION_text[6];

enum class LastAction { NONE, CALLED, APPROVED, URGENT };

void setLastAction(LastAction action, const char* timeText);
uint32_t getLastActionVersion();
//////////////////////////////////////////


//****************************************
extern const unsigned char* image_LAST_ACTION_ICON_bits;
extern const unsigned char* image_WIFI_ICON_bits;
extern const unsigned char* image_STATUS_FACE_bits;


struct Icon {
    const unsigned char* data;
    uint8_t w;
    uint8_t h;
};
enum class Status { OK, PAUSED, ERROR };

extern const Icon* image_WIFI_STATUS_bits;
extern const Icon* image_SERVER_STATUS_bits;

void setWifiStatus(Status s);
void setServerStatus(Status s);
void setWifiIcon(bool connected);
void setStatusFace(bool ok);
//****************************************