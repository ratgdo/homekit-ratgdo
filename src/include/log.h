// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License
#pragma once

// see log.cpp for a note about this unused function
// void print_packet(uint8_t pkt[19]); // MAGIC NUMBER from "secplus.h" TODO FIXME

#ifndef TAG
#error "Must define TAG before including this file"
#endif

#ifndef UNIT_TEST

#include <esp_log.h>

#define RINFO(message, ...) ESP_LOGI(TAG, ">>> [%7d] RATGDO: " message "\r\n", esp_log_early_timestamp(), ##__VA_ARGS__)
#define RERROR(message, ...) ESP_LOGE(TAG, "!!! [%7d] RATGDO: " message "\r\n", esp_log_early_timestamp(), ##__VA_ARGS__)

#else  // UNIT_TEST

#include <stdio.h>

#define RINFO(message, ...) printf(TAG ">>> RATGDO: " message "\n", ##__VA_ARGS__)
#define RERROR(message, ...) printf(TAG "!!! RATGDO: " message "\n", ##__VA_ARGS__)

#endif // UNIT_TEST
