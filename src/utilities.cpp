// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include "utilities.h"
#include "log.h"
#include "LittleFS.h"
#include "comms.h"
#include <ESP8266WiFi.h>

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
const char syslogfile[] = "syslog_file";

// What trigger motion...
motionTriggersUnion motionTriggers = {{0}};
// Control booting into soft access point mode
bool softAPmode = false;
// Realm for MD5 credential hashing
const char www_realm[] = "RATGDO Login Required";
// Temporary buffer for read_string_from_file
#define STRING_BUF_SIZE 64
char strBuffer[STRING_BUF_SIZE] = "";

// Consolodate all config settings into one file.
const char userConfigFile[] = "user_config";
std::map<std::string, std::pair<int, std::any>> userConfig = {
    {"deviceName", {ConfigType::STRING, std::string("Garage Door")}},
    {"wifiSettingsChanged", {ConfigType::BOOL, true}},
    {"wifiPower", {ConfigType::INT, 20}},
    {"wifiPhyMode", {ConfigType::INT, 0}},
    {"staticIP", {ConfigType::BOOL, false}},
    {"IPaddress", {ConfigType::STRING, std::string("0.0.0.0")}},
    {"IPnetmask", {ConfigType::STRING, std::string("0.0.0.0")}},
    {"IPgateway", {ConfigType::STRING, std::string("0.0.0.0")}},
    {"IPnameserver", {ConfigType::STRING, std::string("0.0.0.0")}},
    {"wwwPWrequired", {ConfigType::BOOL, false}},
    {"wwwUsername", {ConfigType::STRING, std::string("admin")}},
    // Credentials are MD5 Hash... server.credentialHash(username, realm, "password");
    {"wwwCredentials", {ConfigType::STRING, std::string("10d3c00fa1e09696601ef113b99f8a87")}},
    {"gdoSecurityType", {ConfigType::INT, 2}},
    {"TTCdelay", {ConfigType::INT, 0}},
    {"rebootSeconds", {ConfigType::INT, 0}},
    {"ledIdleState", {ConfigType::INT, LOW}},
    {"motionTriggers", {ConfigType::INT, 0}},
#ifdef NTP_CLIENT
    {"enableNTP", {ConfigType::BOOL, false}},
    {"doorUpdateAt", {ConfigType::STRING, std::string("")}},
#endif
    {"softAPmode", {ConfigType::BOOL, false}},
    {"syslogEn", {ConfigType::BOOL, false}},
    {"syslogIP", {ConfigType::STRING, std::string("0.0.0.0")}},
};

bool syslogEn = false;

#ifdef NTP_CLIENT
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
bool enableNTP = false;
const char enableNTPFile[] = "enable_ntp_client";
unsigned long lastRebootAt = 0;
const char lastDoorUpdateFile[] = "last_door_update";
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
    while (dest[i - 1] == '-' || dest[i - 1] == '.')
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
    if (!read_config_from_file())
    {
        // Config file does not exist, load all settings from legacy files (if they exist!)
        RINFO("Loading user configuration from legacy files");
        SET_CONFIG_STRING("deviceName", read_string_from_file(device_name_file, device_name, strBuffer, sizeof(strBuffer)));
        Serial.print(".");
        SET_CONFIG_BOOL("wifiSettingsChanged", read_int_from_file(wifiSettingsChangedFile, 1) != 0);
        Serial.print(".");
        SET_CONFIG_INT("wifiPower", read_int_from_file(wifiPowerFile, GET_CONFIG_INT("wifiPower")));
        Serial.print(".");
        SET_CONFIG_INT("wifiPhyMode", read_int_from_file(wifiPhyModeFile));
        Serial.print(".");
        SET_CONFIG_BOOL("staticIP", read_int_from_file(staticIPfile) != 0);
        Serial.print(".");
        if (GET_CONFIG_BOOL("staticIP"))
        {
            SET_CONFIG_STRING("IPaddress", read_string_from_file(IPaddressFile, GET_CONFIG_STRING("IPaddress").c_str(), strBuffer, sizeof(strBuffer)));
            Serial.print(".");
            SET_CONFIG_STRING("IPnetmask", read_string_from_file(IPnetmaskFile, GET_CONFIG_STRING("IPnetmask").c_str(), strBuffer, sizeof(strBuffer)));
            Serial.print(".");
            SET_CONFIG_STRING("IPgateway", read_string_from_file(IPgatewayFile, GET_CONFIG_STRING("IPgateway").c_str(), strBuffer, sizeof(strBuffer)));
            Serial.print(".");
            SET_CONFIG_STRING("IPnameserver", read_string_from_file(IPnameserverFile, GET_CONFIG_STRING("IPnameserver").c_str(), strBuffer, sizeof(strBuffer)));
            Serial.print(".");
        }
        SET_CONFIG_BOOL("wwwPWrequired", read_int_from_file(www_pw_required_file) != 0);
        Serial.print(".");
        SET_CONFIG_STRING("wwwUsername", read_string_from_file(username_file, GET_CONFIG_STRING("wwwUsername").c_str(), strBuffer, sizeof(strBuffer)));
        Serial.print(".");
        SET_CONFIG_STRING("wwwCredentials", read_string_from_file(credentials_file, GET_CONFIG_STRING("wwwCredentials").c_str(), strBuffer, sizeof(strBuffer)));
        Serial.print(".");
        SET_CONFIG_INT("gdoSecurityType", read_int_from_file(gdoSecurityTypeFile, GET_CONFIG_INT("gdoSecurityType")));
        Serial.print(".");
        SET_CONFIG_INT("TTCdelay", read_int_from_file(TTCdelay_file));
        Serial.print(".");
        SET_CONFIG_INT("rebootSeconds", read_int_from_file(system_reboot_timer));
        Serial.print(".");
        SET_CONFIG_INT("ledIdleState", read_int_from_file(ledIdleStateFile, LOW));
        Serial.print(".");
        SET_CONFIG_INT("motionTriggers", read_int_from_file(motionTriggersFile));
        Serial.print(".");
#ifdef NTP_CLIENT
        SET_CONFIG_BOOL("enableNTP", read_int_from_file(enableNTPFile) != 0);
        Serial.print(".");
        SET_CONFIG_INT("doorUpdateAt", read_int_from_file(lastDoorUpdateFile));
        Serial.print(".");
#endif
        SET_CONFIG_BOOL("softAPmode", read_int_from_file(softAPmodeFile) != 0);
        Serial.print("\n");
        // Save to file so we never have to read from individual files again.
        write_config_to_file();
    }

    // All user configuration loaded, set globals...
    led.setIdleState((uint8_t)GET_CONFIG_INT("ledIdleState"));
    strlcpy(device_name, GET_CONFIG_STRING("deviceName").c_str(), sizeof(device_name));
    make_rfc952(device_name_rfc952, device_name, sizeof(device_name_rfc952));
    motionTriggers.asInt = GET_CONFIG_INT("motionTriggers");
    softAPmode = GET_CONFIG_BOOL("softAPmode");
#ifdef NTP_CLIENT
    // Only enable NTP client if not in soft AP mode.
    enableNTP = !softAPmode && GET_CONFIG_BOOL("enableNTP");
#endif

    // Now log what we have loaded
    RINFO("RFC952 compliant device hostname: %s", device_name_rfc952);

    RINFO("User configuration...");
    for (const auto &setting : userConfig)
    {
        RINFO("%s: %s", setting.first.c_str(),
              (setting.second.first == ConfigType::BOOL)     ? ((std::any_cast<bool>(setting.second.second)) ? "true" : "false")
              : (setting.second.first == ConfigType::INT)    ? std::to_string(std::any_cast<int>(setting.second.second)).c_str()
              : (setting.second.first == ConfigType::STRING) ? std::any_cast<std::string>(setting.second.second).c_str()
                                                             : "Unknown Type");
    }
    if (GET_CONFIG_BOOL("staticIP"))
    {
        RINFO("Static IP address configured... IP: %s, Mask: %s, Gateway: %s, DNS: %s",
              GET_CONFIG_STRING("IPaddress").c_str(),
              GET_CONFIG_STRING("IPnetmask").c_str(),
              GET_CONFIG_STRING("IPgateway").c_str(),
              GET_CONFIG_STRING("IPnameserver").c_str());
    }

    syslogEn = GET_CONFIG_BOOL("syslogEn");
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
        SET_CONFIG_BOOL("softAPmode", false);
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
    for (const auto &setting : userConfig)
    {
        file.print(setting.first.c_str());
        file.print(",");
        file.print(setting.second.first);
        file.print(",");
        switch (setting.second.first)
        {
        case ConfigType::BOOL:
            file.print((std::any_cast<bool>(setting.second.second)) ? "true" : "false");
            break;
        case ConfigType::INT:
            file.print(std::any_cast<int>(setting.second.second));
            break;
        case ConfigType::STRING:
            file.print(std::any_cast<std::string>(setting.second.second).c_str());
            break;
        }
        file.print("\n");
    }
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

        switch (atoi(type))
        {
        case ConfigType::BOOL:
            SET_CONFIG_BOOL(key, !strcmp(value, "true"));
            break;
        case ConfigType::INT:
            SET_CONFIG_INT(key, atoi(value));
            break;
        case ConfigType::STRING:
            SET_CONFIG_STRING(key, value);
            break;
        }
    }
    file.close();
    return true;
}