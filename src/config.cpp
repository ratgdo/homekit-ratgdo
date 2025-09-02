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

// ESP system files
#ifdef ESP8266
#include <LittleFS.h>
#else
#include <nvs_flash.h>
#include <nvs.h>
#endif

// RATGDO project includes
#include "ratgdo.h"
#include "config.h"
#include "utilities.h"
#include "comms.h"
#include "led.h"
#include "homekit.h"
#ifndef ESP8266
#include "vehicle.h"
#ifdef USE_GDOLIB
#include "gdo.h"
#endif // USE_GDOLIB
#endif

// Logger tag
static const char *TAG = "ratgdo-config";

char default_device_name[DEVICE_NAME_SIZE] = "";
#ifndef ESP8266
// on ESP8266 these are defined in homekit_decl.c
char device_name[DEVICE_NAME_SIZE] = "";
char device_name_rfc952[DEVICE_NAME_SIZE] = "";
#endif

// Construct the singleton tasks for user config
userSettings *userSettings::instancePtr = new userSettings();
userSettings *userConfig = userSettings::getInstance();
#ifndef ESP8266
nvRamClass *nvRamClass::instancePtr = new nvRamClass();
nvRamClass *nvRam = nvRamClass::getInstance();
#endif

#ifdef ESP8266
// ESP8266 is single core / single threaded, no mutex's.
#define TAKE_MUTEX()
#define GIVE_MUTEX()
#else
// ESP32 is multi-core, need to serialize access to JSON buffers
#define TAKE_MUTEX() xSemaphoreTake(mutex, portMAX_DELAY)
#define GIVE_MUTEX() xSemaphoreGive(mutex)
#endif

bool setDeviceName(const std::string &key, const char *name, configSetting *action)
{
    // Check we have a legal device name...
    make_rfc952(device_name_rfc952, name, sizeof(device_name_rfc952));
    if (strlen(device_name_rfc952) == 0)
    {
        // cannot have a empty device name, reset to default...
        strlcpy(device_name, default_device_name, sizeof(device_name));
        make_rfc952(device_name_rfc952, default_device_name, sizeof(device_name_rfc952));
        userConfig->set(key, default_device_name);
    }
    else
    {
        // device name okay, copy it to our global
        strlcpy(device_name, name, sizeof(device_name));
        userConfig->set(key, device_name);
    }
    return true;
}

bool helperWiFiPower(const std::string &key, const char *value, configSetting *action)
{
    // Only reboot if value has changed
    if (std::get<int>(action->value) != std::stoi(value))
    {
        ESP_LOGI(TAG, "Setting WiFi power to: %s", value);
        userConfig->set(key, value);
        action->reboot = true;
    }
    else
    {
        ESP_LOGI(TAG, "WiFi power unchanged at: %s", value);
        action->reboot = false;
    }
    return true;
}

bool helperWiFiPhyMode(const std::string &key, const char *value, configSetting *action)
{
    // Only reboot if value has changed
    if (std::get<int>(action->value) != std::stoi(value))
    {
        ESP_LOGI(TAG, "Setting WiFi mode to: %s", value);
        userConfig->set(key, value);
        action->reboot = true;
    }
    else
    {
        ESP_LOGI(TAG, "WiFi mode unchanged at: %s", value);
        action->reboot = false;
    }
    return true;
}

bool helperGDOSecurityType(const std::string &key, const char *value, configSetting *action)
{
    // Call fn to reset door
    userConfig->set(key, value);
#ifdef ESP8266
    action->reboot = true;
#else
    reset_door();
#endif
    return true;
}

bool helperLEDidle(const std::string &key, const char *value, configSetting *action)
{
    // call fn to set LED object
    userConfig->set(key, value);
    led.setIdleState(userConfig->getLEDidle());
    led.idle();
    return true;
}

bool helperMotionTriggers(const std::string &key, const char *value, configSetting *action)
{
    uint8_t triggers = (uint8_t)std::stoi(value);
    // Only reboot if need for motion sensor accessory changes...
    // action->reboot = (((triggers == 0) && (motionTriggers.asInt != 0)) || ((triggers != 0) && (motionTriggers.asInt == 0)));
    motionTriggers.asInt = triggers;
    userConfig->set(cfg_motionTriggers, motionTriggers.asInt);
    // enable HomeKit motion service (in case not already done);
#ifndef ESP8266 // TODO - make work for ESP8266
    if (triggers)
    {
        enable_service_homekit_motion(false);
    }
#endif // ESP32
    return true;
}

bool helperTimeZone(const std::string &key, const char *value, configSetting *action)
{
    userConfig->set(key, value);
    if (char *p = strchr(value, ';'))
    {
        // semicolon may separate continent/city from posix TZ string
        // if no semicolon then no POSIX code, so use UTC
        p++;
        ESP_LOGI(TAG, "Set timezone: %s", p);
        configTzTime(p, NTP_SERVER);
    }
    else
    {
        ESP_LOGI(TAG, "Set timezone: UTC0");
        configTzTime("UTC0", NTP_SERVER);
    }
    ESP_LOGI(TAG, "Local time: %s", timeString());
    return true;
}

bool helperSyslogEn(const std::string &key, const char *value, configSetting *action)
{
    userConfig->set(key, value);
    // these globals are set to optimize log message handling...
    strlcpy(syslogIP, userConfig->getSyslogIP(), sizeof(syslogIP));
    syslogPort = userConfig->getSyslogPort();
    syslogEn = userConfig->getSyslogEn();
    return true;
}

bool helperLogLevel(const std::string &key, const char *value, configSetting *action)
{
    userConfig->set(key, value);
#ifndef ESP32
    logLevel = (esp_log_level_t)userConfig->getLogLevel();
#endif // !ESP32
    return true;
}

#ifndef ESP8266
// These features are not available on ESP8266
bool helperBuiltInTTC(const std::string &key, const char *value, configSetting *action)
{
    userConfig->set(key, value);
#ifdef USE_GDOLIB
    if (!userConfig->getBuiltInTTC())
    {
        // We have just disabled use of GDO's built-in time-to-close.
        ESP_LOGI(TAG, "Disable built-in TTC, set to: %d", userConfig->getTTCseconds() < 60 ? 0 : userConfig->getTTCseconds());
        gdo_set_time_to_close(userConfig->getTTCseconds() < 60 ? 0 : userConfig->getTTCseconds());
    }
#endif // USE_GDOLIB
    return true;
}

bool helperVehicleThreshold(const std::string &key, const char *value, configSetting *action)
{
    userConfig->set(key, value);
    // set globals so takes effect immediately
    vehicleThresholdDistance = (uint32_t)std::stoi(value) * 10; // convert centimeters to millimeters
    return true;
}

bool helperVehicleHomeKit(const std::string &key, const char *value, configSetting *action)
{
    userConfig->set(key, value);
    enable_service_homekit_vehicle(userConfig->getVehicleHomeKit());
    return true;
}

bool helperLaser(const std::string &key, const char *value, configSetting *action)
{
    userConfig->set(key, value);
    enable_service_homekit_laser(userConfig->getLaserEnabled() && userConfig->getLaserHomeKit());
    return true;
}

#ifdef USE_GDOLIB
bool helperUseSWserial(const std::string &key, const char *value, configSetting *action)
{
    // We must shutdown the GDOLIB tasks before changing the useSWserial setting.
    gdo_deinit();
    userConfig->set(key, value);
    return true;
}
#endif

bool helperOccupancyDuration(const std::string &key, const char *value, configSetting *action)
{
    userConfig->set(key, value);
    enable_service_homekit_room_occupancy(userConfig->getOccupancyDuration() > 0);
    return true;
}
#endif // ESP32

/****************************************************************************
 * User settings class
 */
userSettings::userSettings()
{
#ifdef ESP8266
    LittleFS.begin();
    snprintf(default_device_name, sizeof(default_device_name), "Garage Door %06X", ESP.getChipId());
#else
    mutex = xSemaphoreCreateMutex(); // need to serialize set's
    uint8_t mac[6];
    Network.macAddress(mac);
    snprintf(default_device_name, sizeof(default_device_name), "Garage Door %02X%02X%02X", mac[3], mac[4], mac[5]);
#endif
    strlcpy(device_name, default_device_name, sizeof(device_name));
    make_rfc952(device_name_rfc952, default_device_name, sizeof(device_name_rfc952));
    IRAM_START(TAG);
    char *localIPBuf = (char *)malloc(IP4ADDR_STRLEN_MAX);
    strlcpy(localIPBuf, "0.0.0.0", 16);
    char *subnetMaskBuf = (char *)malloc(IP4ADDR_STRLEN_MAX);
    strlcpy(subnetMaskBuf, "0.0.0.0", 16);
    char *gatewayIPBuf = (char *)malloc(IP4ADDR_STRLEN_MAX);
    strlcpy(gatewayIPBuf, "0.0.0.0", 16);
    char *nameserverIPBuf = (char *)malloc(IP4ADDR_STRLEN_MAX);
    strlcpy(nameserverIPBuf, "0.0.0.0", 16);
    char *syslogIPBuf = (char *)malloc(IP4ADDR_STRLEN_MAX);
    strlcpy(syslogIPBuf, "0.0.0.0", 16);
    char *timezoneBuf = (char *)malloc(64);
    *timezoneBuf = (char)0;
    char *usernameBuf = (char *)malloc(32);
    strlcpy(usernameBuf, "admin", 32);
    char *credentialsBuf = (char *)malloc(36);
    strlcpy(credentialsBuf, "10d3c00fa1e09696601ef113b99f8a87", 36);
    //  key, {reboot, wifiChanged, value, fn to call}
    settings = {
        {cfg_deviceName, {false, false, (configStr){DEVICE_NAME_SIZE, default_device_name}, setDeviceName}}, // call fn to set global
        {cfg_wifiChanged, {true, true, false, NULL}},
        {cfg_wifiPower, {true, true, WIFI_POWER_MAX, helperWiFiPower}}, // call fn to set reboot only if setting changed
        {cfg_wifiPhyMode, {true, true, 0, helperWiFiPhyMode}},          // call fn to set reboot only if setting changed
        {cfg_staticIP, {true, true, false, NULL}},
        {cfg_localIP, {true, true, (configStr){IP4ADDR_STRLEN_MAX, localIPBuf}, NULL}},
        {cfg_subnetMask, {true, true, (configStr){IP4ADDR_STRLEN_MAX, subnetMaskBuf}, NULL}},
        {cfg_gatewayIP, {true, true, (configStr){IP4ADDR_STRLEN_MAX, gatewayIPBuf}, NULL}},
        {cfg_nameserverIP, {true, true, (configStr){IP4ADDR_STRLEN_MAX, nameserverIPBuf}, NULL}},
        {cfg_passwordRequired, {false, false, false, NULL}},
        {cfg_wwwUsername, {false, false, (configStr){32, usernameBuf}, NULL}},
        //  Credentials are MD5 Hash... server.credentialHash(username, realm, "password");
        {cfg_wwwCredentials, {false, false, (configStr){36, credentialsBuf}, NULL}},
        {cfg_GDOSecurityType, {true, false, 2, helperGDOSecurityType}}, // call fn to reset door
        {cfg_TTCseconds, {false, false, 5, NULL}},
        {cfg_TTClight, {false, false, true, NULL}},
        {cfg_rebootSeconds, {true, true, 0, NULL}},
        {cfg_LEDidle, {false, false, 0, helperLEDidle}},               // call fn to set LED object
        {cfg_motionTriggers, {false, false, 0, helperMotionTriggers}}, // call fn to enable HomeSpan service
        {cfg_enableNTP, {true, false, false, NULL}},
        {cfg_doorUpdateAt, {false, false, 0, NULL}},
        // Will contain string of region/city and POSIX code separated by semicolon...
        // For example... "America/New_York;EST5EDT,M3.2.0,M11.1.0"
        // Current maximum string length is known to be 60 chars (+ null terminator), see JavaScript console log.
        {cfg_timeZone, {false, false, (configStr){64, timezoneBuf}, helperTimeZone}}, // call fn to set system time zone
        {cfg_softAPmode, {true, false, false, NULL}},
        {cfg_syslogEn, {false, false, false, helperSyslogEn}}, // call fn to set globals
        {cfg_syslogIP, {false, false, (configStr){IP4ADDR_STRLEN_MAX, syslogIPBuf}, NULL}},
        {cfg_syslogPort, {false, false, 514, NULL}},
        {cfg_logLevel, {false, false, ESP_LOG_INFO, helperLogLevel}}, // call fn to set log level
        {cfg_dcOpenClose, {true, false, false, NULL}},
        {cfg_dcDebounceDuration, {true, false, 50, NULL}},
        {cfg_obstFromStatus, {true, false, true, NULL}},
        {cfg_useToggleToClose, {false, false, false, NULL}},
#ifndef ESP8266
        // These features not available on ESP8266
        {cfg_builtInTTC, {false, false, false, helperBuiltInTTC}},
        {cfg_vehicleThreshold, {false, false, 100, helperVehicleThreshold}}, // call fn to set globals
        {cfg_vehicleHomeKit, {false, false, false, helperVehicleHomeKit}},   // call fn to enable/disable HomeKit accessories
        {cfg_laserEnabled, {false, false, false, helperLaser}},
        {cfg_laserHomeKit, {false, false, true, helperLaser}}, // call fn to enable/disable HomeKit accessories
        {cfg_assistDuration, {false, false, 60, NULL}},
#ifdef USE_GDOLIB
        {cfg_useSWserial, {true, false, true, helperUseSWserial}}, // call fn to shut down GDO before switch
#endif
        {cfg_occupancyDuration, {false, false, 0, helperOccupancyDuration}}, // call fn to enable/disable HomeKit accessories
        {cfg_enableIPv6, {true, false, false, NULL}},
#endif
    };
    IRAM_END(TAG);
}

void userSettings::toStdOut()
{
    for (const auto &it : settings)
    {
        if (std::holds_alternative<configStr>(it.second.value))
        {
            Serial.printf("%s:\t%s\n", it.first.c_str(), std::get<configStr>(it.second.value).str);
        }
        else if (std::holds_alternative<int>(it.second.value))
        {
            Serial.printf("%s:\t%d\n", it.first.c_str(), std::get<int>(it.second.value));
        }
        else
        {
            Serial.printf("%s:\t%d\n", it.first.c_str(), std::get<bool>(it.second.value));
        }
    }
}

void userSettings::toFile(Print &file)
{
    for (const auto &it : settings)
    {
        if (std::holds_alternative<configStr>(it.second.value))
        {
            file.printf("%s,,%s\n", it.first.c_str(), std::get<configStr>(it.second.value).str);
        }
        else if (std::holds_alternative<int>(it.second.value))
        {
            file.printf("%s,,%d\n", it.first.c_str(), std::get<int>(it.second.value));
#ifdef ESP8266
            // Also save selected values under their old (v1.9.x and older) keynames
            // Just-in-case user uploads back-level firmware.
            if (it.first == cfg_GDOSecurityType)
                file.printf("gdoSecurityType,,%d\n", std::get<int>(it.second.value));
            else if (it.first == cfg_TTCseconds)
                file.printf("TTCdelay,,%d\n", std::get<int>(it.second.value));
            else if (it.first == cfg_LEDidle)
                file.printf("ledIdleState,,%d\n", std::get<int>(it.second.value));
#endif
        }
        else
        {
            file.printf("%s,,%d\n", it.first.c_str(), std::get<bool>(it.second.value));
#ifdef ESP8266
            // Also save selected values under their old (v1.9.x and older) keynames
            // Just-in-case user uploads back-level firmware.
            if (it.first == cfg_passwordRequired)
                file.printf("wwwPWrequired,,%d\n", std::get<bool>(it.second.value));
#endif
        }
    }
}

#ifdef ESP8266
// On ESP8266 we save settings to a file on LittleFS.
void userSettings::save()
{
    ESP_LOGI(TAG, "Writing user configuration to file: %s", cfg_configFile);
    // Atomic write: write to temp file first, then rename
    String tempFile = cfg_configFile + String(".tmp");
    File file = LittleFS.open(tempFile, "w");
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open temp config file for writing: %s", tempFile.c_str());
        return;
    }
    toFile(file);
    file.close();

    // Atomic operation: rename temp file to final file
    if (LittleFS.exists(cfg_configFile))
    {
        LittleFS.remove(cfg_configFile);
    }
    if (!LittleFS.rename(tempFile, cfg_configFile))
    {
        ESP_LOGE(TAG, "Failed to rename temp config file to final: %s -> %s", tempFile.c_str(), cfg_configFile);
        LittleFS.remove(tempFile); // Clean up temp file
        return;
    }
}

void userSettings::load()
{
    ESP_LOGI(TAG, "Read user configuration from file: %s", cfg_configFile);
    File file = LittleFS.open(cfg_configFile, "r");
    if (!file)
        return;
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
        // one-time conversion of legacy (v1.9.x and older) into current keynames.
        if (!strcmp(key, "wifiSettingsChanged"))
            set(cfg_wifiChanged, value);
        else if (!strcmp(key, "IPaddress"))
            set(cfg_localIP, value);
        else if (!strcmp(key, "IPnetmask"))
            set(cfg_subnetMask, value);
        else if (!strcmp(key, "IPgateway"))
            set(cfg_gatewayIP, value);
        else if (!strcmp(key, "IPnameserver"))
            set(cfg_nameserverIP, value);
        else if (!strcmp(key, "wwwPWrequired"))
            set(cfg_passwordRequired, value);
        else if (!strcmp(key, "gdoSecurityType"))
            set(cfg_GDOSecurityType, value);
        else if (!strcmp(key, "TTCdelay"))
            set(cfg_TTCseconds, value);
        else if (!strcmp(key, "ledIdleState"))
            set(cfg_LEDidle, value);
        else
            set(key, value);
    }
    file.close();
    return;
}

void userSettings::erase()
{
    if (LittleFS.exists(cfg_configFile))
    {
        LittleFS.remove(cfg_configFile);
    }
    ESP_LOGI(TAG, "Config file erased");
}
#else
// On ESP32 we save settings to nvram.
void userSettings::save()
{
    ESP_LOGI(TAG, "Writing user configuration to NVRAM");
    for (const auto &it : settings)
    {
        if (std::holds_alternative<configStr>(it.second.value))
        {
            nvRam->write(it.first, std::get<configStr>(it.second.value).str);
        }
        else if (std::holds_alternative<int>(it.second.value))
        {
            nvRam->write(it.first, std::get<int>(it.second.value));
        }
        else
        {
            nvRam->write(it.first, std::get<bool>(it.second.value) ? 1 : 0);
        }
    }
}

void userSettings::load()
{
    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    ESP_LOGI(TAG, "NVRAM Used Entries: (%lu), Free Entries: (%lu), Total Entries: (%lu), Namespace Count: (%lu)",
             nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries, nvs_stats.namespace_count);
    ESP_LOGI(TAG, "Read user configuration from NVRAM");
    for (auto &it : settings)
    {
        if (std::holds_alternative<configStr>(it.second.value))
        {
            char *p = std::get<configStr>(it.second.value).str;
            size_t max = std::get<configStr>(it.second.value).max;
            strlcpy(p, nvRam->read(it.first, p).c_str(), max);
        }
        else if (std::holds_alternative<int>(it.second.value))
        {
            it.second.value = (int)nvRam->read(it.first, std::get<int>(it.second.value));
        }
        else
        {
            it.second.value = (bool)(nvRam->read(it.first, std::get<bool>(it.second.value) ? 1 : 0) != 0);
        }
    }
}
#endif

bool userSettings::contains(const std::string &key)
{
    return (settings.count(key) > 0);
}

std::variant<bool, int, configStr> userSettings::get(const std::string &key)
{
    return settings[key].value;
}

configSetting userSettings::getDetail(const std::string &key)
{
    return settings[key];
}

bool userSettings::set(const std::string &key, const bool value)
{
    bool rc = false;
    TAKE_MUTEX();
    if (settings.count(key))
    {
        if (std::holds_alternative<bool>(settings[key].value))
        {
            settings[key].value = value;
#ifndef ESP8266
            nvRam->write(key, value ? 1 : 0);
#endif
            rc = true;
        }
    }
    GIVE_MUTEX();
    return rc;
}

bool userSettings::set(const std::string &key, const int value)
{
    bool rc = false;
    TAKE_MUTEX();
    if (settings.count(key))
    {
        if (std::holds_alternative<int>(settings[key].value))
        {
            settings[key].value = value;
#ifndef ESP8266
            nvRam->write(key, value);
#endif
            rc = true;
        }
        else if (std::holds_alternative<bool>(settings[key].value))
        {
            settings[key].value = (value != 0);
#ifndef ESP8266
            nvRam->write(key, value ? 1 : 0);
#endif
            rc = true;
        }
    }
    GIVE_MUTEX();
    return rc;
}

bool userSettings::set(const std::string &key, const char *value)
{
    bool rc = false;
    TAKE_MUTEX();
    if (settings.count(key))
    {
        if (std::holds_alternative<configStr>(settings[key].value))
        {
            char *p = std::get<configStr>(settings[key].value).str;
            size_t max = std::get<configStr>(settings[key].value).max;
            strlcpy(p, value, max);
#ifndef ESP8266
            nvRam->write(key, value);
#endif
            rc = true;
        }
        else if (std::holds_alternative<bool>(settings[key].value))
        {
            settings[key].value = (!strcmp(value, "true")) || (atoi(value) != 0);
#ifndef ESP8266
            nvRam->write(key, std::get<bool>(settings[key].value) ? 1 : 0);
#endif
            rc = true;
        }
        else if (std::holds_alternative<int>(settings[key].value))
        {
            settings[key].value = atoi(value);
#ifndef ESP8266
            nvRam->write(key, atoi(value));
#endif
            rc = true;
        }
    }
    GIVE_MUTEX();
    return rc;
}

#ifdef ESP8266
/****************************************************************************
 * No NVRAM on ESP8266 so just use simple read/write from files
 */
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

void delete_file(const char *filename)
{
    LittleFS.remove(filename);
}
#else
/****************************************************************************
 * NVRAM class
 */
nvRamClass::nvRamClass()
{
    ESP_LOGI(TAG, "Constructor for NVRAM class");
    // Initialize non volatile ram
    // We use this sparingly, most settings are saved in file system initialized below.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    err = nvs_open("ratgdo", NVS_READWRITE, &nvHandle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        nvHandle = 0;
    }
}

void nvRamClass::checkStats()
{
    nvs_stats_t nvs_stats;
    if (esp_err_t err = nvs_get_stats(NULL, &nvs_stats) == ESP_OK)
    {
        ESP_LOGI(TAG, "NVRAM Stats... UsedEntries = (%lu), FreeEntries = (%lu), TotalEntries = (%lu), Count = (%lu)\n",
                 nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries, nvs_stats.namespace_count);
    }
    else
    {
        ESP_LOGE(TAG, "Error return from nvs_get_stats: %d", err);
    }
}

int32_t nvRamClass::read(const std::string &constKey, const int32_t dflt)
{
    std::string key = constKey;
    if (key.length() >= NVS_KEY_NAME_MAX_SIZE)
        key.resize(NVS_KEY_NAME_MAX_SIZE - 1); // allow for null terminator

    int32_t value = dflt;
    esp_err_t err = nvs_get_i32(nvHandle, key.c_str(), &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "NVRAM get error for: %s (%s)", key.c_str(), esp_err_to_name(err));
    }
    return value;
}

std::string nvRamClass::read(const std::string &constKey, const char *dflt)
{
    std::string key = constKey;
    if (key.length() >= NVS_KEY_NAME_MAX_SIZE)
        key.resize(NVS_KEY_NAME_MAX_SIZE - 1); // allow for null terminator

    std::string value(dflt);
    size_t len;
    esp_err_t err = nvs_get_str(nvHandle, key.c_str(), NULL, &len);
    if (err == ESP_OK)
    {
        char *buf = (char *)malloc(len);
        if (nvs_get_str(nvHandle, key.c_str(), buf, &len) == ESP_OK)
        {
            value = buf;
        }
        free(buf);
    }
    else if (err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "NVRAM get error for: %s (%s)", key.c_str(), esp_err_to_name(err));
    }
    return value;
}

bool nvRamClass::write(const std::string &constKey, const int32_t value, bool commit)
{
    std::string key = constKey;
    if (key.length() >= NVS_KEY_NAME_MAX_SIZE)
        key.resize(NVS_KEY_NAME_MAX_SIZE - 1); // allow for null terminator

    esp_err_t err = nvs_set_i32(nvHandle, key.c_str(), value);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVRAM set error for: %s (%s)", key.c_str(), esp_err_to_name(err));
        return false;
    }
    if (commit)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(nvHandle));
    }
    return true;
}

bool nvRamClass::readBlob(const std::string &constKey, char *value, size_t size)
{
    std::string key = constKey;
    if (key.length() >= NVS_KEY_NAME_MAX_SIZE)
        key.resize(NVS_KEY_NAME_MAX_SIZE - 1); // allow for null terminator

    esp_err_t err = nvs_get_blob(nvHandle, key.c_str(), value, &size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVRAM get error for: %s (%s)", key.c_str(), esp_err_to_name(err));
        return false;
    }
    return true;
}

bool nvRamClass::writeBlob(const std::string &constKey, const char *value, size_t size, bool commit)
{
    std::string key = constKey;
    if (key.length() >= NVS_KEY_NAME_MAX_SIZE)
        key.resize(NVS_KEY_NAME_MAX_SIZE - 1); // allow for null terminator

    esp_err_t err = nvs_set_blob(nvHandle, key.c_str(), value, size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVRAM set error for: %s (%s)", key.c_str(), esp_err_to_name(err));
        return false;
    }
    if (commit)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(nvHandle));
    }
    return true;
}

bool nvRamClass::write(const std::string &constKey, const char *value, bool commit)
{
    std::string key = constKey;
    if (key.length() >= NVS_KEY_NAME_MAX_SIZE)
        key.resize(NVS_KEY_NAME_MAX_SIZE - 1); // allow for null terminator

    esp_err_t err = nvs_set_str(nvHandle, key.c_str(), value);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVRAM set error for: %s (%s)", key.c_str(), esp_err_to_name(err));
        return false;
    }
    if (commit)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(nvHandle));
    }
    return true;
}

bool nvRamClass::erase(const std::string &constKey)
{
    std::string key = constKey;
    if (key.length() >= NVS_KEY_NAME_MAX_SIZE)
        key.resize(NVS_KEY_NAME_MAX_SIZE - 1); // allow for null terminator

    esp_err_t err = nvs_erase_key(nvHandle, key.c_str());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVRAM erase error for: %s (%s)", key.c_str(), esp_err_to_name(err));
        return false;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(nvHandle));
    return true;
}

void nvRamClass::erase()
{
    esp_err_t err = nvs_erase_all(nvHandle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVRAM erase_all error: %s", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(nvHandle));
    return;
}
#endif
