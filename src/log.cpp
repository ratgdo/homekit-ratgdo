// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include <stdint.h>

#include "log.h"
#include "utilities.h"
#include "secplus2.h"
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

void logToBuffer_P(const char *fmt, ...)
{
    if (!lineBuffer)
    {
        // first time in we need to create the buffers
        HeapSelectIram ephemeral;
        lineBuffer = (char *)malloc(1024);
        msgBuffer = (logBuffer *)malloc(sizeof(logBuffer));
        msgBuffer->head = 0;
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
        msgBuffer->head = len - available;
        memcpy(msgBuffer->buffer, &lineBuffer[available], msgBuffer->head);
    }
    else
    {
        msgBuffer->head += len;
    }
}

void crashCallback()
{
    if (!msgBuffer)
        return;
    write_data_to_file(LOG_MSG_FILE, msgBuffer, sizeof(logBuffer));
}

void printLogBuffer(Print &outputDev)
{
    if (!msgBuffer)
        return;
    logBuffer *savedLogs = NULL;
    {
        HeapSelectIram ephemeral;
        savedLogs = (logBuffer *)malloc(sizeof(logBuffer));
    }
    if (!savedLogs)
        return;

    if (read_data_from_file(LOG_MSG_FILE, savedLogs, sizeof(logBuffer)))
    {
        outputDev.println();
        outputDev.write(&savedLogs->buffer[savedLogs->head], sizeof(savedLogs->buffer) - savedLogs->head);
        outputDev.write(savedLogs->buffer, savedLogs->head);
        outputDev.println();
    }
    free(savedLogs);
}

#endif
