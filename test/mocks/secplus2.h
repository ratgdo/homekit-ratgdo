#pragma once

// Mock secplus2.h for native testing
#include <stdint.h>

#define SECPLUS2_CODE_LEN 19

// Mock functions
int decode_wireline(const uint8_t *wireline_packet, uint32_t *rolling, uint64_t *fixed, uint32_t *data);
int encode_wireline(uint32_t rolling, uint64_t fixed, uint32_t data, uint8_t *wireline_packet);