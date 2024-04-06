// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef _LOG_H
#define _LOG_H

#include <Arduino.h>
#include "secplus2.h"
#include <esp_xpgm.h>

void print_packet(uint8_t pkt[SECPLUS2_CODE_LEN]);

// #define LOG_MSG_BUFFER

#ifdef LOG_MSG_BUFFER

#define LOG_MSG_FILE "crash_log"
typedef struct logBuffer
{
    uint32_t head;
    char buffer[2044];
} logBuffer;

void logToBuffer_P(const char *fmt, ...);
void printLogBuffer(Print &outDevice = Serial);
void crashCallback();

#define RINFO(message, ...) logToBuffer_P(PSTR(">>> [%7d] RATGDO: " message "\r\n"), millis(), ##__VA_ARGS__)
#define RERROR(message, ...) logToBuffer_P(PSTR("!!! [%7d] RATGDO: " message "\r\n"), millis(), ##__VA_ARGS__)
#else // LOG_MSG_BUFFER

#ifndef UNIT_TEST

#define RINFO(message, ...) XPGM_PRINTF(">>> [%7d] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)
#define RERROR(message, ...) XPGM_PRINTF("!!! [%7d] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)

#else // UNIT_TEST

#include <stdio.h>
#define RINFO(message, ...) printf(">>> RATGDO: " message "\n", ##__VA_ARGS__)
#define RERROR(message, ...) printf("!!! RATGDO: " message "\n", ##__VA_ARGS__)

#endif // UNIT_TEST

#endif // LOG_MSG_BUFFER

#endif // _LOG_H
