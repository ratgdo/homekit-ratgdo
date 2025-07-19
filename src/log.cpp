// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include <stdint.h>
#include <WiFiUdp.h>

#include "log.h"
#include "utilities.h"
#include "secplus2.h"
#include "comms.h"
#include "web.h"

#ifndef UNIT_TEST

#include <Arduino.h>

// Logger tag
static const char *TAG = "ratgdo-logger";

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

#define SYSLOG_LOCAL0 16
#define SYSLOG_EMERGENCY 0
#define SYSLOG_ALERT 1
#define SYSLOG_CRIT 2
#define SYSLOG_ERROR 3
#define SYSLOG_WARN 4
#define SYSLOG_NOTICE 5
#define SYSLOG_INFO 6
#define SYSLOG_DEBUG 7
#define SYSLOG_NIL "-"
#define SYSLOG_BOM "\xEF\xBB\xBF"

esp_log_level_t logLevel = ESP_LOG_VERBOSE;
WiFiUDP syslog;

void logToSyslog(char *message)
{
    if (!syslogEn || !WiFi.isConnected())
        return;

    uint8_t PRI = SYSLOG_LOCAL0 * 8;
    if (*message == '>')
        PRI += SYSLOG_INFO;
    else if (*message == '!')
        PRI += SYSLOG_ERROR;
    else if (*message == 'I')
        PRI += SYSLOG_INFO;
    else if (*message == 'E')
        PRI += SYSLOG_ERROR;
    else if (*message == 'W')
        PRI += SYSLOG_WARN;
    else if (*message == 'D')
        PRI += SYSLOG_DEBUG;
    else if (*message == 'V')
        PRI += SYSLOG_DEBUG;

    char *app_name;
    char *msg;
    static char unk[] = "unknown";

    // Strip out the TAG name which is embedded in the log message.
    // Can be bound by [timestamp] tagname: message
    // So extract from the close square bracket to the colon.
    // If no close square bracket, then message format may be
    // (timestamp) tagname:
    char *sqr = strchr(message, ']');
    char *brk = strchr(message, ')');
    if (sqr && brk)
    {
        if (sqr < brk)
            app_name = strtok(message, "]");
        else
            app_name = strtok(message, ")");
    }
    else if (sqr)
        app_name = strtok(message, "]");
    else if (brk)
        app_name = strtok(message, ")");
    else
        app_name = NULL;

    if (app_name)
    {
        while (*app_name == ' ')
            app_name++;
        app_name = strtok(NULL, ":");
        while (*app_name == ' ')
            app_name++;
        msg = strtok(NULL, "\r\n");
        while (*msg == ' ')
            msg++;
    }
    else
    {
        // neither a close close square or regular bracket then cannot determine tag name
        app_name = unk;
        msg = message;
    }
    syslog.beginPacket(userConfig->syslogIP, userConfig->syslogPort);
    // Use RFC5424 Format
    syslog.printf("<%u>1 ", PRI); // PRI code
#if defined(USE_NTP_TIMESTAMP)
    syslog.print((enableNTP && clockSet) ? timeString(0, true) : SYSLOG_NIL);
#else
    syslog.print(SYSLOG_NIL); // Time - let the syslog server insert time
#endif
    syslog.print(" ");
    syslog.print(device_name_rfc952); // hostname
    syslog.print(" ");
    syslog.print(app_name);     // application name
    syslog.printf(" 0");        // process ID
    syslog.print(" " SYSLOG_NIL // message ID
                 " " SYSLOG_NIL // structured data
#ifdef USE_UTF8_BOM
                 " " SYSLOG_BOM); // BOM - indicates UTF-8 encoding
#else
                 " "); // No BOM
#endif
    syslog.print(msg); // message
    syslog.endPacket();
}

void logToBuffer_P(const char *fmt, ...)
{
    if (!msgBuffer)
    {
        // first time in we need to create the buffers
        Serial.printf_P(PSTR("Allocating memory for logs\n"));
        IRAM_START
        // IRAM heap is used only for allocating globals, to leave as much regular heap
        // available during operations.  We need to carefully monitor useage so as not
        // to exceed available IRAM.  We can adjust the LOG_BUFFER_SIZE (in log.h) if we
        // need to make more space available for initialization.
#if defined(MMU_IRAM_HEAP)
        Serial.printf_P(PSTR("IRAM heap size %d\n"), MMU_SEC_HEAP_SIZE);
#endif
        msgBuffer = (logBuffer *)malloc(sizeof(logBuffer));
        if (!msgBuffer) {
            Serial.println("FATAL: Failed to allocate message log buffer");
            ESP.restart();
            return;
        }
        Serial.printf_P(PSTR("Allocated %d bytes for message log buffer\n"), sizeof(logBuffer));
        lineBuffer = (char *)malloc(LINE_BUFFER_SIZE);
        if (!lineBuffer) {
            Serial.println("FATAL: Failed to allocate line buffer");
            ESP.restart();
            return;
        }
        Serial.printf_P(PSTR("Allocated %d bytes for line buffer\n"), LINE_BUFFER_SIZE);
        // Fill the buffer with space chars... because if we crash and dump buffer before it fills
        // up, we want blank space not garbage! Nothing is null-terminated in this circular buffer.
        memset(msgBuffer->buffer, 0x20, sizeof(msgBuffer->buffer));
        msgBuffer->wrapped = 0;
        msgBuffer->head = 0;
        // Open logMessageFile so we don't have to later.
        logMessageFile = (LittleFS.exists(CRASH_LOG_MSG_FILE)) ? LittleFS.open(CRASH_LOG_MSG_FILE, "r+") : LittleFS.open(CRASH_LOG_MSG_FILE, "w+");
        Serial.printf_P(PSTR("Opened log message file, size: %d\n"), logMessageFile.size());
        IRAM_END("Log buffers allocated");
    }

    // parse the format string into lineBuffer
    va_list args;
    va_start(args, fmt);
    vsnprintf_P(lineBuffer, LINE_BUFFER_SIZE, fmt, args);
    va_end(args);
    // If timestamp is wrapped in () and not [] then message is from one of the ESP_LOGx() functions.
    // Insert a period into the milliseconds timestamp, so number of seconds is easier to read.
    if (strchr(lineBuffer, '(') - lineBuffer == 2)
    {
        char *closeBracket = strchr(lineBuffer, ')');
        int i = closeBracket - lineBuffer;
        if (i > 6)
        {
            memmove(&closeBracket[-2], &closeBracket[-3], LINE_BUFFER_SIZE - (i - 1));
            closeBracket[-3] = '.';
        }
    }

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
    msgBuffer->buffer[msgBuffer->head] = 0; // null terminate

    static bool inFn = false;
    if (!inFn)
    {
        // Control recursion... make sure we don't get into a loop if any
        // of the functions we use here log a message.
        inFn = true;
        // send it to subscribed browsers
        SSEBroadcastState(lineBuffer, LOG_MESSAGE);
        // send it to syslog server
        logToSyslog(lineBuffer);
        inFn = false;
    }
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
// Memory stats
extern "C" uint32_t free_heap;
extern "C" uint32_t min_heap;

void printMessageLog(Print &outputDev)
{
#ifdef NTP_CLIENT
    if (enableNTP && clockSet)
    {
        outputDev.write("Server time (secs): ");
        outputDev.println(time(NULL));
    }
#endif
    outputDev.write("Server uptime (ms): ");
    outputDev.println(millis());
    outputDev.write("Firmware version: ");
    outputDev.write(AUTO_VERSION);
    outputDev.println();
    outputDev.write("Flash CRC: 0x");
    outputDev.println(__crc_val, 16);
    outputDev.write("Flash length: ");
    outputDev.println(__crc_len);
    outputDev.write("Free heap: ");
    outputDev.println(free_heap);
    outputDev.write("Minimum heap: ");
    outputDev.println(min_heap);
#if defined(MMU_IRAM_HEAP)
    outputDev.write("IRAM heap size: ");
    outputDev.println(MMU_SEC_HEAP_SIZE);
#endif
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
