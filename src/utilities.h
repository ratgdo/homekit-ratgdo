#ifndef _UTILITIES_H
#define _UTILITIES_H
#include <stdint.h>
#include <ESP8266WiFi.h>
#include "homekit_decl.h"
#include "ratgdo.h"

#ifdef NTP_CLIENT
#include <WiFiUdp.h>
#include <NTPClient.h>
extern NTPClient timeClient;
extern unsigned long lastRebootAt;
extern char *timeString(time_t reqTime = 0);
extern bool enableNTP;
#endif

// Controls whether to log to syslog server
extern bool syslogEn;

// Controls soft Access Point mode.
extern bool softAPmode;

// Password and credential management for HTTP server...
extern const char www_realm[];

// struct to hold user configuration settings.
#define IP_ADDRESS_SIZE 16
typedef struct
{
    char deviceName[DEVICE_NAME_SIZE];
    bool wifiSettingsChanged = true;
    int wifiPower = 20;
    int wifiPhyMode = 0;
    bool staticIP = false;
    char IPaddress[IP_ADDRESS_SIZE] = "0.0.0.0";
    char IPnetmask[IP_ADDRESS_SIZE] = "0.0.0.0";
    char IPgateway[IP_ADDRESS_SIZE] = "0.0.0.0";
    char IPnameserver[IP_ADDRESS_SIZE] = "0.0.0.0";
    bool wwwPWrequired = false;
    char wwwUsername[32] = "admin";
    // Credentials are MD5 Hash... server.credentialHash(username, realm, "password");
    char wwwCredentials[36] = "10d3c00fa1e09696601ef113b99f8a87";
    int gdoSecurityType = 2;
    int TTCdelay = 0;
    int rebootSeconds = 0;
    int ledIdleState = LOW;
    int motionTriggers = 0;
#ifdef NTP_CLIENT
    bool enableNTP = false;
    int doorUpdateAt = 0;
#endif
    bool softAPmode = false;
    bool syslogEn = false;
    char syslogIP[IP_ADDRESS_SIZE] = "0.0.0.0";
} userConfig_t;
extern userConfig_t *userConfig;

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
