// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include "utilities.h"
#include "log.h"
#include "LittleFS.h"
#include "comms.h"
#include <ESP8266WiFi.h>

bool staticIP = false;
char IPaddress[IP_ADDRESS_SIZE] = "0.0.0.0";
char IPnetmask[IP_ADDRESS_SIZE] = "0.0.0.0";
char IPgateway[IP_ADDRESS_SIZE] = "0.0.0.0";
char IPnameserver[IP_ADDRESS_SIZE] = "0.0.0.0";
const char staticIPfile[] = "static_ip_file";
const char IPaddressFile[] = "ip_address_file";
const char IPnetmaskFile[] = "ip_netmask_file";
const char IPgatewayFile[] = "ip_gateway_file";
const char IPnameserverFile[] = "ip_nameserver_file";

uint16_t wifiPower = 20; // maximum
const char wifiPowerFile[] = "wifiPower";
WiFiPhyMode_t wifiPhyMode = (WiFiPhyMode_t)0;
const char wifiPhyModeFile[] = "wifiPhyMode";
bool wifiSettingsChanged = false;
const char wifiSettingsChangedFile[] = "wifiSettingsChanged";

const char device_name_file[] = "device_name";

uint32_t rebootSeconds; // seconds between reboots
const char system_reboot_timer[] = "system_reboot_timer";

uint8_t gdoSecurityType = 2;
const char gdoSecurityTypeFile[] = "gdo_security";
uint8_t TTCdelay = 0;
const char TTCdelay_file[] = "TTC_delay";

char www_username[32] = "admin";
const char username_file[] = "www_username";
const char www_password[] = "password";
const char www_realm[] = "RATGDO Login Required";
// MD5 Hash of "user:realm:password"
// www_credentials = server.credentialHash(www_username, www_realm, www_password);
char www_credentials[48] = "10d3c00fa1e09696601ef113b99f8a87";
const char credentials_file[] = "www_credentials";
bool passwordReq = false;
const char www_pw_required_file[] = "www_pw_required_file";

// What is LED idle state (on or off);
const char ledIdleStateFile[] = "led_idle_state";

// What trigger motion...
motionTriggersUnion motionTriggers = {{0}};
const char motionTriggersFile[] = "motion_triggers";

#ifdef NTP_CLIENT
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
unsigned long lastRebootAt = 0;
const char lastDoorUpdateFile[] = "last_door_update";
int32_t savedDoorUpdateAt = 0;

char *timeString(time_t reqTime)
{
    static char tBuffer[32];
    tBuffer[0] = 0;
    time_t tTime = ((reqTime == 0) && timeClient.isTimeSet()) ? timeClient.getEpochTime() : reqTime;
    if (tTime != 0)
    {
        tm *tmTime = gmtime(&tTime);
        strftime(tBuffer, sizeof(tBuffer), "%Y/%m/%d - %H:%M:%S (UTC)", tmTime);
    }
    return tBuffer;
}
#endif

void load_all_config_settings()
{
    snprintf(device_name, DEVICE_NAME_SIZE, "Garage Door %06X", ESP.getChipId());
    RINFO("=== Load all config settings for %s", device_name);
    read_string_from_file(device_name_file, device_name, device_name, DEVICE_NAME_SIZE);
    RINFO("Configured device name: %s", device_name);
    wifiSettingsChanged = (read_int_from_file(wifiSettingsChangedFile) != 0);
    RINFO("WiFi settings have %schanged", (wifiSettingsChanged) ? "" : "not ");
    wifiPower = (uint16_t)read_int_from_file(wifiPowerFile, wifiPower);
    RINFO("wifiPower: %d", wifiPower);
    wifiPhyMode = (WiFiPhyMode_t)read_int_from_file(wifiPhyModeFile);
    RINFO("wifiPhyMode: %d", wifiPhyMode);
    staticIP = (read_int_from_file(staticIPfile) != 0);
    if (staticIP)
    {
        read_string_from_file(IPaddressFile, IPaddress, IPaddress, sizeof(IPaddress));
        read_string_from_file(IPnetmaskFile, IPnetmask, IPnetmask, sizeof(IPnetmask));
        read_string_from_file(IPgatewayFile, IPgateway, IPgateway, sizeof(IPgateway));
        read_string_from_file(IPnameserverFile, IPnameserver, IPnameserver, sizeof(IPnameserver));
        RINFO("Static IP address configured... IP: %s, Mask: %s, Gateway: %s, DNS: %s", IPaddress, IPnetmask, IPgateway, IPnameserver);
    }
    else
    {
        RINFO("IP address obtained by DHCP");
    }
    gdoSecurityType = (uint8_t)read_int_from_file(gdoSecurityTypeFile, gdoSecurityType);
    RINFO("Garage door security type: %d", gdoSecurityType);
    TTCdelay = read_int_from_file(TTCdelay_file);
    RINFO("Door close delay: %d seconds", TTCdelay);
    read_string_from_file(credentials_file, www_credentials, www_credentials, sizeof(www_credentials));
    RINFO("WWW Credentials: %s", www_credentials);
    read_string_from_file(username_file, www_username, www_username, sizeof(www_username));
    RINFO("WWW Username: %s", www_username);
    passwordReq = (read_int_from_file(www_pw_required_file) != 0);
    RINFO("WWW Password %s required", (passwordReq) ? "is" : "not");
    rebootSeconds = read_int_from_file(system_reboot_timer, REBOOT_SECONDS);
    if (rebootSeconds > 0)
    {
        RINFO("System will reboot every %i seconds", rebootSeconds);
    }
    led.setIdleState((uint8_t)read_int_from_file(ledIdleStateFile, LOW));
    RINFO("LED Idle State: %s", (led.getIdleState() == LOW) ? "on" : "off");
    motionTriggers.asInt = (uint8_t)read_int_from_file(motionTriggersFile);
    RINFO("Motion sensor trigger setting: %d", motionTriggers.asInt);
#ifdef NTP_CLIENT
    savedDoorUpdateAt = read_int_from_file(lastDoorUpdateFile);
    RINFO("Last saved door update at: %s", (savedDoorUpdateAt != 0) ? timeString(savedDoorUpdateAt) : "unknown");
#endif
}

void sync_and_restart()
{
#ifdef NTP_CLIENT
    timeClient.end();
#endif
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
        write_int_to_file(filename, value);
    }
    else
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