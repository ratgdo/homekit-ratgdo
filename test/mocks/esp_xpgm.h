#pragma once

// Mock esp_xpgm.h for native testing
#include <stdio.h>
#include <stdarg.h>

// Mock XPGM_PRINTF
#define XPGM_PRINTF(format, ...) printf(format, ##__VA_ARGS__)