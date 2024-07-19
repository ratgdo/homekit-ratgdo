// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include <stdint.h>

#include "log.h"
#include "utilities.h"
#include "secplus2.h"
#include "comms.h"
#include "web.h"

#if defined(MMU_IRAM_HEAP) && defined(USE_IRAM_HEAP)
#include <umm_malloc/umm_malloc.h>
#include <umm_malloc/umm_heap_select.h>
#endif

#ifndef UNIT_TEST

#include <Arduino.h>

void print_packet(uint8_t pkt[SECPLUS2_CODE_LEN])
{
    RINFO("decoded packet: [%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X]",
          pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5], pkt[6], pkt[7], pkt[8], pkt[9],
          pkt[10], pkt[11], pkt[12], pkt[13], pkt[14], pkt[15], pkt[16], pkt[17], pkt[18]);
}

#else // UNIT_TEST

void print_packet(uint8_t pkt[SECPLUS2_CODE_LEN]) {}

#endif // UNIT_TEST

#ifdef LOG_MSG_BUFFER
#define LINE_BUFFER_SIZE 256
char *lineBuffer = NULL;
logBuffer *msgBuffer = NULL; // Buffer to save log messages as they occur
File logMessageFile;

void logToBuffer_P(const char *fmt, ...)
{
    if (!msgBuffer)
    {
        // first time in we need to create the buffers
#if defined(MMU_IRAM_HEAP) && defined(USE_IRAM_HEAP)
        HeapSelectIram ephemeral;
#endif
        msgBuffer = (logBuffer *)malloc(sizeof(logBuffer));
        lineBuffer = (char *)malloc(LINE_BUFFER_SIZE);
        // Fill the buffer with space chars... because if we crash and dump buffer before it fills
        // up, we want blank space not garbage! Nothing is null-terminated in this circular buffer.
        memset(msgBuffer->buffer, 0x20, sizeof(msgBuffer->buffer));
        msgBuffer->wrapped = 0;
        msgBuffer->head = 0;
        // Open logMessageFile so we don't have to later.
        logMessageFile = (LittleFS.exists(CRASH_LOG_MSG_FILE)) ? LittleFS.open(CRASH_LOG_MSG_FILE, "r+") : LittleFS.open(CRASH_LOG_MSG_FILE, "w+");
        Serial.printf_P(PSTR("Opened log message file, size: %d\n"), logMessageFile.size());
    }

    // parse the format string into lineBuffer
    va_list args;
    va_start(args, fmt);
    vsnprintf_P(lineBuffer, LINE_BUFFER_SIZE, fmt, args);
    va_end(args);
    // print line to the serial port
    Serial.print(lineBuffer);
    // copy the line into the message save buffer
    size_t len = strlen(lineBuffer);
    size_t available = sizeof(msgBuffer->buffer) - msgBuffer->head;
    memcpy(&msgBuffer->buffer[msgBuffer->head], lineBuffer, min(available, len));
    if (available < len)
    {
        // we wrapped on the available buffer space
        msgBuffer->wrapped = 1;
        msgBuffer->head = len - available;
        memcpy(msgBuffer->buffer, &lineBuffer[available], msgBuffer->head);
    }
    else
    {
        msgBuffer->head += len;
    }
    // send it to subscribed browsers
    SSEBroadcastState(lineBuffer, LOG_MESSAGE);
}

#ifdef ENABLE_CRASH_LOG
void crashCallback()
{
    if (msgBuffer && logMessageFile)
    {
        logMessageFile.truncate(0);
        logMessageFile.seek(0, fs::SeekSet);
        logMessageFile.println();
        logMessageFile.write(ESP.checkFlashCRC() ? "Flash CRC OK" : "Flash CRC BAD");
        logMessageFile.println();
        printMessageLog(logMessageFile);
        logMessageFile.close();
    }
    // We may not have enough memory to open the file and save the code
    // save_rolling_code();
}
#endif

void printSavedLog(File file, Print &outputDev)
{
    if (file && file.size() > 0)
    {
        int num = LINE_BUFFER_SIZE;
        file.seek(0, fs::SeekSet);
        while (num == LINE_BUFFER_SIZE)
        {
            num = file.read((uint8_t *)lineBuffer, LINE_BUFFER_SIZE);
            outputDev.write(lineBuffer, num);
        }
        outputDev.println();
    }
}

void printSavedLog(Print &outputDev)
{
    return printSavedLog(logMessageFile, outputDev);
}

// These are defined in the linker script, and filled in by the elf2bin.py util
extern "C" uint32_t __crc_len;
extern "C" uint32_t __crc_val;
// Memory stats from the web.cpp file
extern "C" uint32_t free_heap;
extern "C" uint32_t max_block;
extern "C" uint8_t max_frag;
void printMessageLog(Print &outputDev)
{
    ESP.getHeapStats(&free_heap, &max_block, &max_frag);
    outputDev.write("Firmware Version: ");
    outputDev.write(AUTO_VERSION);
    outputDev.println();
    outputDev.write("Flash CRC: 0x");
    outputDev.println(__crc_val, 16);
    outputDev.write("Flash Length: ");
    outputDev.println(__crc_len);
    outputDev.write("Free heap: ");
    outputDev.println(free_heap);
    outputDev.write("Max malloc size: ");
    outputDev.println(max_block);
    outputDev.write("Fragmentation pct: ");
    outputDev.println(max_frag);
    outputDev.println();
    if (msgBuffer)
    {
        if (msgBuffer->wrapped != 0)
        {
            outputDev.write(&msgBuffer->buffer[msgBuffer->head], sizeof(msgBuffer->buffer) - msgBuffer->head);
        }
        outputDev.write(msgBuffer->buffer, msgBuffer->head);
    }
}
#endif // LOG_MSG_BUFFER
