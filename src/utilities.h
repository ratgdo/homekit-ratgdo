#ifndef _UTILITIES_H
#define _UTILITIES_H
#include <stdint.h>
#include <ESP8266WiFi.h>
#include "homekit_decl.h"
#include "ratgdo.h"
#include <map>
#include <string>
#include <any>

#ifdef NTP_CLIENT
#include <WiFiUdp.h>
#include <NTPClient.h>
extern NTPClient timeClient;
extern unsigned long lastRebootAt;
extern char *timeString(time_t reqTime = 0);
extern bool enableNTP;
#endif

// Controls soft Access Point mode.
extern bool softAPmode;

// Password and credential management for HTTP server...
extern const char www_realm[];

// map and macros to hold, get and set user configuration settings.
enum ConfigType : int
{
    BOOL = 1,
    INT = 2,
    STRING = 3
};
extern std::map<std::string, std::pair<int, std::any>> userConfig;

#define GET_CONFIG_BOOL(key) (std::any_cast<bool>(userConfig[key].second))
#define GET_CONFIG_INT(key) (std::any_cast<int>(userConfig[key].second))
#define GET_CONFIG_STRING(key) (std::any_cast<std::string>(userConfig[key].second))

#define SET_CONFIG_BOOL(key, value) (userConfig[key] = {ConfigType::BOOL, static_cast<bool>(value)})
#define SET_CONFIG_INT(key, value) (userConfig[key] = {ConfigType::INT, static_cast<int>(value)})
#define SET_CONFIG_STRING(key, value) (userConfig[key] = {ConfigType::STRING, std::string(value)})

// Bitset that identifies what will trigger the motion sensor
typedef struct
{
    uint8_t motion : 1;
    uint8_t obstruction : 1;
    uint8_t lightKey : 1;
    uint8_t doorKey : 1;
    uint8_t lockKey : 1;
    uint8_t undef : 3;
} motionTriggerBitset;
typedef union
{
    motionTriggerBitset bit;
    uint8_t asInt;
} motionTriggersUnion;
extern motionTriggersUnion motionTriggers;

// Function declarations
void load_all_config_settings();
void sync_and_restart();

uint32_t read_int_from_file(const char *filename, uint32_t defaultValue = 0);
void write_int_to_file(const char *filename, uint32_t value);

char *read_string_from_file(const char *filename, const char *defaultValue, char *buffer, int bufsize);
void write_string_to_file(const char *filename, const char *value);

bool read_config_from_file();
void write_config_to_file();

void delete_file(const char *filename);

#endif
