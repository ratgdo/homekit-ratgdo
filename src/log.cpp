// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include <stdint.h>

#include "log.h"
#include "utilities.h"
#include "secplus2.h"
#include "comms.h"
#include "web.h"
#include <LittleFS.h>
#include <umm_malloc/umm_malloc.h>
#include <umm_malloc/umm_heap_select.h>

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
char *lineBuffer = NULL;
logBuffer *msgBuffer = NULL;
logBuffer *savedLogs = NULL;
File logMessageFile;

void logToBuffer_P(const char *fmt, ...)
{
    if (!lineBuffer)
    {
        // first time in we need to create the buffers
        HeapSelectIram ephemeral;
        lineBuffer = (char *)malloc(1024);
        msgBuffer = (logBuffer *)malloc(sizeof(logBuffer));
        // Fill the buffer with space chars... because if we crash and dump buffer before it fills
        // up, we want blank space not garbage! Nothing is null-terminated in this circular buffer.
        memset(msgBuffer->buffer, 0x20, sizeof(msgBuffer->buffer));
        msgBuffer->wrapped = 0;
        msgBuffer->head = 0;
        savedLogs = (logBuffer *)malloc(sizeof(logBuffer));
        // Open logMessageFile so we don't have to later.
        logMessageFile = (LittleFS.exists(LOG_MSG_FILE)) ? LittleFS.open(LOG_MSG_FILE, "r+") : LittleFS.open(LOG_MSG_FILE, "w+");
        Serial.printf("Opened log message file, size: %d\n", logMessageFile.size());
    }

    // parse the format string into lineBuffer
    va_list args;
    va_start(args, fmt);
    vsnprintf_P(lineBuffer, 1024, fmt, args);
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

void crashCallback()
{
    if (msgBuffer && logMessageFile)
    {
        logMessageFile.seek(0, fs::SeekSet);
        logMessageFile.write((const uint8_t *)msgBuffer, sizeof(logBuffer));
        logMessageFile.close();
    }
    // We may not have enough memory to open the file and save the code
    // save_rolling_code();
}

void printSavedLog(Print &outputDev)
{
    if (savedLogs && logMessageFile && logMessageFile.size() > 0)
    {
        logMessageFile.seek(0, fs::SeekSet);
        logMessageFile.read((uint8_t *)savedLogs, sizeof(logBuffer));
        outputDev.println();
        if (savedLogs->wrapped != 0)
        {
            outputDev.write(&savedLogs->buffer[savedLogs->head], sizeof(savedLogs->buffer) - savedLogs->head);
        }
        outputDev.write(savedLogs->buffer, savedLogs->head);
        outputDev.println();
    }
}

void printMessageLog(Print &outputDev)
{
    if (msgBuffer)
    {
        if (msgBuffer->wrapped != 0)
        {
            outputDev.write(&msgBuffer->buffer[msgBuffer->head], sizeof(msgBuffer->buffer) - msgBuffer->head);
        }
        outputDev.write(msgBuffer->buffer, msgBuffer->head);
    }
}
#endif
