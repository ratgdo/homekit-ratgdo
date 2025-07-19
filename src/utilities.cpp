// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include "utilities.h"
#include "log.h"
#include "LittleFS.h"
#include "comms.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// Logger tag
static const char *TAG = "ratgdo-utils";

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
bool clockSet = false;
bool enableNTP = false;
unsigned long lastRebootAt = 0;
int32_t savedDoorUpdateAt = 0;

bool get_tz()
{
    WiFiClient client;
    HTTPClient http;
    bool success = false;

    ESP_LOGI(TAG, "Get timezone automatically based on IP address");
    if (http.begin(client, "http://ip-api.com/csv/?fields=timezone"))
    {
        // start connection and send HTTP header
        int httpCode = http.GET();
        // httpCode will be negative on error
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
        {
            String tz = http.getString();
            tz.trim();
            strlcpy(userConfig->timeZone, tz.c_str(), sizeof(userConfig->timeZone));
            ESP_LOGI(TAG, "Automatic timezone set to: %s", userConfig->timeZone);
            success = true;
        }
        http.end();
    }
    return success;
}

void time_is_set(bool from_sntp)
{
    ESP_LOGI(TAG, "Clock set from NTP server: %d", from_sntp ? 1 : 0);
    clockSet = true;
    ESP_LOGI(TAG, "Current time: %s", timeString());
    if (strlen(userConfig->timeZone) == 0)
    {
        // no timeZone set, try and find it automatically
        get_tz();
        // if successful this will have set the region and city, but not
        // the POSIX time zone code. That will be done by browser.
    }
}

uint32_t sntp_update_delay_MS_rfc_not_less_than_15000()
{
    return 30 * 60 * 1000UL; // update every 30 minutes
}

char *timeString(time_t reqTime, bool syslog)
{
    // declare static so we don't use stack space
    static char tBuffer[32];
    static time_t tTime = 0;
    static tm tmTime;
    tBuffer[0] = 0;
    tTime = ((reqTime == 0) && clockSet) ? time(NULL) : reqTime;
    if (tTime != 0)
    {
        localtime_r(&tTime, &tmTime);
        if (syslog)
        {
            // syslog compatibe
            strftime(tBuffer, sizeof(tBuffer), "%Y-%m-%dT%H:%M:%S.000%z", &tmTime);
            // %z returns e.g. "-400" or "+1000", we need it to be "-4:00" or "+10:00"
            // thie format is REQUIRED by syslog
            int i = strlen(tBuffer);
            if (tBuffer[i - 5] == '-' || tBuffer[i - 5] == '+' ||
                tBuffer[i - 6] == '-' || tBuffer[i - 6] == '+')
            {
                tBuffer[i + 1] = 0;
                tBuffer[i] = tBuffer[i - 1];
                tBuffer[i - 1] = tBuffer[i - 2];
                tBuffer[i - 2] = ':';
            }
        }
        else
        {
            // Print format example: 27-Oct-2024 11:16:18 EDT
            strftime(tBuffer, sizeof(tBuffer), "%d-%b-%Y %H:%M:%S %Z", &tmTime);
        }
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
    ESP_LOGI(TAG, "=== Load all config settings for %s", device_name);
    if (!userConfig)
    {
        IRAM_START
        // IRAM heap is used only for allocating globals, to leave as much regular heap
        // available during operations.  We need to carefully monitor useage so as not
        // to exceed available IRAM.  We can adjust the LOG_BUFFER_SIZE (in log.h) if we
        // need to make more space available for initialization.
        userConfig = (userConfig_t *)malloc(sizeof(userConfig_t));
        if (!userConfig)
        {
            Serial.println("FATAL: Failed to allocate userConfig memory");
            ESP.restart();
            return;
        }
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
        ESP_LOGI(TAG, "Loading user configuration from legacy files");
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
        ESP_LOGI(TAG, "No settings saved, using factory defaults.");
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
    if (enableNTP)
    {
        settimeofday_cb(time_is_set);
        char *tz = strchr(userConfig->timeZone, ';');
        // semicolon may separate continent/city from posix TZ string
        // if no semicolon then no POSIX code, so use UTC
        configTime((tz) ? tz + 1 : "UTC0", NTP_SERVER);
    }
#endif
    syslogEn = userConfig->syslogEn;
    // Now log what we have loaded
    ESP_LOGI(TAG, "RFC952 compliant device hostname: %s", device_name_rfc952);
    ESP_LOGI(TAG, "User Configuration...");
    ESP_LOGI(TAG, "   deviceName:          %s", userConfig->deviceName);
    ESP_LOGI(TAG, "   wifiSettingsChanged: %s", userConfig->wifiSettingsChanged ? "true" : "false");
    ESP_LOGI(TAG, "   wifiPower:           %d", userConfig->wifiPower);
    ESP_LOGI(TAG, "   wifiPhyMode:         %d", userConfig->wifiPhyMode);
    ESP_LOGI(TAG, "   staticIP:            %s", userConfig->staticIP ? "true" : "false");
    ESP_LOGI(TAG, "   IPaddress:           %s", userConfig->IPaddress);
    ESP_LOGI(TAG, "   IPnetmask:           %s", userConfig->IPnetmask);
    ESP_LOGI(TAG, "   IPgateway:           %s", userConfig->IPgateway);
    ESP_LOGI(TAG, "   IPnameserver:        %s", userConfig->IPnameserver);
    ESP_LOGI(TAG, "   wwwPWrequired:       %s", userConfig->wwwPWrequired ? "true" : "false");
    ESP_LOGI(TAG, "   wwwUsername:         %s", userConfig->wwwUsername);
    ESP_LOGI(TAG, "   wwwCredentials:      %s", userConfig->wwwCredentials);
    ESP_LOGI(TAG, "   gdoSecurityType:     %d", userConfig->gdoSecurityType);
    ESP_LOGI(TAG, "   TTCdelay:            %d", userConfig->TTCdelay);
    ESP_LOGI(TAG, "   rebootSeconds:       %d", userConfig->rebootSeconds);
    ESP_LOGI(TAG, "   ledIdleState:        %d", userConfig->ledIdleState);
    ESP_LOGI(TAG, "   motionTriggers:      %d", userConfig->motionTriggers);
#ifdef NTP_CLIENT
    ESP_LOGI(TAG, "   enableNTP:           %s", userConfig->enableNTP ? "true" : "false");
    ESP_LOGI(TAG, "   doorUpdateAt:        %d", userConfig->doorUpdateAt);
    ESP_LOGI(TAG, "   timeZone:            %s", userConfig->timeZone);
#endif
    ESP_LOGI(TAG, "   softAPmode:          %s", userConfig->softAPmode ? "true" : "false");
    ESP_LOGI(TAG, "   syslogEn:            %s", userConfig->syslogEn ? "true" : "false");
    ESP_LOGI(TAG, "   syslogIP:            %s", userConfig->syslogIP);
    ESP_LOGI(TAG, "   syslogPort:          %d", userConfig->syslogPort);
}

void sync_and_restart()
{
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
    // ESP_LOGI(TAG,"checkFlashCRC: %s", ESP.checkFlashCRC() ? "true" : "false");
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
    ESP_LOGI(TAG, "writing %lu to file %s", value, filename);
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
    ESP_LOGI(TAG, "Writing string to file: %s", value);
    file.print(value);
    file.close();
}

void delete_file(const char *filename)
{
    LittleFS.remove(filename);
}

void write_config_to_file()
{
    ESP_LOGI(TAG, "Writing user configuration to file: %s", userConfigFile);

    // Atomic write: write to temp file first, then rename
    String tempFile = String(userConfigFile) + ".tmp";
    File file = LittleFS.open(tempFile.c_str(), "w");
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open temp config file for writing: %s", tempFile.c_str());
        return;
    }

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
    file.printf_P(PSTR("timeZone,,%s\n"), userConfig->timeZone);
#endif
    file.printf_P(PSTR("softAPmode,,%s\n"), userConfig->softAPmode ? "true" : "false");
    file.printf_P(PSTR("syslogEn,,%s\n"), userConfig->syslogEn ? "true" : "false");
    file.printf_P(PSTR("syslogIP,,%s\n"), userConfig->syslogIP);
    file.printf_P(PSTR("syslogPort,,%d\n"), userConfig->syslogPort);
    file.close();

    // Atomic operation: rename temp file to final file
    if (LittleFS.exists(userConfigFile))
    {
        LittleFS.remove(userConfigFile);
    }
    if (!LittleFS.rename(tempFile.c_str(), userConfigFile))
    {
        ESP_LOGE(TAG, "Failed to rename temp config file to final: %s -> %s", tempFile.c_str(), userConfigFile);
        LittleFS.remove(tempFile.c_str()); // Clean up temp file
        return;
    }

    ESP_LOGI(TAG, "Config file written atomically");
}

bool read_config_from_file()
{
    ESP_LOGI(TAG, "Read user configuration from file: %s", userConfigFile);
    File file = LittleFS.open(userConfigFile, "r");
    if (!file)
        return false;
    while (file.available())
    {
        String line = file.readStringUntil('\n');
        const char *key = line.c_str();
        char *type = strchr(key, ',');
        if (!type)
        {
            ESP_LOGI(TAG, "Malformed config line, skipping: %s", key);
            continue;
        }
        *type++ = 0;
        char *value = strchr(type, ',');
        if (!value)
        {
            ESP_LOGI(TAG, "Malformed config line, missing value: %s", key);
            continue;
        }
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
        else if (!strcmp(key, "timeZone"))
        {
            strlcpy(userConfig->timeZone, value, sizeof(userConfig->timeZone));
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
        else if (!strcmp(key, "syslogPort"))
        {
            userConfig->syslogPort = atoi(value);
        }
    }
    file.close();
    return true;
}