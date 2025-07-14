#pragma once

// Mock Arduino.h for native testing
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Basic types
typedef uint8_t byte;

// Mock functions
inline uint32_t millis() { return 12345; }
inline void delay(uint32_t ms) { (void)ms; }

// Print class mock
class Print {
public:
    virtual size_t write(uint8_t) { return 1; }
    virtual size_t write(const uint8_t *buffer, size_t size) { (void)buffer; return size; }
    virtual size_t print(const char* str) { printf("%s", str); return strlen(str); }
    virtual size_t println(const char* str) { printf("%s\n", str); return strlen(str) + 1; }
};

// Serial mock
extern Print Serial;

// Memory functions
inline void* malloc(size_t size) { return ::malloc(size); }
inline void free(void* ptr) { ::free(ptr); }

// PROGMEM support
#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))

// Digital pin functions
#define INPUT 0
#define OUTPUT 1
#define HIGH 1
#define LOW 0

inline void pinMode(uint8_t pin, uint8_t mode) { (void)pin; (void)mode; }
inline void digitalWrite(uint8_t pin, uint8_t value) { (void)pin; (void)value; }
inline int digitalRead(uint8_t pin) { (void)pin; return LOW; }