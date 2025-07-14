#include "Arduino.h"
#include "LittleFS.h"
#include "secplus2.h"
#include <cstring>

// Mock Serial instance
Print Serial;

// Mock LittleFS instance
LittleFSClass LittleFS;

// Mock secplus2 functions
int decode_wireline(const uint8_t *wireline_packet, uint32_t *rolling, uint64_t *fixed, uint32_t *data) {
    // Mock implementation - just return success
    if (rolling) *rolling = 0x12345;
    if (fixed) *fixed = 0x67890ABCDEF;
    if (data) *data = 0x123;
    return 0;
}

int encode_wireline(uint32_t rolling, uint64_t fixed, uint32_t data, uint8_t *wireline_packet) {
    // Mock implementation - fill with test data
    if (wireline_packet) {
        memset(wireline_packet, 0x55, SECPLUS2_CODE_LEN);
    }
    return 0;
}