// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef _LOG_H
#define _LOG_H

#include <Arduino.h>
#include <LittleFS.h>
#include "secplus2.h"
#include <esp_xpgm.h>

void print_packet(uint8_t pkt[SECPLUS2_CODE_LEN]);

// #define LOG_MSG_BUFFER

#ifdef LOG_MSG_BUFFER

#define CRASH_LOG_MSG_FILE "crash_log"
#define REBOOT_LOG_MSG_FILE "reboot_log"
#if defined(MMU_IRAM_HEAP)
// Reduced from 8192 to 4096 to save 4KB IRAM for other critical allocations.
// This still provides sufficient log storage while leaving more IRAM available
// for HomeKit and other services that need it during runtime.
#define LOG_BUFFER_SIZE 2048
#else
#define LOG_BUFFER_SIZE 1024
#endif

typedef struct logBuffer
{
    uint16_t wrapped;                 // two bytes
    uint16_t head;                    // two bytes
    char buffer[LOG_BUFFER_SIZE - 4]; // sized so whole struct is LOG_BUFFER_SIZE bytes
} logBuffer;

#ifndef ESP_LOG_LEVEL_T
#define ESP_LOG_LEVEL_T
typedef enum
{
    ESP_LOG_NONE,   /*!< No log output */
    ESP_LOG_ERROR,  /*!< Critical errors, software module can not recover on its own */
    ESP_LOG_WARN,   /*!< Error conditions from which recovery measures have been taken */
    ESP_LOG_INFO,   /*!< Information messages which describe normal flow of events */
    ESP_LOG_DEBUG,  /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
    ESP_LOG_VERBOSE /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */
} esp_log_level_t;
#endif

extern "C" esp_log_level_t logLevel;
extern "C" void logToBuffer_P(const char *fmt, ...);
void printSavedLog(File file, Print &outDevice = Serial);
void printSavedLog(Print &outDevice = Serial);
void printMessageLog(Print &outDevice = Serial);
void crashCallback();

// #define RATGDO_PRINTF(message, ...) logToBuffer_P(PSTR(message), ##__VA_ARGS__)

// #define RINFO(message, ...) RATGDO_PRINTF(">>> [%7lu] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)
// #define RERROR(message, ...) RATGDO_PRINTF("!!! [%7lu] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)

#define RATGDO_PRINTF(level, message, ...)               \
    do                                                   \
    {                                                    \
        if (level <= logLevel)                           \
            logToBuffer_P(PSTR(message), ##__VA_ARGS__); \
    } while (0)

#define ESP_LOGE(tag, message, ...) RATGDO_PRINTF(ESP_LOG_ERROR, "E (%lu) %s: " message "\r\n", millis(), tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, message, ...) RATGDO_PRINTF(ESP_LOG_WARN, "W (%lu) %s: " message "\r\n", millis(), tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, message, ...) RATGDO_PRINTF(ESP_LOG_INFO, "I (%lu) %s: " message "\r\n", millis(), tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, message, ...) RATGDO_PRINTF(ESP_LOG_DEBUG, "D (%lu) %s: " message "\r\n", millis(), tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, message, ...) RATGDO_PRINTF(ESP_LOG_VERBOSE, "V (%lu) %s: " message "\r\n", millis(), tag, ##__VA_ARGS__)

#define RINFO(message, ...) ESP_LOGI("unknown", message, ##__VA_ARGS__)
#define RERROR(message, ...) ESP_LOGE("unknown", message, ##__VA_ARGS__)

#else // LOG_MSG_BUFFER

#ifndef UNIT_TEST

#define RINFO(message, ...) XPGM_PRINTF(">>> [%7lu] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)
#define RERROR(message, ...) XPGM_PRINTF("!!! [%7lu] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)

#else // UNIT_TEST

#include <stdio.h>
#define RINFO(message, ...) printf(">>> RATGDO: " message "\n", ##__VA_ARGS__)
#define RERROR(message, ...) printf("!!! RATGDO: " message "\n", ##__VA_ARGS__)

#endif // UNIT_TEST

#endif // LOG_MSG_BUFFER

#endif // _LOG_H
