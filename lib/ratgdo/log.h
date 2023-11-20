#ifndef _LOG_H
#define _LOG_H

#include "secplus2.h"

void print_packet(uint8_t pkt[SECPLUS2_CODE_LEN]);

#ifndef UNIT_TEST

#include <esp_xpgm.h>

#define RINFO(message, ...) XPGM_PRINTF(">>> [%7d] RATGDO: " message "\n", millis(), ##__VA_ARGS__)
#define RERROR(message, ...) XPGM_PRINTF("!!! [%7d] RATGDO: " message "\n", millis(), ##__VA_ARGS__)

#else  // UNIT_TEST

#define RINFO(...) do {} while (0);
#define RERROR(...) do {} while (0);

#endif // UNIT_TEST

#endif // _LOG_H
