// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include "utilities.h"
#include "log.h"
#include "LittleFS.h"
#include "comms.h"
#include <ESP8266WiFi.h>

#ifdef LEGACY_SETTINGS_MIGRATION
// Filenames for legacy user configuation, replaced by single file.
const char staticIPfile[] = "static_ip_file";
const char IPaddressFile[] = "ip_address_file";
const char IPnetmaskFile[] = "ip_netmask_file";
const char IPgatewayFile[] = "ip_gateway_file";
const char IPnameserverFile[] = "ip_nameserver_file";
const char wifiPowerFile[] = "wifiPower";
const char wifiPhyModeFile[] = "wifiPhyMode";
const char wifiSettingsChangedFile[] = "wifiSettingsChanged";
const char device_name_file[] = "device_name";
const char system_reboot_timer[] = "system_reboot_timer";
const char gdoSecurityTypeFile[] = "gdo_security";
const char TTCdelay_file[] = "TTC_delay";
const char username_file[] = "www_username";
const char credentials_file[] = "www_credentials";
const char www_pw_required_file[] = "www_pw_required_file";
const char ledIdleStateFile[] = "led_idle_state";
const char motionTriggersFile[] = "motion_triggers";
const char softAPmodeFile[] = "soft_ap_mode";
const char lastDoorUpdateFile[] = "last_door_update";
const char enableNTPFile[] = "enable_ntp_client";
#endif

// What trigger motion...
motionTriggersUnion motionTriggers = {{0}};
// Control booting into soft access point mode
bool softAPmode = false;
// Realm for MD5 credential hashing
const char www_realm[] = "RATGDO Login Required";

// Consolodate all config settings into one file.
const char userConfigFile[] = "user_config";
userConfig_t *userConfig = NULL;

// Controls whether to log to syslog server
bool syslogEn = false;

#ifdef NTP_CLIENT
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
bool enableNTP = false;
unsigned long lastRebootAt = 0;
int32_t savedDoorUpdateAt = 0;

char *timeString(time_t reqTime)
{
    // declare static so we don't use stack space
    static char tBuffer[32];
    static time_t tTime = 0;
    static tm *tmTime = NULL;
    tBuffer[0] = 0;
    tTime = ((reqTime == 0) && timeClient.isTimeSet()) ? timeClient.getEpochTime() : reqTime;
    if (tTime != 0)
    {
        tmTime = gmtime(&tTime);
        strftime(tBuffer, sizeof(tBuffer), "%Y-%m-%dT%H:%M:%S.000Z", tmTime);
    }
    return tBuffer;
}
#endif

char *make_rfc952(char *dest, const char *src, int size)
{
    // Make device name RFC952 complient (simple, just checking for the basics)
    // RFC952 says max len of 24, [a-z][A-Z][0-9][-.] and no dash or period in last char.
    int i = 0;
    while (i <= min(24, size - 1) && src[i] != 0)
    {
        dest[i] = (isspace((unsigned char)src[i])) ? '-' : src[i];
        i++;
    }
    // remove dashes and periods from end of name
    while (i > 0 && (dest[i - 1] == '-' || dest[i - 1] == '.'))
    {
        dest[--i] = 0;
    }
    // null terminate string
    dest[min(i, min(24, size - 1))] = 0;
    return dest;
}

void load_all_config_settings()
{
    snprintf(device_name, DEVICE_NAME_SIZE, "Garage Door %06X", ESP.getChipId());
    RINFO("=== Load all config settings for %s", device_name);
    if (!userConfig)
    {
        IRAM_START
        // IRAM heap is used only for allocating globals, to leave as much regular heap
        // available during operations.  We need to carefully monitor useage so as not
        // to exceed available IRAM.  We can adjust the LOG_BUFFER_SIZE (in log.h) if we
        // need to make more space available for initialization.
        userConfig = (userConfig_t *)malloc(sizeof(userConfig_t));
        IRAM_END("User config buffer allocated");
    }
    // Initialize with defaults.
    *userConfig = (userConfig_t){}; // Initializes with defaults defined in typedef.
    strlcpy(userConfig->deviceName, device_name, sizeof(userConfig->deviceName));
    if (!read_config_from_file())
    {
#ifdef LEGACY_SETTINGS_MIGRATION
        // Config file does not exist, load all settings from legacy files (if they exist!)
        // SOMETIME in the future we can delete this whole block of code, we only need it for migraion from old files
        RINFO("Loading user configuration from legacy files");
        read_string_from_file(device_name_file, userConfig->deviceName, device_name, sizeof(userConfig->deviceName));
        delete_file(device_name_file);
        Serial.print(".");
        userConfig->wifiSettingsChanged = read_int_from_file(wifiSettingsChangedFile, 1) != 0;
        delete_file(wifiSettingsChangedFile);
        Serial.print(".");
        userConfig->wifiPower = read_int_from_file(wifiPowerFile, userConfig->wifiPower);
        delete_file(wifiPowerFile);
        Serial.print(".");
        userConfig->wifiPhyMode = read_int_from_file(wifiPhyModeFile);
        delete_file(wifiPhyModeFile);
        Serial.print(".");
        userConfig->staticIP = read_int_from_file(staticIPfile) != 0;
        delete_file(staticIPfile);
        Serial.print(".");
        if (userConfig->staticIP)
        {
            read_string_from_file(IPaddressFile, userConfig->IPaddress, userConfig->IPaddress, sizeof(userConfig->IPaddress));
            Serial.print(".");
            read_string_from_file(IPnetmaskFile, userConfig->IPnetmask, userConfig->IPnetmask, sizeof(userConfig->IPnetmask));
            Serial.print(".");
            read_string_from_file(IPgatewayFile, userConfig->IPgateway, userConfig->IPgateway, sizeof(userConfig->IPgateway));
            Serial.print(".");
            read_string_from_file(IPnameserverFile, userConfig->IPnameserver, userConfig->IPnameserver, sizeof(userConfig->IPnameserver));
            Serial.print(".");
        }
        delete_file(IPaddressFile);
        delete_file(IPnetmaskFile);
        delete_file(IPgatewayFile);
        delete_file(IPnameserverFile);
        userConfig->wwwPWrequired = read_int_from_file(www_pw_required_file) != 0;
        delete_file(www_pw_required_file);
        Serial.print(".");
        read_string_from_file(username_file, userConfig->wwwUsername, userConfig->wwwUsername, sizeof(userConfig->wwwUsername));
        delete_file(username_file);
        Serial.print(".");
        read_string_from_file(credentials_file, userConfig->wwwCredentials, userConfig->wwwCredentials, sizeof(userConfig->wwwCredentials));
        delete_file(credentials_file);
        Serial.print(".");
        userConfig->gdoSecurityType = read_int_from_file(gdoSecurityTypeFile, userConfig->gdoSecurityType);
        delete_file(gdoSecurityTypeFile);
        Serial.print(".");
        userConfig->TTCdelay = read_int_from_file(TTCdelay_file);
        delete_file(TTCdelay_file);
        Serial.print(".");
        userConfig->rebootSeconds = read_int_from_file(system_reboot_timer);
        delete_file(system_reboot_timer);
        Serial.print(".");
        userConfig->ledIdleState = read_int_from_file(ledIdleStateFile, userConfig->ledIdleState);
        delete_file(ledIdleStateFile);
        Serial.print(".");
        userConfig->motionTriggers = read_int_from_file(motionTriggersFile);
        delete_file(motionTriggersFile);
        Serial.print(".");
#ifdef NTP_CLIENT
        userConfig->enableNTP = read_int_from_file(enableNTPFile) != 0;
        delete_file(enableNTPFile);
        Serial.print(".");
        userConfig->doorUpdateAt = read_int_from_file(lastDoorUpdateFile);
        delete_file(lastDoorUpdateFile);
        Serial.print(".");
#endif
        userConfig->softAPmode = read_int_from_file(softAPmodeFile) != 0;
        delete_file(softAPmodeFile);
        Serial.print("\n");
// Save to file so we never have to read from individual files again.
#else
        RINFO("No settings saved, using factory defaults.");
#endif
        write_config_to_file();
    }

    // Check we have a legal device name...
    make_rfc952(device_name_rfc952, userConfig->deviceName, sizeof(device_name_rfc952));
    if (strlen(device_name_rfc952) == 0)
    {
        // cannot have a empty device name, reset to default...
        strlcpy(userConfig->deviceName, device_name, sizeof(userConfig->deviceName));
        make_rfc952(device_name_rfc952, userConfig->deviceName, sizeof(device_name_rfc952));
    }
    else
    {
        // device name okay, copy it to our global
        strlcpy(device_name, userConfig->deviceName, sizeof(device_name));
    }

    // Set rest of globals...
    led.setIdleState((uint8_t)userConfig->ledIdleState);
    motionTriggers.asInt = userConfig->motionTriggers;
    softAPmode = userConfig->softAPmode;
#ifdef NTP_CLIENT
    // Only enable NTP client if not in soft AP mode.
    enableNTP = !softAPmode && userConfig->enableNTP;
#endif
    syslogEn = userConfig->syslogEn;
    // Now log what we have loaded
    RINFO("RFC952 compliant device hostname: %s", device_name_rfc952);
    RINFO("User Configuration...");
    RINFO("   deviceName:          %s", userConfig->deviceName);
    RINFO("   wifiSettingsChanged: %s", userConfig->wifiSettingsChanged ? "true" : "false");
    RINFO("   wifiPower:           %d", userConfig->wifiPower);
    RINFO("   wifiPhyMode:         %d", userConfig->wifiPhyMode);
    RINFO("   staticIP:            %s", userConfig->staticIP ? "true" : "false");
    RINFO("   IPaddress:           %s", userConfig->IPaddress);
    RINFO("   IPnetmask:           %s", userConfig->IPnetmask);
    RINFO("   IPgateway:           %s", userConfig->IPgateway);
    RINFO("   IPnameserver:        %s", userConfig->IPnameserver);
    RINFO("   wwwPWrequired:       %s", userConfig->wwwPWrequired ? "true" : "false");
    RINFO("   wwwUsername:         %s", userConfig->wwwUsername);
    RINFO("   wwwCredentials:      %s", userConfig->wwwCredentials);
    RINFO("   gdoSecurityType:     %d", userConfig->gdoSecurityType);
    RINFO("   TTCdelay:            %d", userConfig->TTCdelay);
    RINFO("   rebootSeconds:       %d", userConfig->rebootSeconds);
    RINFO("   ledIdleState:        %d", userConfig->ledIdleState);
    RINFO("   motionTriggers:      %d", userConfig->motionTriggers);
#ifdef NTP_CLIENT
    RINFO("   enableNTP:           %s", userConfig->enableNTP ? "true" : "false");
    RINFO("   doorUpdateAt:        %d", userConfig->doorUpdateAt);
#endif
    RINFO("   softAPmode:          %s", userConfig->softAPmode ? "true" : "false");
    RINFO("   syslogEn:            %s", userConfig->syslogEn ? "true" : "false");
    RINFO("   syslogIP:            %s", userConfig->syslogIP);
}

void sync_and_restart()
{
#ifdef NTP_CLIENT
    if (enableNTP)
    {
        timeClient.end();
    }
#endif
    if (softAPmode)
    {
        // reset so next reboot will be standard station mode
        userConfig->softAPmode = false;
        write_config_to_file();
    }
    else
    {
        // In soft AP mode we never initialized garage door comms, so don't save rolling code.
        save_rolling_code();
    }
    // RINFO("checkFlashCRC: %s", ESP.checkFlashCRC() ? "true" : "false");
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    // Save current logs in case needed for future analysis
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
    if (file)
    {
        value = file.parseInt();
        file.close();
    }
    return value;
}

void write_int_to_file(const char *filename, uint32_t value)
{
    File file = LittleFS.open(filename, "w");
    RINFO("writing %lu to file %s", value, filename);
    file.print(value);
    file.close();
}

char *read_string_from_file(const char *filename, const char *defaultValue, char *buffer, int bufsize)
{
    File file = LittleFS.open(filename, "r");
    strlcpy(buffer, (file) ? file.readString().c_str() : defaultValue, bufsize);
    if (file)
        file.close();
    return buffer;
}

void write_string_to_file(const char *filename, const char *value)
{
    File file = LittleFS.open(filename, "w");
    RINFO("Writing string to file: %s", value);
    file.print(value);
    file.close();
}

void delete_file(const char *filename)
{
    LittleFS.remove(filename);
}

void write_config_to_file()
{
    RINFO("Writing user configuration to file: %s", userConfigFile);
    File file = LittleFS.open(userConfigFile, "w");
    file.printf_P(PSTR("deviceName,,%s\n"), userConfig->deviceName);
    file.printf_P(PSTR("wifiSettingsChanged,,%s\n"), userConfig->wifiSettingsChanged ? "true" : "false");
    file.printf_P(PSTR("wifiPower,,%d\n"), userConfig->wifiPower);
    file.printf_P(PSTR("wifiPhyMode,,%d\n"), userConfig->wifiPhyMode);
    file.printf_P(PSTR("staticIP,,%s\n"), userConfig->staticIP ? "true" : "false");
    file.printf_P(PSTR("IPaddress,,%s\n"), userConfig->IPaddress);
    file.printf_P(PSTR("IPnetmask,,%s\n"), userConfig->IPnetmask);
    file.printf_P(PSTR("IPgateway,,%s\n"), userConfig->IPgateway);
    file.printf_P(PSTR("IPnameserver,,%s\n"), userConfig->IPnameserver);
    file.printf_P(PSTR("wwwPWrequired,,%s\n"), userConfig->wwwPWrequired ? "true" : "false");
    file.printf_P(PSTR("wwwUsername,,%s\n"), userConfig->wwwUsername);
    file.printf_P(PSTR("wwwCredentials,,%s\n"), userConfig->wwwCredentials);
    file.printf_P(PSTR("gdoSecurityType,,%d\n"), userConfig->gdoSecurityType);
    file.printf_P(PSTR("TTCdelay,,%d\n"), userConfig->TTCdelay);
    file.printf_P(PSTR("rebootSeconds,,%d\n"), userConfig->rebootSeconds);
    file.printf_P(PSTR("ledIdleState,,%d\n"), userConfig->ledIdleState);
    file.printf_P(PSTR("motionTriggers,,%d\n"), userConfig->motionTriggers);
#ifdef NTP_CLIENT
    file.printf_P(PSTR("enableNTP,,%s\n"), userConfig->enableNTP ? "true" : "false");
    file.printf_P(PSTR("doorUpdateAt,,%d\n"), userConfig->doorUpdateAt);
#endif
    file.printf_P(PSTR("softAPmode,,%s\n"), userConfig->softAPmode ? "true" : "false");
    file.printf_P(PSTR("syslogEn,,%s\n"), userConfig->syslogEn ? "true" : "false");
    file.printf_P(PSTR("syslogIP,,%s\n"), userConfig->syslogIP);
    file.close();
}

bool read_config_from_file()
{
    RINFO("Read user configuration from file: %s", userConfigFile);
    File file = LittleFS.open(userConfigFile, "r");
    if (!file)
        return false;
    while (file.available())
    {
        String line = file.readStringUntil('\n');
        const char *key = line.c_str();
        char *type = strchr(key, ',');
        *type++ = 0;
        char *value = strchr(type, ',');
        *value++ = 0;

        if (!userConfig)
            return false;

        if (!strcmp(key, "deviceName"))
        {
            strlcpy(userConfig->deviceName, value, sizeof(userConfig->deviceName));
        }
        else if (!strcmp(key, "wifiSettingsChanged"))
        {
            userConfig->wifiSettingsChanged = !strcmp(value, "true");
        }
        else if (!strcmp(key, "wifiPower"))
        {
            userConfig->wifiPower = atoi(value);
        }
        else if (!strcmp(key, "wifiPhyMode"))
        {
            userConfig->wifiPhyMode = atoi(value);
        }
        else if (!strcmp(key, "staticIP"))
        {
            userConfig->staticIP = !strcmp(value, "true");
        }
        else if (!strcmp(key, "IPaddress"))
        {
            strlcpy(userConfig->IPaddress, value, sizeof(userConfig->IPaddress));
        }
        else if (!strcmp(key, "IPnetmask"))
        {
            strlcpy(userConfig->IPnetmask, value, sizeof(userConfig->IPnetmask));
        }
        else if (!strcmp(key, "IPgateway"))
        {
            strlcpy(userConfig->IPgateway, value, sizeof(userConfig->IPgateway));
        }
        else if (!strcmp(key, "IPnameserver"))
        {
            strlcpy(userConfig->IPnameserver, value, sizeof(userConfig->IPnameserver));
        }
        else if (!strcmp(key, "wwwPWrequired"))
        {
            userConfig->wwwPWrequired = !strcmp(value, "true");
        }
        else if (!strcmp_P(key, PSTR("wwwUsername")))
        {
            strlcpy(userConfig->wwwUsername, value, sizeof(userConfig->wwwUsername));
        }
        else if (!strcmp(key, "wwwCredentials"))
        {
            strlcpy(userConfig->wwwCredentials, value, sizeof(userConfig->wwwCredentials));
        }
        else if (!strcmp(key, "gdoSecurityType"))
        {
            userConfig->gdoSecurityType = atoi(value);
        }
        else if (!strcmp(key, "TTCdelay"))
        {
            userConfig->TTCdelay = atoi(value);
        }
        else if (!strcmp(key, "rebootSeconds"))
        {
            userConfig->rebootSeconds = atoi(value);
        }
        else if (!strcmp(key, "ledIdleState"))
        {
            userConfig->ledIdleState = atoi(value);
        }
        else if (!strcmp(key, "motionTriggers"))
        {
            userConfig->motionTriggers = atoi(value);
        }
#ifdef NTP_CLIENT
        else if (!strcmp(key, "enableNTP"))
        {
            userConfig->enableNTP = !strcmp(value, "true");
        }
        else if (!strcmp(key, "doorUpdateAt"))
        {
            userConfig->doorUpdateAt = 0;
        }
#endif
        else if (!strcmp(key, "softAPmode"))
        {
            userConfig->softAPmode = !strcmp(value, "true");
        }
        else if (!strcmp(key, "syslogEn"))
        {
            userConfig->syslogEn = !strcmp(value, "true");
        }
        else if (!strcmp(key, "syslogIP"))
        {
            strlcpy(userConfig->syslogIP, value, sizeof(userConfig->syslogIP));
        }
    }
    file.close();
    return true;
}