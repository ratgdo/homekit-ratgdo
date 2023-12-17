// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef _LOG_H
#define _LOG_H

#include "secplus2.h"

void print_packet(uint8_t pkt[SECPLUS2_CODE_LEN]);

#ifndef UNIT_TEST

#include <Syslog.h>
extern Syslog syslog;
#define RINFO(message, ...) syslog.logf(LOG_INFO, ">>> [%7d] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)
#define RERROR(message, ...) syslog.logf(LOG_ERR, "!!! [%7d] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)
/*
#include <esp_xpgm.h>

#define RINFO(message, ...) XPGM_PRINTF(">>> [%7d] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)
#define RERROR(message, ...) XPGM_PRINTF("!!! [%7d] RATGDO: " message "\r\n", millis(), ##__VA_ARGS__)
*/

#else  // UNIT_TEST

#include <stdio.h>

#define RINFO(message, ...) printf(">>> RATGDO: " message "\n", ##__VA_ARGS__)
#define RERROR(message, ...) printf("!!! RATGDO: " message "\n", ##__VA_ARGS__)

#endif // UNIT_TEST

#endif // _LOG_H
