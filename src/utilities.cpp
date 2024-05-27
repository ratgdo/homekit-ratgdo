// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include "utilities.h"
#include "log.h"
#include "LittleFS.h"
#include "comms.h"
#include <ESP8266WiFi.h>

void sync_and_restart()
{
    RINFO("checkFlashCRC: %s", ESP.checkFlashCRC() ? "true" : "false");
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    save_rolling_code();
    File file = LittleFS.open(REBOOT_LOG_MSG_FILE, "w");
    printMessageLog(file);
    file.close();
    LittleFS.end();
    delay(100);
    ESP.restart();
}

uint32_t read_int_from_file(const char *filename, uint32_t defaultValue)
{
    // set to default value
    uint32_t value = defaultValue;
    File file = LittleFS.open(filename, "r");
    if (!file)
    {
        RINFO("%s doesn't exist. creating...", filename);
        write_int_to_file(filename, &value);
    }
    else
    {
        value = file.parseInt();
        file.close();
    }
    return value;
}

void write_int_to_file(const char *filename, uint32_t *value)
{
    File file = LittleFS.open(filename, "w");
    RINFO("writing %lu to file %s", *value, filename);
    file.print(*value);
    file.close();
}

char *read_string_from_file(const char *filename, const char *defaultValue, char *buffer, int bufsize)
{
    File file = LittleFS.open(filename, "r");
    if (!file)
    {
        RINFO("%s doesn't exist. creating...", filename);
        strlcpy(buffer, defaultValue, bufsize);
        write_string_to_file(filename, buffer);
    }
    else
    {
        strlcpy(buffer, file.readString().c_str(), bufsize);
        file.close();
    }
    return buffer;
}

void write_string_to_file(const char *filename, const char *value)
{
    File file = LittleFS.open(filename, "w");
    RINFO("Writing string to file: %s", value);
    file.print(value);
    file.close();
}

void *read_data_from_file(const char *filename, void *buffer, int bufsize)
{
    File file = LittleFS.open(filename, "r");
    if (!file)
        return NULL;
    // strlcpy(buffer, file.readString().c_str(), bufsize);
    file.read((uint8_t *)buffer, bufsize);
    file.close();
    return buffer;
}

void write_data_to_file(const char *filename, const void *buffer, const int bufsize)
{
    File file = LittleFS.open(filename, "w");
    file.write((const uint8_t *)buffer, bufsize);
    file.close();
}

void delete_file(const char *filename)
{
    LittleFS.remove(filename);
}