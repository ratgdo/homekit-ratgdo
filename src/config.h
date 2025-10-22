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
#pragma once

// C/C++ language includes
#include <variant>
#include <string>
#include <map>

// Arduino includes
#include <Print.h>

// ESP system includes
#ifndef ESP8266
#include <nvs.h>
#endif

// RATGDO project includes
// none

// Globals, for easy access...

#ifdef ESP8266
#define WIFI_POWER_MAX 20
#else
// TODO support WiFi TX Power... adjust max value if necessary (on ESP32 const are defined)
#define WIFI_POWER_MAX 20
#endif

#ifndef ESP8266
// on ESP8266 these are defined in homekit_decl.h
#define DEVICE_NAME_SIZE 32
extern char device_name[DEVICE_NAME_SIZE];
extern char device_name_rfc952[DEVICE_NAME_SIZE];
#endif
extern char default_device_name[DEVICE_NAME_SIZE];

// Define all the user setting keys as consts so we don't repeat strings
// throughout the code... and compiler will pick up any typos for us.
// const char cfg_deviceName[] PROGMEM = "deviceName";
// On ESP32 these are saved to NVRAM, on ESP8266 we will save to a file.
// NOTE... truncated to 15 chars when saving to NVRAM !!!
constexpr char cfg_deviceName[] PROGMEM = "deviceName";
constexpr char cfg_wifiChanged[] PROGMEM = "wifiChanged";
constexpr char cfg_wifiPower[] PROGMEM = "wifiPower";
constexpr char cfg_wifiPhyMode[] PROGMEM = "wifiPhyMode";
constexpr char cfg_staticIP[] PROGMEM = "staticIP";
constexpr char cfg_localIP[] PROGMEM = "localIP";
constexpr char cfg_subnetMask[] PROGMEM = "subnetMask";
constexpr char cfg_gatewayIP[] PROGMEM = "gatewayIP";
constexpr char cfg_nameserverIP[] PROGMEM = "nameserverIP";
constexpr char cfg_passwordRequired[] PROGMEM = "passwordRequired";
constexpr char cfg_wwwUsername[] PROGMEM = "wwwUsername";
constexpr char cfg_wwwCredentials[] PROGMEM = "wwwCredentials";
constexpr char cfg_GDOSecurityType[] PROGMEM = "GDOSecurityType";
constexpr char cfg_TTCseconds[] PROGMEM = "TTCseconds";
constexpr char cfg_TTClight[] PROGMEM = "TTClight";
constexpr char cfg_rebootSeconds[] PROGMEM = "rebootSeconds";
constexpr char cfg_LEDidle[] PROGMEM = "LEDidle";
constexpr char cfg_motionTriggers[] PROGMEM = "motionTriggers";
constexpr char cfg_enableNTP[] PROGMEM = "enableNTP";
constexpr char cfg_doorUpdateAt[] PROGMEM = "doorUpdateAt";
constexpr char cfg_doorOpenAt[] PROGMEM = "doorOpenAt";
constexpr char cfg_doorCloseAt[] PROGMEM = "doorCloseAt";
constexpr char cfg_timeZone[] PROGMEM = "timeZone";
constexpr char cfg_softAPmode[] PROGMEM = "softAPmode";
constexpr char cfg_syslogEn[] PROGMEM = "syslogEn";
constexpr char cfg_syslogIP[] PROGMEM = "syslogIP";
constexpr char cfg_syslogPort[] PROGMEM = "syslogPort";
constexpr char cfg_syslogFacility[] PROGMEM = "syslogFacility";
constexpr char cfg_logLevel[] PROGMEM = "logLevel";
constexpr char cfg_dcOpenClose[] PROGMEM = "dcOpenClose";
constexpr char cfg_dcDebounceDuration[] PROGMEM = "dcDebounceDuration";
constexpr char cfg_useSWserial[] PROGMEM = "useSWserial";
constexpr char cfg_obstFromStatus[] PROGMEM = "obstFromStatus";
#ifdef ESP8266
// On ESP8266 we save user config to a file in LittleFS
constexpr char cfg_configFile[] PROGMEM = "user_config";
#else
constexpr char cfg_builtInTTC[] PROGMEM = "builtInTTC";
constexpr char cfg_vehicleThreshold[] PROGMEM = "vehicleThreshold";
constexpr char cfg_vehicleHomeKit[] PROGMEM = "vehicleHomeKit";
constexpr char cfg_laserEnabled[] PROGMEM = "laserEnabled";
constexpr char cfg_laserHomeKit[] PROGMEM = "laserHomeKit";
constexpr char cfg_assistDuration[] PROGMEM = "assistDuration";
constexpr char cfg_occupancyDuration[] PROGMEM = "occupancyDuration";
constexpr char cfg_enableIPv6[] PROGMEM = "enableIPv6";
constexpr char cfg_homespanCLI[] PROGMEM = "homespanCLI";
#endif

constexpr char nvram_id_code[] PROGMEM = "id_code";
constexpr char nvram_rolling[] PROGMEM = "rolling";
constexpr char nvram_has_motion[] PROGMEM = "has_motion";
constexpr char nvram_open_history[] PROGMEM = "open_history";
constexpr char nvram_close_history[] PROGMEM = "close_history";
#ifndef ESP8266
constexpr char nvram_ratgdo_pw[] PROGMEM = "ratgdo_pw";
constexpr char nvram_has_distance[] PROGMEM = "has_distance";
#endif

struct configStr
{
    size_t max;
    char *str;
};

struct configSetting
{
    bool reboot;
    bool wifiChanged;
    std::variant<bool, int, configStr> value;
    bool (*fn)(const std::string &key, const char *value, configSetting *actions);
};

class userSettings
{
private:
    std::map<std::string, configSetting> settings;
    static userSettings *instancePtr;
    userSettings();
    void toFile(Print &file);
#ifndef ESP8266
    SemaphoreHandle_t mutex;
#endif

public:
    userSettings(const userSettings &obj) = delete;
    static userSettings *getInstance() { return instancePtr; }

    bool contains(const std::string &key);
    bool set(const std::string &key, const bool value);
    bool set(const std::string &key, const int value);
    bool set(const std::string &key, const char *value);
    std::variant<bool, int, configStr> get(const std::string &key);
    configSetting getDetail(const std::string &key);
    void toStdOut();
    void save();
    void load();
#ifdef ESP8266
    void erase();
    // on ESP32 every "set" saves the value to NVRAM
    // on ESP8266 we must call save() to save all user config values into a file.
#define ESP8266_SAVE_CONFIG() userConfig->save()
#else
#define ESP8266_SAVE_CONFIG()
#endif

    char *getDeviceName() { return (std::get<configStr>(get(cfg_deviceName)).str); };
    bool getWifiChanged() { return std::get<bool>(get(cfg_wifiChanged)); };
    uint32_t getWifiPower() { return std::get<int>(get(cfg_wifiPower)); };
    uint32_t getWifiPhyMode() { return std::get<int>(get(cfg_wifiPhyMode)); };
    bool getStaticIP() { return std::get<bool>(get(cfg_staticIP)); };
    char *getLocalIP() { return (std::get<configStr>(get(cfg_localIP)).str); };
    char *getSubnetMask() { return (std::get<configStr>(get(cfg_subnetMask)).str); };
    char *getGatewayIP() { return (std::get<configStr>(get(cfg_gatewayIP)).str); };
    char *getNameserverIP() { return (std::get<configStr>(get(cfg_nameserverIP)).str); };
    bool getPasswordRequired() { return std::get<bool>(get(cfg_passwordRequired)); };
    char *getwwwUsername() { return (std::get<configStr>(get(cfg_wwwUsername)).str); };
    char *getwwwCredentials() { return (std::get<configStr>(get(cfg_wwwCredentials)).str); };
    uint32_t getGDOSecurityType() { return std::get<int>(get(cfg_GDOSecurityType)); };
    uint32_t getTTCseconds() { return std::get<int>(get(cfg_TTCseconds)); };
    bool getTTClight() { return std::get<bool>(get(cfg_TTClight)); };
    uint32_t getRebootSeconds() { return std::get<int>(get(cfg_rebootSeconds)); };
    uint32_t getLEDidle() { return std::get<int>(get(cfg_LEDidle)); };
    uint32_t getMotionTriggers() { return std::get<int>(get(cfg_motionTriggers)); };
    bool getEnableNTP() { return std::get<bool>(get(cfg_enableNTP)); };
    uint32_t getDoorUpdateAt() { return std::get<int>(get(cfg_doorUpdateAt)); };
    uint32_t getDoorOpenAt() { return std::get<int>(get(cfg_doorOpenAt)); };
    uint32_t getDoorCloseAt() { return std::get<int>(get(cfg_doorCloseAt)); };
    char *getTimeZone() { return (std::get<configStr>(get(cfg_timeZone)).str); };
    bool getSoftAPmode() { return std::get<bool>(get(cfg_softAPmode)); };
    bool getSyslogEn() { return std::get<bool>(get(cfg_syslogEn)); };
    char *getSyslogIP() { return (std::get<configStr>(get(cfg_syslogIP)).str); };
    uint32_t getSyslogPort() { return std::get<int>(get(cfg_syslogPort)); };
    uint32_t getSyslogFacility() { return std::get<int>(get(cfg_syslogFacility)); };
    uint32_t getLogLevel() { return std::get<int>(get(cfg_logLevel)); };
    bool getDCOpenClose() { return std::get<bool>(get(cfg_dcOpenClose)); };
    uint32_t getDCDebounceDuration() { return std::get<int>(get(cfg_dcDebounceDuration)); };
    bool getObstFromStatus() { return std::get<bool>(get(cfg_obstFromStatus)); };
#ifdef RATGDO32_DISCO
    uint32_t getVehicleThreshold() { return std::get<int>(get(cfg_vehicleThreshold)); };
    bool getLaserEnabled() { return std::get<bool>(get(cfg_laserEnabled)); };
    bool getLaserHomeKit() { return std::get<bool>(get(cfg_laserHomeKit)); };
    bool getVehicleHomeKit() { return std::get<bool>(get(cfg_vehicleHomeKit)); };
    uint32_t getAssistDuration() { return std::get<int>(get(cfg_assistDuration)); };
#endif
#ifndef ESP8266
    bool getBuiltInTTC() { return std::get<bool>(get(cfg_builtInTTC)); };
    bool getUseSWserial() { return std::get<bool>(get(cfg_useSWserial)); };
    uint32_t getOccupancyDuration() { return std::get<int>(get(cfg_occupancyDuration)); };
    bool getEnableIPv6() { return std::get<bool>(get(cfg_enableIPv6)); };
    bool getEnableHomeSpanCLI() { return std::get<bool>(get(cfg_homespanCLI)); };
#endif
};
extern userSettings *userConfig;

#ifdef ESP8266
// No NVRAM on ESP8266 so just use simple read/write from files
uint32_t read_int_from_file(const char *filename, uint32_t defaultValue = 0);
void write_int_to_file(const char *filename, uint32_t value);
bool read_blob_from_file(const char *filename, void *value, size_t size);
void write_blob_to_file(const char *filename, const void *value, size_t size);
void delete_file(const char *filename);
#define read_door_int read_int_from_file
#define write_door_int write_int_to_file
#define read_door_data read_blob_from_file
#define write_door_data write_blob_to_file
#define erase_door_data delete_file
#else
class nvRamClass
{
private:
    nvs_handle_t nvHandle;
    static nvRamClass *instancePtr;
    nvRamClass();

public:
    nvRamClass(const nvRamClass &obj) = delete;
    static nvRamClass *getInstance() { return instancePtr; };

    void checkStats();
    int32_t read(const std::string &constKey, const int32_t dflt);
    int32_t read(const std::string &constKey) { return read(constKey, (int32_t)0); };
    std::string read(const std::string &constKey, const char *dflt);
    bool write(const std::string &constKey, const int32_t value, bool commit);
    bool write(const std::string &constKey, const int32_t value) { return write(constKey, value, true); };
    bool write(const std::string &constKey, const char *value, bool commit);
    bool write(const std::string &constKey, const char *value) { return write(constKey, value, true); };
    bool writeBlob(const std::string &constKey, const void *value, size_t size, bool commit);
    bool writeBlob(const std::string &constKey, const void *value, size_t size) { return writeBlob(constKey, value, size, true); };
    bool readBlob(const std::string &constKey, void *value, size_t size);
    bool erase(const std::string &constKey);
    void erase();
};
extern nvRamClass *nvRam;
#define read_door_int nvRam->read
#define write_door_int nvRam->write
#define read_door_str nvRam->read
#define write_door_str nvRam->write
#define read_door_data nvRam->readBlob
#define write_door_data nvRam->writeBlob
#define erase_door_data nvRam->erase
#endif
