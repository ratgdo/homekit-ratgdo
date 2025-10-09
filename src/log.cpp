/****************************************************************************
 * RATGDO HomeKit
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-25 David A Kerr... https://github.com/dkerr64/
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 * Contributions acknowledged from
 * Brandon Matthews... https://github.com/thenewwazoo
 * Jonathan Stroud...  https://github.com/jgstroud
 *
 */

// C/C++ language includes
#include <stdint.h>

// Arduino includes
#include <WiFiUdp.h>
#ifndef ESP8266
#include <esp32-hal.h>
#include <esp_core_dump.h>
#endif

// RATGDO project includes
#include "ratgdo.h"
#include "config.h"
#include "utilities.h"
#include "web.h"

// Logger tag
static const char *TAG = "ratgdo-logger";

// Construct the singleton object for logger access
LOG *LOG::instancePtr = new LOG();
LOG *ratgdoLogger = LOG::getInstance();

void logToSyslog(char *message);
bool syslogEn = false;
uint32_t syslogPort = 514;
char syslogIP[IP4ADDR_STRLEN_MAX] = "";
uint32_t syslogFacility = SYSLOG_LOCAL0;
WiFiUDP syslog;
bool suppressSerialLog = false;
esp_log_level_t logLevel = ESP_LOG_VERBOSE;

#define SERIAL_PRINT(x)         \
    do                          \
    {                           \
        if (!suppressSerialLog) \
            Serial.print(x);    \
    } while (0)

// Allows us to intercept all ESP_LOGx() even outside our code.
void esp_log_hook(const char *fmt, va_list args)
{
    if (!Serial)
    {
        // Before we can log anything, we need to initialize the serial port.
        Serial.begin(115200);
        while (!Serial)
            ; // Wait for serial port to open
        Serial.print("\n\n\n");
    }
    if (ratgdoLogger)
    {
        ratgdoLogger->logToBuffer(fmt, args);
    }
    else if (!suppressSerialLog)
    {
        // We should only ever come here if ratgdoLogger has not been constructed
        // which would only be in very early part of booting the system
        char buf[64];
        vsnprintf(buf, 64, fmt, args);
        Serial.print(buf);
    }
}

#ifdef ESP8266
// These are defined in the linker script, and filled in by the elf2bin.py util
extern "C" uint32_t __crc_len;
extern "C" uint32_t __crc_val;
// Keep track of number of times crashed
void crashCallback();
int32_t crashCount = 0;
EspSaveCrash saveCrash(1408, 1024, true, &crashCallback);
// ESP8266 is single core / single threaded, no mutex's.
#define TAKE_MUTEX()
#define GIVE_MUTEX()

void crashCallback()
{
    if (ratgdoLogger->msgBuffer && ratgdoLogger->logMessageFile)
    {
        ratgdoLogger->logMessageFile.truncate(0);
        ratgdoLogger->logMessageFile.seek(0, fs::SeekSet);
        ratgdoLogger->logMessageFile.print("\n");
        ratgdoLogger->logMessageFile.write(ESP.checkFlashCRC() ? "Flash CRC OK" : "Flash CRC BAD");
        ratgdoLogger->logMessageFile.print("\n");
        ratgdoLogger->printMessageLog(ratgdoLogger->logMessageFile, false);
        ratgdoLogger->logMessageFile.close();
    }
}

void logToBuffer_P(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_hook(fmt, args);
    va_end(args);
}

#else
// There is 8KB of RTC memory that can be set to not initialize on restart.
// Data saved here will survive a crash and restart, but will not survive a power interruption.
typedef struct logSaveBuffer
{
    uint32_t wrapped;                                                   // two bytes
    uint32_t head;                                                      // two bytes
    char buffer[LOG_SAVE_BUFFER_SIZE - sizeof(wrapped) - sizeof(head)]; // sized so whole struct is LOG_SAVE_BUFFER_SIZE bytes
} logSaveBuffer;

RTC_NOINIT_ATTR logSaveBuffer rtcRebootLog;
RTC_NOINIT_ATTR logSaveBuffer rtcCrashLog;
RTC_NOINIT_ATTR time_t rebootTime;
RTC_NOINIT_ATTR _millis_t rebootUpTime;
RTC_NOINIT_ATTR time_t crashTime;
RTC_NOINIT_ATTR _millis_t crashUpTime;
RTC_NOINIT_ATTR int32_t crashCount;
RTC_NOINIT_ATTR char reasonString[64];
RTC_NOINIT_ATTR char crashVersion[16];
RTC_NOINIT_ATTR volatile uint32_t resetMagic;
#define RESET_MAGIC 0xDEADBEEF // Thankyou google AI for the suggestion.
const int rtcSize = sizeof(rtcRebootLog) + sizeof(rtcCrashLog) + sizeof(rebootTime) + sizeof(rebootUpTime) + sizeof(crashTime) + sizeof(crashUpTime) +
                    sizeof(crashCount) + sizeof(reasonString) + sizeof(crashVersion) + sizeof(resetMagic);

#define TAKE_MUTEX() xSemaphoreTakeRecursive(logMutex, portMAX_DELAY)
#define GIVE_MUTEX() xSemaphoreGiveRecursive(logMutex)

void panic_handler(arduino_panic_info_t *info, void *arg)
{
    // As precaution... reset UART pins as failing to do this could cause the door to open/close
    gpio_reset_pin(UART_TX_PIN);
    gpio_reset_pin(UART_RX_PIN);

    // crashCount could be negative... indicating that there is a core dump image, but no saved crash log.
    // But now we are saving a crash log, so need to make sure it is positive.
    crashUpTime = _millis();
    crashCount = (crashCount < 0) ? 1 : crashCount + 1;
    crashTime = (clockSet) ? time(NULL) : 0;
    esp_rom_printf("Panic Handler, crash count %d\n", crashCount);
    size_t len = sizeof(rtcCrashLog.buffer);
    uint32_t end = (ratgdoLogger->msgBuffer->head + 1) % sizeof(ratgdoLogger->msgBuffer->buffer); // include the null terminator
    uint32_t start = (sizeof(ratgdoLogger->msgBuffer->buffer) + end - len) % sizeof(ratgdoLogger->msgBuffer->buffer);
    rtcCrashLog.wrapped = 0;
    rtcCrashLog.head = len - 1; // end of the buffer
    if (start >= ratgdoLogger->msgBuffer->head)
    {
        len = (sizeof(ratgdoLogger->msgBuffer->buffer) - start) * ratgdoLogger->msgBuffer->wrapped;
        memcpy(&rtcCrashLog.buffer[0], &ratgdoLogger->msgBuffer->buffer[start], len);
        memcpy(&rtcCrashLog.buffer[len], &ratgdoLogger->msgBuffer->buffer[0], end);
    }
    else
    {
        memcpy(&rtcCrashLog.buffer[0], &ratgdoLogger->msgBuffer->buffer[start], len);
    }
    strlcpy(reasonString, info->reason, sizeof(reasonString));
    strlcpy(crashVersion, AUTO_VERSION, sizeof(crashVersion));
}
#endif

// Constructor for LOG class
LOG::LOG()
{
#ifdef ESP8266
    LittleFS.begin();
    IRAM_START(TAG);
    // IRAM heap is used only for allocating globals, to leave as much regular heap
    // available during operations.  We need to carefully monitor useage so as not
    // to exceed available IRAM.  We can adjust the LOG_BUFFER_SIZE (in log.h) if we
    // need to make more space available for initialization.
    msgBuffer = static_cast<logBuffer *>(malloc(sizeof(logBuffer)));
    lineBuffer = static_cast<char *>(malloc(LINE_BUFFER_SIZE));
    // Open logMessageFile so we don't have to later.
    logMessageFile = (LittleFS.exists(CRASH_LOG_MSG_FILE)) ? LittleFS.open(CRASH_LOG_MSG_FILE, "r+") : LittleFS.open(CRASH_LOG_MSG_FILE, "w+");
    IRAM_END(TAG);
#else
    if (resetMagic != RESET_MAGIC)
    {
        // Reboot after a hard restart (e.g. power cycle). Contents of RTC memory are undefined.
        resetMagic = RESET_MAGIC;
        crashCount = 0;
        crashUpTime = 0;
        crashTime = 0;
        rebootUpTime = 0;
        rebootTime = 0;
    }
    logMutex = xSemaphoreCreateRecursiveMutex();
    msgBuffer = static_cast<logBuffer *>(malloc(sizeof(logBuffer)));
    lineBuffer = static_cast<char *>(malloc(LINE_BUFFER_SIZE));
    set_arduino_panic_handler(panic_handler, NULL);
#endif
    // Zero out the buffer... because if we crash and dump buffer before it fills
    // up, we want blank space not garbage!
    memset(msgBuffer->buffer, 0, sizeof(msgBuffer->buffer));
    msgBuffer->wrapped = 0;
    msgBuffer->head = 0;
}

void LOG::logToBuffer(const char *fmt, va_list args)
{
    TAKE_MUTEX();
    // parse the format string into lineBuffer
    vsnprintf(lineBuffer, LINE_BUFFER_SIZE, fmt, args);
    // If timestamp is wrapped in () and not [] then message is from one of the ESP_LOGx() functions.
    // Convert the milliseconds into HH:MM:SS.mmm so it is easier to read.
    if (strchr(lineBuffer, '(') - lineBuffer == 2)
    {
        char *numptr = &lineBuffer[3];
        char *endptr;
        uint32_t num = strtoll(numptr, &endptr, 10);
        const char *ts = toHHMMSSmmm(num);
        uint32_t timestrlen = strlen(ts);
        int32_t diff = timestrlen - (int32_t)(endptr - numptr);
        int32_t max = LINE_BUFFER_SIZE - ((int32_t)(endptr - lineBuffer) + diff);
        memmove(endptr + diff, endptr, max);
        memcpy(numptr, ts, timestrlen);
        // Make sure we always end in a newline/null... which can get missed if we truncated the log message
        lineBuffer[LINE_BUFFER_SIZE - 2] = '\n';
        lineBuffer[LINE_BUFFER_SIZE - 1] = 0;
    }

    //  print line to the serial port
    SERIAL_PRINT(lineBuffer);

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
        // of the functions we use here log a message.  This is known to happen
        // in NetworkUDP code in error condition... used for SysLog.
        inFn = true;
        // send it to subscribed browsers
        SSEBroadcastState(lineBuffer, LOG_MESSAGE);
        // send it to syslog server
        logToSyslog(lineBuffer);
        inFn = false;
    }
    GIVE_MUTEX();
    return;
}

void LOG::clearCrashLog()
{
#ifdef ESP8266
    saveCrash.clear();
#else
    esp_core_dump_image_erase();
#endif
    crashCount = 0;
}

#ifdef ESP8266
void LOG::printCrashLog(Print &outputDev)
{
    // We save data from crash EEPROM into a temp file so when we send to the
    // browser client we chunk it in smaller pieces.  This improves reliability
    // on slow network links avoiding watchdog timeouts
    if (crashCount > 0)
    {
        constexpr char CRASH_TEMP_FILE[] = "/crash_temp";
        File file = LittleFS.open(CRASH_TEMP_FILE, "w");
        file.truncate(0);
        file.seek(0, fs::SeekSet);
        saveCrash.print(file);
        ratgdoLogger->printSavedLog(logMessageFile, file, false);
        file.close();
        // Open temp file for reading and send it to client
        file = LittleFS.open(CRASH_TEMP_FILE, "r");
        ratgdoLogger->printSavedLog(file, outputDev, true);
        file.close();
    }
    else
    {
        outputDev.print("\n\nNo crash log available.\n");
    }
}

#else
void LOG::printCrashLog(Print &outputDev)
{
    TAKE_MUTEX();
    if (crashCount > 0)
    {
        if (crashTime)
        {
            outputDev.printf("Server boot time: %llu (%s)\n", crashTime - (crashUpTime / 1000), timeString(crashTime - (crashUpTime / 1000)));
            outputDev.printf("Server crash time: %llu (%s)\n", crashTime, timeString(crashTime));
        }
        outputDev.printf("Server uptime: %llu ms (%s)\n", crashUpTime, toHHMMSSmmm((_millis_t)crashUpTime));
        outputDev.printf("Crash reason: %s\n", reasonString);
        outputDev.printf("Firmware version: %s\n\n", crashVersion);
        outputDev.flush();
        // head points to a zero (null terminator of previous log line) which we need to skip.
        size_t start = (rtcCrashLog.head + 1) % sizeof(rtcCrashLog.buffer);
        if (rtcCrashLog.wrapped != 0)
        {
            outputDev.write(&rtcCrashLog.buffer[start], sizeof(rtcCrashLog.buffer) - start);
        }
        outputDev.print(rtcCrashLog.buffer); // assumes null terminated
    }

    if (esp_core_dump_image_check() == ESP_OK)
    {
        esp_core_dump_summary_t *summary = static_cast<esp_core_dump_summary_t *>(malloc(sizeof(esp_core_dump_summary_t)));
        if (summary)
        {
            if (esp_core_dump_get_summary(summary) == ESP_OK)
            {
                if (crashCount <= 0)
                    outputDev.print("No saved message log available.\n");

                outputDev.print("\n\n");
                outputDev.printf("Crash in task: %s, at address: 0x%08lX\n", summary->exc_task, summary->exc_pc);
                outputDev.print("Decode backtrace with this Linux command:\n\n");
                if (crashCount > 0)
                    outputDev.printf("addr2line -p -f -C -e homekit-ratgdo32-v%s.elf \\\n", crashVersion);
                else
                    outputDev.print("addr2line -p -f -C -e firmware.elf \\\n");
                outputDev.print(" -a ");
                for (int i = 0; i < summary->exc_bt_info.depth; i++)
                {
                    outputDev.printf("0x%08lX ", summary->exc_bt_info.bt[i]);
                    if (((i + 1) % 6) == 0 && ((i + 1) < summary->exc_bt_info.depth))
                        outputDev.print("\\\n    ");
                }
                outputDev.print("\n\n");
                outputDev.print("Make sure that the ELF file matches the binary that crashed.\n");
            }
            free(summary);
        }
    }
    else
    {
        outputDev.print("\n\nNo core dump image available.\n");
    }
    GIVE_MUTEX();
}

void LOG::saveMessageLog()
{
    ESP_LOGI(TAG, "Save message log buffer");
    TAKE_MUTEX();
    size_t len = sizeof(rtcRebootLog.buffer);
    uint32_t end = (msgBuffer->head + 1) % sizeof(msgBuffer->buffer); // include the null terminator
    uint32_t start = (sizeof(msgBuffer->buffer) + end - len) % sizeof(msgBuffer->buffer);
    rtcRebootLog.wrapped = 0;
    rtcRebootLog.head = len - 1; // end of the buffer
    if (start >= msgBuffer->head)
    {
        len = (sizeof(msgBuffer->buffer) - start) * msgBuffer->wrapped;
        memcpy(&rtcRebootLog.buffer[0], &msgBuffer->buffer[start], len);
        memcpy(&rtcRebootLog.buffer[len], &msgBuffer->buffer[0], end);
    }
    else
    {
        memcpy(&rtcRebootLog.buffer[0], &msgBuffer->buffer[start], len);
    }
    rebootTime = (clockSet) ? time(NULL) : 0;
    rebootUpTime = _millis();
    GIVE_MUTEX();
}
#endif

#ifdef ESP8266
void LOG::printSavedLog(File file, Print &outputDev, bool slow)
{
    SERIAL_PRINT("Send saved file.");
    if (file && file.size() > 0)
    {
        size_t num = LINE_BUFFER_SIZE;
        size_t count = 0;
        file.seek(0, fs::SeekSet);
        while (num == LINE_BUFFER_SIZE)
        {
            num = file.read(reinterpret_cast<uint8_t *>(lineBuffer), LINE_BUFFER_SIZE);
            // Progress dot dot dot
            SERIAL_PRINT(".");
            YIELD();
            outputDev.write(lineBuffer, num);
            if (slow && (count += num) > TCP_SND_BUF / 2)
            {
                // Don't risk filling the TCP Send Buffer
                // wait for it to empty with a flush.
                count = num;
                outputDev.flush();
            }
        }
        outputDev.print("\n");
        outputDev.flush();
    }
    SERIAL_PRINT("\n");
}
#endif

void LOG::printSavedLog(Print &outputDev)
{
#ifdef ESP8266
    return printSavedLog(logMessageFile, outputDev, true);
#else
    if (rebootUpTime != 0)
    {
        if (rebootTime)
        {
            outputDev.printf("Server boot time: %llu (%s)\n", rebootTime - (rebootUpTime / 1000), timeString(rebootTime - (rebootUpTime / 1000)));
            outputDev.printf("Server log time: %llu (%s)\n", rebootTime, timeString(rebootTime));
        }
        outputDev.printf("Server uptime: %llu ms (%s)\n", rebootUpTime, toHHMMSSmmm((_millis_t)rebootUpTime));
        outputDev.println("Firmware version: " AUTO_VERSION);
        outputDev.flush();
        outputDev.print(rtcRebootLog.buffer);
    }
    else
    {
        outputDev.print("\nNo saved log available\n");
    }
#endif
}

void LOG::printMessageLog(Print &outputDev, bool slow)
{
    TAKE_MUTEX();
    if (clockSet)
    {
        time_t now = time(NULL);
        outputDev.printf_P(PSTR("Server boot time: %llu (%s)\n"), lastRebootAt, timeString(lastRebootAt));
        outputDev.printf_P(PSTR("Server log time: %llu (%s)\n"), now, timeString(now));
    }
    int64_t upTime = (int64_t)_millis();
    outputDev.printf_P(PSTR("Server uptime: %llu ms (%s)\n"), upTime, toHHMMSSmmm((_millis_t)upTime));
    outputDev.println("Firmware version: " AUTO_VERSION);
    outputDev.printf_P(PSTR("Free heap: %d\n"), free_heap);
    outputDev.printf_P(PSTR("Minimum heap: %d\n"), min_heap);
    outputDev.flush();

    if (msgBuffer)
    {
        // +1 head points to a zero (null terminator of previous log line) which we need to skip.
        // +3 want to round up to multiple of 4
        size_t start = ((msgBuffer->head + 1 + 3) & ~0x03) % sizeof(msgBuffer->buffer);
        size_t len = sizeof(msgBuffer->buffer) - start;
        // On slow output devices (e.g. network), chunk up to protect against buffer overflow
        size_t CHUNK = (slow) ? TCP_SND_BUF / 2 : sizeof(msgBuffer->buffer);
        size_t chunk;
        SERIAL_PRINT("Send message log.");
        if (msgBuffer->wrapped != 0)
        {
            while (len)
            {
                chunk = std::min(len, CHUNK);
                // Progress dot dot dot
                SERIAL_PRINT(".");
                YIELD();
                outputDev.write(&msgBuffer->buffer[start], chunk);
                if (slow)
                    outputDev.flush();
                len -= chunk;
                start += chunk;
            }
        }
        start = 0;
        len = strlen(msgBuffer->buffer); // must be null terminated
        while (len)
        {
            chunk = std::min(len, CHUNK);
            // Progress dot dot dot
            SERIAL_PRINT(".");
            YIELD();
            outputDev.write(&msgBuffer->buffer[start], chunk);
            outputDev.flush();
            len -= chunk;
            start += chunk;
        }
        SERIAL_PRINT("\n");
    }
    GIVE_MUTEX();
}

/****************************************************************************
 * Syslog
 */
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

void logToSyslog(char *message)
{
    if (!syslogEn || !WiFi.isConnected())
        return;

    uint8_t PRI = syslogFacility * 8;
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

    // Replace newline with null terminator
    strtok(message, "\n");
    strtok(message, "\r");

    syslog.beginPacket(syslogIP, syslogPort);
    // Use RFC5424 Format
    syslog.print("<");
    syslog.print(PRI);
    syslog.print(">1 ");
#if defined(USE_NTP_TIMESTAMP)
    syslog.print((enableNTP && clockSet) ? timeString(0, true) : SYSLOG_NIL);
#else
    syslog.print(SYSLOG_NIL); // Time - let the syslog server insert time
#endif
    syslog.print(" ");
    syslog.print(device_name_rfc952); // hostname
#ifdef ESP8266
    syslog.print(" ratgdo"); // application name
#else
    syslog.print(" ratgdo32"); // application name
#endif
    syslog.print(" " SYSLOG_NIL // process ID
                 " " SYSLOG_NIL // message ID
                 " " SYSLOG_NIL // structured data
#ifdef USE_UTF8_BOM
                 " " SYSLOG_BOM); // BOM - indicates UTF-8 encoding
#else
                 " "); // No BOM
#endif
    syslog.print(message); // message
    syslog.endPacket();
}
