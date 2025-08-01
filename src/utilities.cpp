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
#include <cstring>
#include <algorithm>

// ESP system includes
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "LittleFS.h"
#else
#include <esp_sntp.h>
#include <HTTPClient.h>
#endif

// RATGDO project includes
#include "ratgdo.h"
#include "utilities.h"
#include "config.h"
#include "comms.h"
#include "led.h"

// Logger tag
static const char *TAG = "ratgdo-utils";

// What trigger motion...
motionTriggersUnion motionTriggers = {0, 0, 0, 0, 0, 0};
// Control booting into soft access point mode
bool softAPmode = false;
// Realm for MD5 credential hashing
const char www_realm[] = "RATGDO Login Required";
// automatically reboot after X seconds
uint32_t rebootSeconds = 0;

time_t clockSet = 0;
bool enableNTP = false;
uint64_t lastRebootAt = 0;
#define SNTP_SYNC_INTERVAL (60 * 60 * 1000) // 60 minutes

bool get_auto_timezone()
{
    bool success = false;
    WiFiClient client;
    HTTPClient http;

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
            userConfig->set(cfg_timeZone, tz.c_str());
            ESP_LOGI(TAG, "Automatic timezone set to: %s", userConfig->getTimeZone());
            success = true;
        }
        http.end();
    }
    return success;
}

#ifdef ESP8266
void time_is_set(bool from_sntp)
{
    clockSet = time(NULL);
}

uint32_t sntp_update_delay_MS_rfc_not_less_than_15000()
{
    return SNTP_SYNC_INTERVAL; // update every 60 minutes
}
#else
void time_is_set(timeval *tv)
{
    // Keep this callback function short and simple. Do not use our logging functions, as it may hang.
    clockSet = tv->tv_sec;
}
#endif

char *timeString(time_t reqTime, bool syslog)
{
    static char tBuffer[32];
    time_t tTime = 0;
    tm tmTime;
    tBuffer[0] = 0;
    tTime = ((reqTime == 0) && clockSet) ? time(NULL) : reqTime;
    if (tTime != 0)
    {
        localtime_r(&tTime, &tmTime);
        if (syslog)
        {
            // syslog compatible
            strftime(tBuffer, sizeof(tBuffer), "%Y-%m-%dT%H:%M:%S.000%z", &tmTime);
            // %z returns e.g. "-400" or "+1000", we need it to be "-4:00" or "+10:00"
            // this format is REQUIRED by syslog
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

char *make_rfc952(char *dest, const char *src, int size)
{
    // Make device name RFC952 compliant (simple, just checking for the basics)
    // RFC952 says max len of 24, [a-z][A-Z][0-9][-.] and no dash or period in last char.
    int i = 0;
    while (i <= std::min(24, size - 1) && src[i] != 0)
    {
        dest[i] = (std::isalnum((int)src[i]) || src[i] == '.') ? src[i] : '-';
        i++;
    }
    // remove dashes and periods from end of name
    while (i > 0 && (dest[i - 1] == '-' || dest[i - 1] == '.'))
    {
        dest[--i] = 0;
    }
    // null terminate string
    dest[std::min(i, std::min(24, size - 1))] = 0;
    return dest;
}

void load_all_config_settings()
{
    ESP_LOGI(TAG, "=== Load all config settings for %s", device_name);
    userConfig->load();
    // Set globals...
    strlcpy(device_name, userConfig->getDeviceName(), sizeof(device_name));
    make_rfc952(device_name_rfc952, device_name, sizeof(device_name_rfc952));
    led.setIdleState(userConfig->getLEDidle());
    motionTriggers.asInt = userConfig->getMotionTriggers();
    softAPmode = userConfig->getSoftAPmode();
    strlcpy(syslogIP, userConfig->getSyslogIP(), sizeof(syslogIP));
    syslogPort = userConfig->getSyslogPort();
    syslogEn = userConfig->getSyslogEn();
    rebootSeconds = userConfig->getRebootSeconds();

    // Now log what we have loaded
    ESP_LOGI(TAG, "   deviceName:          %s", userConfig->getDeviceName());
    ESP_LOGI(TAG, "   wifiChanged:         %s", userConfig->getWifiChanged() ? "true" : "false");
    ESP_LOGI(TAG, "   wifiPower:           %d", userConfig->getWifiPower());
    ESP_LOGI(TAG, "   wifiPhyMode:         %d", userConfig->getWifiPhyMode());
    ESP_LOGI(TAG, "   staticIP:            %s", userConfig->getStaticIP() ? "true" : "false");
    ESP_LOGI(TAG, "   localIP:             %s", userConfig->getLocalIP());
    ESP_LOGI(TAG, "   subnetMask:          %s", userConfig->getSubnetMask());
    ESP_LOGI(TAG, "   gatewayIP:           %s", userConfig->getGatewayIP());
    ESP_LOGI(TAG, "   nameserverIP:        %s", userConfig->getNameserverIP());
    ESP_LOGI(TAG, "   wwwPWrequired:       %s", userConfig->getPasswordRequired() ? "true" : "false");
    ESP_LOGI(TAG, "   wwwUsername:         %s", userConfig->getwwwUsername());
    ESP_LOGI(TAG, "   wwwCredentials:      %s", userConfig->getwwwCredentials());
    ESP_LOGI(TAG, "   GDOSecurityType:     %d", userConfig->getGDOSecurityType());
    ESP_LOGI(TAG, "   UseToggleToClose:    %d", userConfig->getUseToggleToClose());
    ESP_LOGI(TAG, "   TTCseconds:          %d", userConfig->getTTCseconds());
    ESP_LOGI(TAG, "   rebootSeconds:       %d", userConfig->getRebootSeconds());
    ESP_LOGI(TAG, "   LEDidle:             %d", userConfig->getLEDidle());
    ESP_LOGI(TAG, "   motionTriggers:      %d", userConfig->getMotionTriggers());
    ESP_LOGI(TAG, "   enableNTP:           %s", userConfig->getEnableNTP() ? "true" : "false");
    ESP_LOGI(TAG, "   doorUpdateAt:        %d", userConfig->getDoorUpdateAt());
    ESP_LOGI(TAG, "   timeZone:            %s", userConfig->getTimeZone());
    ESP_LOGI(TAG, "   softAPmode:          %s", userConfig->getSoftAPmode() ? "true" : "false");
    ESP_LOGI(TAG, "   syslogEn:            %s", userConfig->getSyslogEn() ? "true" : "false");
    ESP_LOGI(TAG, "   syslogIP:            %s", userConfig->getSyslogIP());
    ESP_LOGI(TAG, "   syslogPort:          %d", userConfig->getSyslogPort());
#ifndef ESP8266
    // Features not available on ESP8266
    ESP_LOGI(TAG, "   vehicleThreshold:    %d", userConfig->getVehicleThreshold());
    ESP_LOGI(TAG, "   vehicleHomeKit:      %s", userConfig->getVehicleHomeKit() ? "true" : "false");
    ESP_LOGI(TAG, "   laserEnabled:        %s", userConfig->getLaserEnabled() ? "true" : "false");
    ESP_LOGI(TAG, "   laserHomeKit:        %s", userConfig->getLaserHomeKit() ? "true" : "false");
    ESP_LOGI(TAG, "   assistDuration:      %d", userConfig->getAssistDuration());
    ESP_LOGI(TAG, "   occupancyDuration:   %d", userConfig->getOccupancyDuration());
    ESP_LOGI(TAG, "   enableIPv6:          %s", userConfig->getEnableIPv6() ? "true" : "false");
#endif
    ESP_LOGI(TAG, "RFC952 device hostname: %s", device_name_rfc952);

    // Only enable NTP client if not in soft AP mode.
    enableNTP = !softAPmode && userConfig->getEnableNTP();
    if (enableNTP)
    {
#ifdef ESP8266
        settimeofday_cb(time_is_set);
#else
        sntp_set_time_sync_notification_cb(time_is_set);
        sntp_set_sync_interval(SNTP_SYNC_INTERVAL);
#endif
        ESP_LOGI(TAG, "Timezone: %s", userConfig->getTimeZone());
        std::string tz = userConfig->getTimeZone();
        size_t pos = tz.find(';');
        if (pos != std::string::npos)
        {
            // semicolon may separate continent/city from posix TZ string
            ESP_LOGI(TAG, "Set timezone: %s", tz.substr(pos + 1).c_str());
            configTzTime(tz.substr(pos + 1).c_str(), NTP_SERVER);
        }
        else
        {
            // if no semicolon then no POSIX code, so use UTC
            ESP_LOGI(TAG, "Set timezone: UTC0");
            configTzTime("UTC0", NTP_SERVER);
        }
    }
}

void sync_and_restart()
{
    if (softAPmode)
    {
        // reset so next reboot will be standard station mode
        userConfig->set(cfg_softAPmode, false);
    }
    else
    {
        // In soft AP mode we never initialized garage door comms, so don't save rolling code.
        save_rolling_code();
    }
#ifdef ESP8266
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    // Save current logs in case needed for future analysis
    File file = LittleFS.open(REBOOT_LOG_MSG_FILE, "w");
    ratgdoLogger->printMessageLog(file);
    file.close();
    LittleFS.end();
#else
    // Save current logs in case needed for future analysis
    ratgdoLogger->saveMessageLog();
#endif
    delay(100);
    ESP.restart();
}
