#pragma once
#include "config.h"

#if ENABLE_LOGS
#include <Arduino.h>

#define LOG_BEGIN()   Serial.begin(115200)

#define LOG_INFO(tag, fmt, ...)  Serial.printf("[%10lu] [ INFO] [%-8s] " fmt "\r\n", millis(), tag, ##__VA_ARGS__)
#define LOG_OK(tag, fmt, ...)    Serial.printf("[%10lu] [  OK ] [%-8s] " fmt "\r\n", millis(), tag, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  Serial.printf("[%10lu] [ WARN] [%-8s] " fmt "\r\n", millis(), tag, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) Serial.printf("[%10lu] [ERROR] [%-8s] " fmt "\r\n", millis(), tag, ##__VA_ARGS__)
#define LOG_LINE()               Serial.println(F("------------------------------------------------------------"))

#else

#define LOG_BEGIN()
#define LOG_INFO(tag, fmt, ...)
#define LOG_OK(tag, fmt, ...)
#define LOG_WARN(tag, fmt, ...)
#define LOG_ERROR(tag, fmt, ...)
#define LOG_LINE()

#endif