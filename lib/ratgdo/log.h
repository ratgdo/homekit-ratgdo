/****************************************************************************
 * RATGDO HomeKit for ESP32
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-25 David A Kerr... https://github.com/dkerr64/
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 * Contributions acknowledged from
 * Brandon Matthews... https://github.com/thenewwazoo
 * Jonathan Stroud...  https://github.com/jgstroud
 *
 */
#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#ifdef ESP8266
#include <LittleFS.h>
#else
#include "HomeSpan.h"
#endif

#ifdef ESP8266
// This can be large, but not too large.
// On ESP8266 we save logs in IRAM heap, which is approx 18KB, we also need
// space for other data in here, so during development monitor logs and adjust
// this smaller if necessary.  IRAM malloc's are all done during startup.
#ifdef MMU_IRAM_HEAP
#define LOG_BUFFER_SIZE (1024 * 6)
#else
#define LOG_BUFFER_SIZE 1024
#endif
#else
// On ESP32 we save reboot and crash logs in RTC noinit memory, which is approx 8KB,
// But we have two of them and some other things so..
#define LOG_SAVE_BUFFER_SIZE (512 * 7)
// But we have way more regular RAM for volatile log buffer. 16KB allows us to keep
// all logs from startup until a point where user can request a copy of the log.
#define LOG_BUFFER_SIZE (1024 * 16)
#endif

#define LINE_BUFFER_SIZE 256

extern bool syslogEn;
extern uint32_t syslogPort;
extern char syslogIP[IP4ADDR_STRLEN_MAX];
extern bool suppressSerialLog;

extern time_t rebootTime;
extern time_t crashTime;
extern int32_t crashCount;

typedef struct logBuffer
{
    uint32_t wrapped;
    uint32_t head;
    char buffer[LOG_BUFFER_SIZE - sizeof(wrapped) - sizeof(head)]; // sized so whole struct is LOG_BUFFER_SIZE bytes
} logBuffer;

#ifdef ESP8266
/****************************************************************************
 * On ESP8266 we roll our own log macros... to mimic what is available on ESP32.
 * On ESP32 we use the system log macros and system intercept catch the messages.
 */
#define CRASH_LOG_MSG_FILE "/crash_log"
#define REBOOT_LOG_MSG_FILE "/reboot_log"
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
extern void crashCallback();

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

#endif

class LOG
{
private:
    char *lineBuffer = NULL; // Buffer for single message line
#ifndef ESP8266
    // ESP8266 is single thread and inherently serialized.  No mutex semaphores
    SemaphoreHandle_t logMutex = NULL;
#endif

    static LOG *instancePtr;
    LOG();

public:
    logBuffer *msgBuffer = NULL; // Buffer to save log messages as they occur
#ifdef ESP8266
    File logMessageFile; // File to save log messages on crash
#endif

    LOG(const LOG &obj) = delete;
    static LOG *getInstance() { return instancePtr; }

    void logToBuffer(const char *fmt, va_list args);
    void logToBuffer(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        logToBuffer(fmt, args);
        va_end(args);
    };
    void printSavedLog(Print &outDevice = Serial, bool fromNVram = false);
#ifdef ESP8266
    void printSavedLog(File file, Print &outputDev);
#endif
    void printMessageLog(Print &outDevice = Serial);
    void printCrashLog(Print &outDevice = Serial);
    void saveMessageLog();
};
extern LOG *ratgdoLogger;

extern void esp_log_hook(const char *fmt, va_list args);
