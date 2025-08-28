/****************************************************************************
 * RATGDO HomeKit
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

// C/C++ language includes
#include <stdint.h>
#ifndef ESP8266
#include <time.h>
#include <esp_timer.h>
#endif

#ifdef ESP8266
typedef uint32_t _millis_t;
#define _millis() ((_millis_t)millis())
#define YIELD()                 \
    do                          \
    {                           \
        system_soft_wdt_feed(); \
        esp_yield();            \
    } while (0)
#else
typedef int64_t _millis_t;
#define _millis() ((_millis_t)(esp_timer_get_time() / 1000LL))
#define YIELD() vTaskDelay(1 / portTICK_PERIOD_MS)
#endif

extern time_t clockSet;
extern uint64_t lastRebootAt;
extern char *timeString(time_t reqTime = 0, bool syslog = false);
extern bool enableNTP;
extern bool get_auto_timezone();
#define NTP_SERVER "pool.ntp.org"

#if defined(MMU_IRAM_HEAP)
// IRAM heap is used only for allocating globals, to leave as much regular heap
// available during operations.  We need to carefully monitor useage so as not
// to exceed available IRAM.  We can adjust the LOG_BUFFER_SIZE (in log.h) if we
// need to make more space available for initialization.
#include <umm_malloc/umm_malloc.h>
#include <umm_malloc/umm_heap_select.h>
#define IRAM_START(tag)           \
    {                             \
        HeapSelectIram ephemeral; \
        ESP_LOGI(tag, "IRAM_START heap (%s): %d", __func__, ESP.getFreeHeap());
#define IRAM_END(tag)                                                     \
    ESP_LOGI(tag, "IRAM_END heap (%s): %d", __func__, ESP.getFreeHeap()); \
    }
#else // MMU_IRAM_HEAP
#define IRAM_START(tag) \
    {                   \
        ESP_LOGI(tag, "Start heap (%s): %d", __func__, ESP.getFreeHeap());
#define IRAM_END(tag)                                                \
    ESP_LOGI(tag, "End heap (%s): %d", __func__, ESP.getFreeHeap()); \
    }
#endif // MMU_IRAM_HEAP

// Controls soft Access Point mode.
extern bool softAPmode;
// Password and credential management for HTTP server...
extern const char www_realm[];
// automatically reboot after X seconds
extern uint32_t rebootSeconds;

// Bitset that identifies what will trigger the motion sensor
typedef struct
{
    uint8_t motion : 1;
    uint8_t obstruction : 1;
    uint8_t lightKey : 1;
    uint8_t doorKey : 1;
    uint8_t lockKey : 1;
    uint8_t undef : 3;
} motionTriggerBitset;
typedef union
{
    motionTriggerBitset bit;
    uint8_t asInt;
} motionTriggersUnion;
extern motionTriggersUnion motionTriggers;

// Function declarations
extern void load_all_config_settings();
extern void sync_and_restart();
extern char *make_rfc952(char *dest, const char *src, int size);
