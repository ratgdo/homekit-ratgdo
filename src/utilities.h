#ifndef _UTILITIES_H
#define _UTILITIES_H
#include <stdint.h>
#include <ESP8266WiFi.h>
#include "homekit_decl.h"
#include "ratgdo.h"

// For saving DHCP and static IP settings
#define IP_ADDRESS_SIZE 16
extern bool staticIP;
extern char IPaddress[IP_ADDRESS_SIZE];
extern char IPnetmask[IP_ADDRESS_SIZE];
extern char IPgateway[IP_ADDRESS_SIZE];
extern const char staticIPfile[];
extern const char IPaddressFile[];
extern const char IPnetmaskFile[];
extern const char IPgatewayFile[];

// For WiFi physical connection...
extern uint16_t wifiPower;
extern const char wifiPowerFile[];
extern WiFiPhyMode_t wifiPhyMode;
extern const char wifiPhyModeFile[];
extern bool wifiSettingsChanged;
extern const char wifiSettingsChangedFile[];

// device_name is defined in homekit_decl.c
extern const char device_name_file[];

#define REBOOT_SECONDS (0)
extern uint32_t rebootSeconds;
extern const char system_reboot_timer[];

extern uint8_t gdoSecurityType;
extern const char gdoSecurityTypeFile[];
extern uint8_t TTCdelay;
extern const char TTCdelay_file[];

// Password and credential management for HTTP server...
extern char www_username[32];
extern const char username_file[];
extern const char www_password[];
extern const char www_realm[];
extern char www_credentials[48];
extern const char credentials_file[];
extern bool passwordReq;
extern const char www_pw_required_file[];

// What is LED idle state (on or off);
// value is defined in ratgdo.cpp
extern const char ledIdleStateFile[];

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
extern const char motionTriggersFile[];

void load_all_config_settings();
void sync_and_restart();
uint32_t read_int_from_file(const char *filename, uint32_t defaultValue = 0);
void write_int_to_file(const char *filename, uint32_t value);

char *read_string_from_file(const char *filename, const char *defaultValue, char *buffer, int bufsize);
void write_string_to_file(const char *filename, const char *value);

void *read_data_from_file(const char *filename, void *buffer, int bufsize);
void write_data_to_file(const char *filename, const void *buffer, const int bufsize);

void delete_file(const char *filename);

#endif