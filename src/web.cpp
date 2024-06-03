// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// Copyright (c) 2023-24 David Kerr, https://github.com/dkerr64
// All rights reserved. GPLv3 License

// Browser cache control, time in seconds after which browser cache invalid
// This is used for CSS, JS and IMAGE file types.  Set to 30 days !!
#define CACHE_CONTROL (60 * 60 * 24 * 30)

// Reboot the system every X seconds, defaults to disabled
#define REBOOT_SECONDS (0)

#include <string>
#include <tuple>
#include <unordered_map>

#include "www/build/webcontent.h"

#include "ratgdo.h"
#include "comms.h"
#include "log.h"
#include "web.h"
#include "utilities.h"

#ifdef ENABLE_CRASH_LOG
#include "EspSaveCrash.h"
#endif
#include <arduino_homekit_server.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>
#include <eboot_command.h>
#include <MD5Builder.h>

#if defined(MMU_IRAM_HEAP) && defined(USE_IRAM_HEAP)
#include <umm_malloc/umm_malloc.h>
#include <umm_malloc/umm_heap_select.h>
#endif

#ifdef ENABLE_CRASH_LOG
#ifdef LOG_MSG_BUFFER
EspSaveCrash saveCrash(1408, 1024, true, &crashCallback);
#else
EspSaveCrash saveCrash(1408, 1024, true);
#endif
#endif

ESP8266WebServer server(80);

// Forward declare the internal URI handling functions...
void handle_reset();
void handle_reboot();
void handle_status();
void handle_everything();
void handle_setgdo();
void handle_logout();
void handle_auth();
void handle_subscribe();
void handle_showlog();
void handle_showrebootlog();
#ifdef ENABLE_CRASH_LOG
void handle_crashlog();
void handle_clearcrashlog();
#endif
#ifdef CRASH_DEBUG
void handle_forcecrash();
void handle_crash_oom();
void *crashptr;
char *test_str = NULL;
#endif
void handle_checkflash();
void handle_update();
void handle_firmware_upload();
void SSEHandler(uint8_t);
void handle_notfound();

// Built in URI handlers
const char restEvents[] PROGMEM = "/rest/events/";
typedef std::unordered_map<std::string, std::pair<const HTTPMethod, void (*)()>> BuiltInUriMap;
const BuiltInUriMap builtInUri = {
    {"/status.json", {HTTP_GET, handle_status}},
    {"/reset", {HTTP_POST, handle_reset}},
    {"/reboot", {HTTP_POST, handle_reboot}},
    {"/setgdo", {HTTP_POST, handle_setgdo}},
    {"/logout", {HTTP_GET, handle_logout}},
    {"/auth", {HTTP_GET, handle_auth}},
    {"/showlog", {HTTP_GET, handle_showlog}},
    {"/showrebootlog", {HTTP_GET, handle_showrebootlog}},
    {"/checkflash", {HTTP_GET, handle_checkflash}},
#ifdef ENABLE_CRASH_LOG
    {"/crashlog", {HTTP_GET, handle_crashlog}},
    {"/clearcrashlog", {HTTP_GET, handle_clearcrashlog}},
#endif
#ifdef CRASH_DEBUG
    {"/forcecrash", {HTTP_POST, handle_forcecrash}},
    {"/crashoom", {HTTP_POST, handle_crash_oom}},
#endif
    {"/rest/events/subscribe", {HTTP_GET, handle_subscribe}}};

// Make device_name available
extern "C" char device_name[DEVICE_NAME_SIZE];
// filename to save device name
extern "C" const char device_name_file[] = "device_name";

// Garage door status
extern struct GarageDoor garage_door;
// Local copy of door status
GarageDoor last_reported_garage_door;
bool last_reported_paired = false;
uint32_t lastDoorUpdateAt = 0;
GarageDoorCurrentState lastDoorState = (GarageDoorCurrentState)0xff;

// Garage door security type
extern uint8_t gdoSecurityType;

// For time-to-close control
extern uint8_t TTCdelay;
const char TTCdelay_file[] = "TTC_delay";

// userid/password
const char www_username[] = "admin";
const char www_password[] = "password";
const char www_realm[] = "RATGDO Login Required";

// MD5 Hash of "user:realm:password"
char www_credentials[48] = "10d3c00fa1e09696601ef113b99f8a87";
const char credentials_file[] = "www_credentials";

// Whether password required
bool passwordReq = false;
const char www_pw_required_file[] = "www_pw_required_file";

// Control automatic reboot
uint32_t rebootSeconds; // seconds between reboots
const char system_reboot_timer[] = "system_reboot_timer";
uint32_t min_heap = 0xffffffff;

// Control WiFi physical layer mode
WiFiPhyMode_t wifiPhyMode = (WiFiPhyMode_t)0;
extern "C" const char wifiPhyModeFile[] = "wifiPhyMode";
extern "C" const char wifiSettingsChangedFile[] = "wifiSettingsChanged";
uint16_t wifiPower = 20; // maximum
extern "C" const char wifiPowerFile[] = "wifiPower";

// number of times the device has crashed
int crashCount = 0;

// Implement our own firmware update so can enforce MD5 check.
// Based on ESP8266HTTPUpdateServer
String _updaterError;
bool _authenticatedUpdate;
char firmwareMD5[36] = "";
size_t firmwareSize = 0;
bool flashCRC = true;

// Common HTTP responses
const char response400missing[] PROGMEM = "400: Bad Request, missing argument\n";
const char response400invalid[] PROGMEM = "400: Bad Request, invalid argument\n";
const char response404[] PROGMEM = "404: Not Found\n";
const char response503[] PROGMEM = "503: Service Unavailable.\n";
const char response200[] PROGMEM = "HTTP/1.1 200 OK\nContent-Type: text/plain\nConnection: close\n\n";

const char *http_methods[] PROGMEM = {"HTTP_ANY", "HTTP_GET", "HTTP_HEAD", "HTTP_POST", "HTTP_PUT", "HTTP_PATCH", "HTTP_DELETE", "HTTP_OPTIONS"};

// For Server Sent Events (SSE) support
// Just reloading page causes register on new channel.  So we need a reasonable number
// to accommodate "extra" until old one is detected as disconnected.
#define SSE_MAX_CHANNELS 4
struct SSESubscription
{
    IPAddress clientIP;
    WiFiClient client;
    Ticker heartbeatTimer;
    bool SSEconnected;
    int SSEfailCount;
    String clientUUID;
    bool logViewer;
};
SSESubscription subscription[SSE_MAX_CHANNELS];
// During firmware update note which subscribed client is updating
SSESubscription *firmwareUpdateSub = NULL;

uint8_t subscriptionCount = 0;

#define JSON_BUFFER_SIZE 1024
char *json = NULL;

#define START_JSON(s)     \
    {                     \
        s[0] = 0;         \
        strcat(s, "{\n"); \
    }
#define END_JSON(s)           \
    {                         \
        s[strlen(s) - 2] = 0; \
        strcat(s, "\n}");     \
    }
#define ADD_INT(s, k, v)                      \
    {                                         \
        strcat(s, "\"");                      \
        strcat_P(s, PSTR(k));                 \
        strcat(s, "\": ");                    \
        strcat(s, std::to_string(v).c_str()); \
        strcat(s, ",\n");                     \
    }
#define ADD_STR(s, k, v)      \
    {                         \
        strcat(s, "\"");      \
        strcat_P(s, PSTR(k)); \
        strcat(s, "\": \"");  \
        strcat(s, (v));       \
        strcat(s, "\",\n");   \
    }
#define ADD_BOOL(s, k, v)                  \
    {                                      \
        strcat(s, "\"");                   \
        strcat_P(s, PSTR(k));              \
        strcat(s, "\": ");                 \
        strcat(s, (v) ? "true" : "false"); \
        strcat(s, ",\n");                  \
    }
#define ADD_BOOL_C(s, k, v, ov) \
    {                           \
        if (v != ov)            \
        {                       \
            ov = v;             \
            ADD_BOOL(s, k, v)   \
        }                       \
    }
#define ADD_STR_C(s, k, v, nv, ov) \
    {                              \
        if (nv != ov)              \
        {                          \
            ov = nv;               \
            ADD_STR(s, k, v)       \
        }                          \
    }
#define REMOVE_NL(s) for (char *p = (char *)s; (p = strchr(p, '\n')) != NULL; *p = ' ')

#define DOOR_STATE(s) (s == 0) ? "Open" : (s == 1) ? "Closed"  \
                                      : (s == 2)   ? "Opening" \
                                      : (s == 3)   ? "Closing" \
                                      : (s == 4)   ? "Stopped" \
                                                   : "Unknown"
#define LOCK_STATE(s) (s == 0) ? "Unsecured" : (s == 1) ? "Secured" \
                                           : (s == 2)   ? "Jammed"  \
                                                        : "Unknown"

void web_loop()
{
    unsigned long upTime = millis();
    START_JSON(json);
    if (garage_door.active && garage_door.current_state != lastDoorState)
    {
        RINFO("Current Door State changing from %d to %d", lastDoorState, garage_door.current_state);
        lastDoorUpdateAt = (lastDoorState == 0xff) ? 0 : upTime;
        lastDoorState = garage_door.current_state;
        // We send milliseconds relative to current time... ie updated X milliseconds ago
        // First time through, zero offset from upTime, which is when we last rebooted)
        ADD_INT(json, "lastDoorUpdateAt", (upTime - lastDoorUpdateAt));
    }
    // Conditional macros, only add if value has changed
    ADD_BOOL_C(json, "paired", homekit_is_paired(), last_reported_paired);
    ADD_STR_C(json, "garageDoorState", DOOR_STATE(garage_door.current_state), garage_door.current_state, last_reported_garage_door.current_state);
    ADD_STR_C(json, "garageLockState", LOCK_STATE(garage_door.current_lock), garage_door.current_lock, last_reported_garage_door.current_lock);
    ADD_BOOL_C(json, "garageLightOn", garage_door.light, last_reported_garage_door.light);
    ADD_BOOL_C(json, "garageMotion", garage_door.motion, last_reported_garage_door.motion);
    ADD_BOOL_C(json, "garageObstructed", garage_door.obstructed, last_reported_garage_door.obstructed);
    if (strlen(json) > 2)
    {
        // Have we added anything to the JSON string?
        ADD_INT(json, "upTime", upTime);
        END_JSON(json);
        REMOVE_NL(json);
        SSEBroadcastState(json);
    }
    if ((rebootSeconds != 0) && (rebootSeconds < millis() / 1000))
    {
        // Reboot the system if we have reached time...
        RINFO("Rebooting system as %i seconds expired", rebootSeconds);
        server.stop();
        sync_and_restart();
        return;
    }
    server.handleClient();

    uint32_t free_heap = system_get_free_heap_size();
    if (free_heap < min_heap)
    {
        min_heap = free_heap;
        RINFO("Free HEAP dropped to %d", min_heap);
    }
}

void setup_web()
{
    RINFO("Starting server");
    {
#if defined(MMU_IRAM_HEAP) && defined(USE_IRAM_HEAP)
        HeapSelectIram ephemeral;
#endif
        json = (char *)malloc(JSON_BUFFER_SIZE);
    }
    last_reported_paired = homekit_is_paired();
    // www_credentials = server.credentialHash(www_username, www_realm, www_password);
    read_string_from_file(credentials_file, www_credentials, www_credentials, sizeof(www_credentials));
    RINFO("WWW Credentials: %s", www_credentials);
    passwordReq = (read_int_from_file(www_pw_required_file) != 0);
    RINFO("WWW Password %s required", (passwordReq) ? "is" : "not");
    wifiPhyMode = (WiFiPhyMode_t)read_int_from_file(wifiPhyModeFile);
    RINFO("wifiPhyMode: %d", wifiPhyMode);
    TTCdelay = read_int_from_file(TTCdelay_file);
    RINFO("TTCdelay: %d", TTCdelay);
    wifiPower = (uint16_t)read_int_from_file(wifiPowerFile, 20);
    RINFO("wifiPower: %d", wifiPower);
    lastDoorUpdateAt = 0;
    lastDoorState = (GarageDoorCurrentState)0xff;

#ifdef ENABLE_CRASH_LOG
    crashCount = saveCrash.count();
    if (crashCount == 255)
    {
        saveCrash.clear();
        crashCount = 0;
    }
#endif
    rebootSeconds = read_int_from_file(system_reboot_timer, REBOOT_SECONDS);
    if (rebootSeconds > 0)
    {
        RINFO("System will reboot every %i seconds", rebootSeconds);
    }

    RINFO("Registering URI handlers");
    {
#if defined(MMU_IRAM_HEAP) && defined(USE_IRAM_HEAP)
        HeapSelectIram ephemeral;
#endif
        server.on("/update", HTTP_POST, handle_update, handle_firmware_upload);
        server.onNotFound(handle_everything);
    }

    // here the list of headers to be recorded
    const char *headerkeys[] = {"If-None-Match"};
    size_t headerkeyssize = sizeof(headerkeys) / sizeof(char *);
    // ask server to track these headers
    server.collectHeaders(headerkeys, headerkeyssize);

    server.begin();
    // initialize all the Server-Sent Events (SSE) slots.
    for (uint8_t i = 0; i < SSE_MAX_CHANNELS; i++)
    {
        subscription[i].SSEconnected = false;
        subscription[i].clientIP = INADDR_NONE;
        subscription[i].clientUUID.clear();
    }
    RINFO("HTTP server started");
    return;
}

/********* handlers **********/
void handle_notfound()
{
    RINFO("Sending 404 Not Found for: %s with method: %s", server.uri().c_str(), http_methods[server.method()]);
    server.send_P(404, type_txt, response404);
}

void handle_auth()
{
    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    server.send_P(200, type_txt, PSTR("Authenticated"));
    return;
}

void handle_reset()
{
    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    RINFO("... reset requested");
    homekit_storage_reset();
    server.client().setNoDelay(true);
    server.send_P(200, type_txt, PSTR("Device has been un-paired from HomeKit. Rebooting...\n"));
    server.stop();
    sync_and_restart();
    return;
}

void handle_reboot()
{
    RINFO("... reboot requested");
    server.client().setNoDelay(true);
    server.send_P(200, type_txt, PSTR("Rebooting...\n"));
    server.stop();
    sync_and_restart();
    return;
}

void handle_checkflash()
{
    flashCRC = ESP.checkFlashCRC();
    RINFO("checkFlashCRC: %s", flashCRC ? "true" : "false");
    server.client().setNoDelay(true);
    server.send_P(200, type_txt, flashCRC ? "true\n" : "false\n");
    return;
}

void load_page(const char *page)
{
    if (webcontent.count(page) == 0)
        return handle_notfound();

    const char *data = (char *)std::get<0>(webcontent.at(page));
    int length = std::get<1>(webcontent.at(page));
    const char *typeP = std::get<2>(webcontent.at(page));
    // need local copy as strcmp_P cannot take two PSTR()'s
    char type[MAX_MIME_TYPE_LEN];
    strncpy_P(type, typeP, MAX_MIME_TYPE_LEN);
    // Following for browser cache control...
    const char *crc32 = std::get<3>(webcontent.at(page)).c_str();
    bool cache = false;
    char cacheHdr[24] = "no-cache, no-store";
    char matchHdr[8] = "";
    if ((CACHE_CONTROL > 0) &&
        (!strcmp_P(type, type_css) || !strcmp_P(type, type_js) || strstr_P(type, PSTR("image"))))
    {
        sprintf(cacheHdr, "max-age=%i", CACHE_CONTROL);
        cache = true;
    }
    if (server.hasHeader(F("If-None-Match")))
        strlcpy(matchHdr, server.header(F("If-None-Match")).c_str(), sizeof(matchHdr));

    HTTPMethod method = server.method();
    if (strcmp(crc32, matchHdr))
    {
#if defined(MMU_IRAM_HEAP) && defined(USE_IRAM_HEAP)
        WiFiClient client = server.client();
        client.print("HTTP/1.1 200 OK\n");
        client.print("Content-Type: ");
        client.print(type);
        client.print("\n");
        client.print("Content-Encoding: gzip\n");
        client.print("Cache-Control: ");
        client.print(cacheHdr);
        client.print("\n");
        if (cache)
        {
            client.print("ETag: ");
            client.print(crc32);
            client.print("\n");
        }
        client.print("Connection: close\n");
        client.print("\n");
        client.flush();
#define CHUNK_SIZE 536
        while (length > 0)
        {
            uint32_t sent;
            uint32_t tx_size = min(CHUNK_SIZE, length);
            sent = client.write_P(data, tx_size);
            length -= sent;
            data += sent;
        }
        client.stop();
#else
        server.sendHeader(F("Content-Encoding"), F("gzip"));
        server.sendHeader(F("Cache-Control"), cacheHdr);
        if (cache)
            server.sendHeader(F("ETag"), crc32);
        if (method == HTTP_HEAD)
        {
            RINFO("Client %s requesting: %s (HTTP_HEAD, type: %s)", server.client().remoteIP().toString().c_str(), page, type);
            server.send_P(200, type, "", 0);
        }
        else
        {
            RINFO("Client %s requesting: %s (HTTP_GET, type: %s, length: %i)", server.client().remoteIP().toString().c_str(), page, type, length);
            server.send_P(200, type, data, length);
        }
#endif
    }
    else
    {
        RINFO("Sending 304 not modified to client %s requesting: %s (method: %s, type: %s)", server.client().remoteIP().toString().c_str(), page, http_methods[method], type);
        server.send_P(304, type, "", 0);
    }
    return;
}

void handle_everything()
{
    HTTPMethod method = server.method();
    String page = server.uri();
    const char *uri = page.c_str();

    if (builtInUri.count(uri) > 0)
    {
        // requested page matches one of our built-in handlers
        RINFO("Client %s requesting: %s (method: %s)", server.client().remoteIP().toString().c_str(), uri, http_methods[method]);
        if (method == builtInUri.at(uri).first)
            return builtInUri.at(uri).second();
        else
            return handle_notfound();
    }
    else if ((method == HTTP_GET) && (!strncmp_P(uri, restEvents, strlen(restEvents))))
    {
        // Request for "/rest/events/" with a channel number appended
        uri += strlen(restEvents);
        unsigned int channel = atoi(uri);
        if (channel < SSE_MAX_CHANNELS)
            return SSEHandler(channel);
        else
            return handle_notfound();
    }
    else if (method == HTTP_GET || method == HTTP_HEAD)
    {
        // HTTP_GET that does not match a built-in handler
        if (page == "/")
            return load_page("/index.html");
        else
            return load_page(uri);
    }
    // it is a HTTP_POST for unknown URI
    return handle_notfound();
}

void handle_status()
{
    unsigned long upTime = millis();
#define paired homekit_is_paired()
#define accessoryID (arduino_homekit_get_running_server() ? arduino_homekit_get_running_server()->accessory_id : "Inactive")
#define IPaddr WiFi.localIP().toString().c_str()
#define subnetMask WiFi.subnetMask().toString().c_str()
#define gatewayIP WiFi.gatewayIP().toString().c_str()
#define macAddress WiFi.macAddress().c_str()
#define wifiSSID WiFi.SSID().c_str()
#define GDOSecurityType std::to_string(gdoSecurityType).c_str()
    // Build the JSON string
    START_JSON(json);
    ADD_INT(json, "upTime", upTime);
    ADD_STR(json, "deviceName", device_name);
    ADD_BOOL(json, "paired", paired);
    ADD_STR(json, "firmwareVersion", std::string(AUTO_VERSION).c_str());
    ADD_STR(json, "accessoryID", accessoryID);
    ADD_STR(json, "localIP", IPaddr);
    ADD_STR(json, "subnetMask", subnetMask);
    ADD_STR(json, "gatewayIP", gatewayIP);
    ADD_STR(json, "macAddress", macAddress);
    ADD_STR(json, "wifiSSID", wifiSSID);
    ADD_STR(json, "wifiRSSI", (std::to_string(WiFi.RSSI()) + " dBm").c_str());
    ADD_STR(json, "GDOSecurityType", GDOSecurityType);
    ADD_STR(json, "garageDoorState", garage_door.active ? DOOR_STATE(garage_door.current_state) : DOOR_STATE(255));
    ADD_STR(json, "garageLockState", LOCK_STATE(garage_door.current_lock));
    ADD_BOOL(json, "garageLightOn", garage_door.light);
    ADD_BOOL(json, "garageMotion", garage_door.motion);
    ADD_BOOL(json, "garageObstructed", garage_door.obstructed);
    ADD_BOOL(json, "passwordRequired", passwordReq);
    ADD_INT(json, "rebootSeconds", rebootSeconds);
    uint32_t free_heap = system_get_free_heap_size();
    if (free_heap < min_heap)
        min_heap = free_heap;
    ADD_INT(json, "freeHeap", free_heap);
    ADD_INT(json, "minHeap", min_heap);
    ADD_INT(json, "minStack", ESP.getFreeContStack());
    ADD_INT(json, "crashCount", crashCount);
    ADD_INT(json, "wifiPhyMode", wifiPhyMode);
    ADD_INT(json, "wifiPower", wifiPower);
    ADD_INT(json, "TTCseconds", TTCdelay);
    // We send milliseconds relative to current time... ie updated X milliseconds ago
    ADD_INT(json, "lastDoorUpdateAt", (upTime - lastDoorUpdateAt));
    ADD_BOOL(json, "checkFlashCRC", flashCRC);
    END_JSON(json);

    // send JSON straight to serial port
    Serial.printf("%s\n", json);
    last_reported_garage_door = garage_door;

    server.sendHeader(F("Cache-Control"), F("no-cache, no-store"));
    server.send_P(200, type_json, json);
    RINFO("JSON length: %d", strlen(json));
    return;
}

void handle_logout()
{
    RINFO("Handle logout");
    return server.requestAuthentication(DIGEST_AUTH, www_realm);
}

void handle_setgdo()
{
    const char *key;
    const char *value;
    bool reboot = false;
    bool error = false;

    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        RINFO("In setGDO request authentication");
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }

    // Loop over all the GDO settings passed in...
    for (int i = 0; i < server.args(); i++)
    {
        key = server.argName(i).c_str();
        value = server.arg(i).c_str();

        RINFO("Key: %s, Value: %s", key, value);
        if (strlen(key) == 0 || strlen(value) == 0)
        {
            RINFO("Sending 400 bad request, missing argument, for: %s", server.uri().c_str());
            server.send_P(400, type_txt, response400missing);
            return;
        }

        // Check against each known setting
        if (!strcmp(key, "garageLightOn"))
        {
            set_light(!strcmp(value, "1") ? true : false);
        }
        else if (!strcmp(key, "garageDoorState"))
        {
            if (!strcmp(value, "1"))
                open_door();
            else
                close_door();
        }
        else if (!strcmp(key, "garageLockState"))
        {
            set_lock(!strcmp(value, "1") ? 1 : 0);
        }
        else if (!strcmp(key, "credentials"))
        {
            strlcpy(www_credentials, value, sizeof(www_credentials));
            RINFO("Writing new www_credentials to file: %s", www_credentials);
            write_string_to_file(credentials_file, www_credentials);
        }
        else if (!strcmp(key, "GDOSecurityType"))
        {
            uint32_t type = atoi(value);
            if ((type == 1) || (type == 2))
            {
                RINFO("SetGDO security type to %i", type);
                // reset the door opener ID and rolling codes.
                delete_file("rolling");
                delete_file("id_code");
                // Write to flash and reboot
                write_int_to_file("gdo_security", &type);
                reboot = true;
            }
            else
            {
                error = true;
            }
        }
        else if (!strcmp(key, "passwordRequired"))
        {
            uint32_t required = atoi(value);
            passwordReq = (required != 0);
            write_int_to_file(www_pw_required_file, &required);
        }
        else if (!strcmp(key, "rebootSeconds"))
        {
            uint32_t seconds = atoi(value);
            rebootSeconds = seconds;
            write_int_to_file(system_reboot_timer, &seconds);
            // only reboot if setting to non-zero
            reboot = (rebootSeconds != 0);
        }
        else if (!strcmp(key, "deviceName"))
        {
            if (strlen(value) > 0)
            {
                strlcpy(device_name, value, sizeof(device_name));
                write_string_to_file(device_name_file, device_name);
            }
        }
        else if (!strcmp(key, "wifiPhyMode"))
        {
            WiFiPhyMode_t wifiPhyMode = (WiFiPhyMode_t)atoi(value);
            if (read_int_from_file(wifiPhyModeFile) != (uint32_t)wifiPhyMode)
            {
                // Setting has changed.  Write new value and note that change has taken place
                write_int_to_file(wifiPhyModeFile, (uint32_t *)&wifiPhyMode);
                uint32_t changed = 1;
                write_int_to_file(wifiSettingsChangedFile, &changed);
                reboot = true;
            }
        }
        else if (!strcmp(key, "wifiPower"))
        {
            uint32_t wifiPower = (uint32_t)atoi(value);
            if (read_int_from_file(wifiPowerFile) != wifiPower)
            {
                // Setting has changed.  Write new value and note that change has taken place
                write_int_to_file(wifiPowerFile, &wifiPower);
                uint32_t changed = 1;
                write_int_to_file(wifiSettingsChangedFile, &changed);
                reboot = true;
            }
        }
        else if (!strcmp(key, "TTCseconds"))
        {
            uint32_t seconds = atoi(value);
            TTCdelay = (uint8_t)seconds;
            write_int_to_file(TTCdelay_file, &seconds);
        }
        else if (!strcmp(key, "updateUnderway"))
        {
            firmwareSize = 0;
            firmwareUpdateSub = NULL;
            char *md5 = strstr(value, "md5");
            char *size = strstr(value, "size");
            char *uuid = strstr(value, "uuid");
            if (md5 && size && uuid)
            {
                // JSON string of passed in.
                // Very basic parsing, not using library functions to save memory
                // find the colon after the key string
                md5 = strchr(md5, ':') + 1;
                size = strchr(size, ':') + 1;
                uuid = strchr(uuid, ':') + 1;
                // for strings find the double quote
                md5 = strchr(md5, '"') + 1;
                uuid = strchr(uuid, '"') + 1;
                // null terminate the strings (at closing quote).
                *strchr(md5, '"') = (char)0;
                *strchr(uuid, '"') = (char)0;
                // RINFO("MD5: %s, UUID: %s, Size: %d", md5, uuid, atoi(size));
                // save values...
                strlcpy(firmwareMD5, md5, sizeof(firmwareMD5));
                firmwareSize = atoi(size);
                for (uint8_t channel = 0; channel < SSE_MAX_CHANNELS; channel++)
                {
                    if (subscription[channel].SSEconnected && subscription[channel].clientUUID == uuid && subscription[channel].client.connected())
                    {
                        firmwareUpdateSub = &subscription[channel];
                        break;
                    }
                }
            }
            else
            {
                // old implementation... before JSON form of parameter.
                strlcpy(firmwareMD5, value, sizeof(firmwareMD5));
            }
        }
        else
        {
            error = true;
        }
    }
    RINFO("SetGDO Complete");
    // Simple error handling...
    if (error)
    {
        RINFO("Sending %s, for: %s", response400invalid, server.uri().c_str());
        server.send_P(400, type_txt, response400invalid);
    }
    else
    {
        if (reboot)
            server.send_P(200, type_html, PSTR("<p>Success. Reboot.</p>"));
        else
            server.send_P(200, type_html, PSTR("<p>Success.</p>"));
    }
    // Some settings require reboot to take effect...
    if (reboot)
    {
        RINFO("SetGDO Restart required");
        server.stop();
        sync_and_restart();
    }
    return;
}

void SSEheartbeat(SSESubscription *s)
{
    // Serial.printf("Heartbeat\n");
    if (!s)
        return;
    uint32_t free_heap = system_get_free_heap_size();
    if (free_heap < min_heap)
        min_heap = free_heap;

    if (!(s->clientIP))
        return;

    if (!(s->SSEconnected))
    {
        if (s->SSEfailCount++ >= 5)
        {
            // 5 heartbeats have failed... assume client will not connect
            // and free up the slot
            subscriptionCount--;
            RINFO("Client %s timeout waiting to listen, remove SSE subscription.  Total subscribed: %d", s->clientIP.toString().c_str(), subscriptionCount);
            s->heartbeatTimer.detach();
            s->clientIP = INADDR_NONE;
            s->clientUUID.clear();
            // no need to stop client socket because it is not live yet.
        }
        else
        {
            RINFO("Client %s not yet listening for SSE", s->clientIP.toString().c_str());
        }
        return;
    }

    if (s->client.connected())
    {
        START_JSON(json);
        ADD_INT(json, "upTime", millis());
        ADD_INT(json, "freeHeap", free_heap);
        ADD_INT(json, "minHeap", min_heap);
        ADD_STR(json, "wifiRSSI", (std::to_string(WiFi.RSSI()) + " dBm").c_str());
        ADD_BOOL(json, "checkFlashCRC", flashCRC);
        END_JSON(json);
        REMOVE_NL(json);
        s->client.printf("event: message\nretry: 15000\ndata: %s\n\n", json);
    }
    else
    {
        subscriptionCount--;
        RINFO("Client %s not listening, remove SSE subscription. Total subscribed: %d", s->clientIP.toString().c_str(), subscriptionCount);
        s->heartbeatTimer.detach();
        s->client.flush();
        s->client.stop();
        s->clientIP = INADDR_NONE;
        s->clientUUID.clear();
        s->SSEconnected = false;
    }
}

void SSEHandler(uint8_t channel)
{
    if (server.args() != 1)
    {
        RINFO("Sending %s, for: %s", response400missing, server.uri().c_str());
        server.send_P(400, type_txt, response400missing);
        return;
    }
    WiFiClient client = server.client();
    SSESubscription &s = subscription[channel];
    if (s.clientUUID != server.arg(0))
    {
        RINFO("Client %s with IP %s tries to listen for SSE but not subscribed", server.arg(0).c_str(), client.remoteIP().toString().c_str());
        return handle_notfound();
    }
    client.setNoDelay(true);
    client.setSync(true);
    s.client = client;                               // capture SSE server client connection
    server.setContentLength(CONTENT_LENGTH_UNKNOWN); // the payload can go on forever
    /*
    server.sendHeader("Cache-Control", "no-cache, no-store");
    server.sendHeader("Connection", "keep-alive");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    */
    server.sendContent_P(PSTR("HTTP/1.1 200 OK\nContent-Type: text/event-stream;\nConnection: keep-alive\nCache-Control: no-cache\nAccess-Control-Allow-Origin: *\n\n"));
    s.SSEconnected = true;
    s.SSEfailCount = 0;
    s.heartbeatTimer.attach_scheduled(1.0, [channel, &s]
                                      { SSEheartbeat(&s); });
    RINFO("Client %s listening for SSE events on channel %d", client.remoteIP().toString().c_str(), channel);
}

void handle_subscribe()
{
    uint8_t channel;
    IPAddress clientIP = server.client().remoteIP(); // get IP address of client
    String SSEurl = "/rest/events/";

    if (subscriptionCount == SSE_MAX_CHANNELS)
    {
        RINFO("Client %s SSE Subscription declined, subscription count: %d", clientIP.toString().c_str(), subscriptionCount);
        for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
        {
            RINFO("Client %d: %s at %s", channel, subscription[channel].clientUUID.c_str(), subscription[channel].clientIP.toString().c_str());
        }
        return handle_notfound(); // We ran out of channels
    }

    if (clientIP == INADDR_NONE)
    {
        RINFO("Sending %s, for: %s as clientIP missing", response400invalid, server.uri().c_str());
        server.send_P(400, type_txt, response400invalid);
        return;
    }

    // check we were passed at least one arguement
    if (server.args() < 1)
    {
        RINFO("Sending %s, for: %s", response400missing, server.uri().c_str());
        server.send_P(400, type_txt, response400missing);
        return;
    }

    // find the UUID and whether client wants to receive log messages
    int id = 0;
    bool logViewer = false;
    for (int i = 0; i < server.args(); i++)
    {
        if (server.argName(i) == "id")
            id = i;
        else if (server.argName(i) == "log")
            logViewer = true;
    }

    // check if we already have a subscription for this UUID
    for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
    {
        if (subscription[channel].clientUUID == server.arg(id))
        {
            if (subscription[channel].SSEconnected)
            {
                // Already connected.  We need to close it down as client will be reconnecting
                RINFO("SSE Subscribe - client %s with IP %s already connected on channel %d, remove subscription", server.arg(id).c_str(), clientIP.toString().c_str(), channel);
                subscription[channel].heartbeatTimer.detach();
                subscription[channel].client.flush();
                subscription[channel].client.stop();
            }
            else
            {
                // Subscribed but not connected yet, so nothing to close down.
                RINFO("SSE Subscribe - client %s with IP %s already subscribed but not connected on channel %d", server.arg(id).c_str(), clientIP.toString().c_str(), channel);
            }
            break;
        }
    }

    if (channel == SSE_MAX_CHANNELS)
    {
        // ended loop above without finding a match, so need to allocate a free slot
        ++subscriptionCount;
        for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
            if (!subscription[channel].clientIP)
                break;
    }
    subscription[channel] = {clientIP, server.client(), Ticker(), false, 0, server.arg(id), logViewer};
    SSEurl += channel;
    RINFO("SSE Subscription for client %s with IP %s: event bus location: %s, Total subscribed: %d", server.arg(id).c_str(), clientIP.toString().c_str(), SSEurl.c_str(), subscriptionCount);
    server.sendHeader(F("Cache-Control"), F("no-cache, no-store"));
    server.send_P(200, type_txt, SSEurl.c_str());
}

#ifdef ENABLE_CRASH_LOG
void handle_crashlog()
{
    RINFO("Request to display crash log...");
    WiFiClient client = server.client();
    client.print(response200);
    saveCrash.print(client);
#ifdef LOG_MSG_BUFFER
    if (crashCount > 0)
        printSavedLog(client);
#endif
    client.stop();
}
#endif

void handle_showlog()
{
    WiFiClient client = server.client();
    client.print(response200);
#ifdef LOG_MSG_BUFFER
    printMessageLog(client);
#endif
    client.stop();
}

void handle_showrebootlog()
{
    WiFiClient client = server.client();
    client.print(response200);
#ifdef LOG_MSG_BUFFER
    File file = LittleFS.open(REBOOT_LOG_MSG_FILE, "r");
    printSavedLog(file, client);
    file.close();
#endif
    client.stop();
}

#ifdef ENABLE_CRASH_LOG
void handle_clearcrashlog()
{
    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    RINFO("Clear saved crash log");
    saveCrash.clear();
    crashCount = 0;
    server.send_P(200, type_txt, PSTR("Crash log cleared\n"));
}
#endif

#ifdef CRASH_DEBUG
void handle_crash_oom()
{
    RINFO("Attempting to use up all memory");
    server.send_P(200, type_txt, PSTR("Attempting to use up all memory\n"));
    delay(1000);
    for (int i = 0; i < 30; i++)
    {
        crashptr = malloc(1024);
    }
}

void handle_forcecrash()
{
    RINFO("Attempting to null ptr deref");
    server.send_P(200, type_txt, PSTR("Attempting to null ptr deref\n"));
    delay(1000);
    RINFO("Result: %s", test_str);
}
#endif

void SSEBroadcastState(const char *data, BroadcastType type)
{
    // if nothing subscribed, then return
    if (subscriptionCount == 0)
        return;

    for (uint8_t i = 0; i < SSE_MAX_CHANNELS; i++)
    {
        if (subscription[i].SSEconnected && subscription[i].client.connected())
        {
            if (type == LOG_MESSAGE)
            {
                if (subscription[i].logViewer)
                {
                    subscription[i].client.printf_P(PSTR("event: logger\ndata: %s\n\n"), data);
                }
            }
            else if (type == RATGDO_STATUS)
            {
                String IPaddrstr = IPAddress(subscription[i].clientIP).toString();
                RINFO("SSE send to client %s on channel %d, data: %s", IPaddrstr.c_str(), i, data);
                subscription[i].client.printf_P(PSTR("event: message\ndata: %s\n\n"), data);
            }
        }
    }
}

// Implement our own firmware update so can enforce MD5 check.
// Based on ESP8266HTTPUpdateServer
void _setUpdaterError()
{
    StreamString str;
    Update.printError(str);
    _updaterError = str.c_str();
    RINFO("Update error: %s", str.c_str());
}

bool check_flash_md5(uint32_t flashAddr, uint32_t size, const char *expectedMD5)
{
    MD5Builder md5 = MD5Builder();
    uint8_t buffer[128];
    uint32_t pos = 0;

    md5.begin();
    while (pos < size)
    {
        size_t read_size = ((size - pos) > sizeof(buffer)) ? sizeof(buffer) : size - pos;
        ESP.flashRead(flashAddr + pos, buffer, read_size);
        md5.add(buffer, read_size);
        pos += sizeof(buffer);
    }
    md5.calculate();
    RINFO("Flash MD5: %s", md5.toString().c_str());
    return (strcmp(md5.toString().c_str(), expectedMD5) == 0);
}

void handle_update()
{
    bool verify = !strcmp(server.arg("action").c_str(), "verify");

    server.sendHeader(F("Access-Control-Allow-Headers"), "*");
    server.sendHeader(F("Access-Control-Allow-Origin"), "*");
    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        RINFO("In handle_update request authentication");
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }

    server.client().setNoDelay(true);
    if (!verify && Update.hasError())
    {
        // Error logged in _setUpdaterError
        eboot_command_clear();
        RERROR("Firmware upload error. Aborting update, not rebooting");
        server.send(400, "text/plain", _updaterError);
        return;
    }
    else
    {
        RINFO("Received MD5: %s", Update.md5String().c_str());
        struct eboot_command ebootCmd;
        eboot_command_read(&ebootCmd);
        // RINFO("eboot_command: 0x%08X 0x%08X [0x%08X 0x%08X 0x%08X (%d)]", ebootCmd.magic, ebootCmd.action, ebootCmd.args[0], ebootCmd.args[1], ebootCmd.args[2], ebootCmd.args[2]);
        if (!check_flash_md5(ebootCmd.args[0], firmwareSize, firmwareMD5))
        {
            // MD5 of flash does not match expected MD5
            eboot_command_clear();
            RERROR("Flash MD5 does not match expected MD5. Aborting update, not rebooting");
            server.send(400, "text/plain", "Flash MD5 does not match expected MD5.");
            return;
        }
    }

    if (server.args() > 0)
    {
        // Don't reboot, user/client must explicity request reboot.
        server.send_P(200, type_txt, PSTR("Upload Success.\n"));
    }
    else
    {
        // Legacy... no query string args, so automatically reboot...
        server.send_P(200, type_txt, PSTR("Upload Success. Rebooting...\n"));
        server.stop();
        sync_and_restart();
    }
}

void handle_firmware_upload()
{
    // handler for the file upload, gets the sketch bytes, and writes
    // them through the Update object
    static size_t uploadProgress;
    static unsigned int nextPrintPercent;
    HTTPUpload &upload = server.upload();
    static bool verify = false;
    static size_t size = 0;
    static const char *md5 = NULL;

    if (upload.status == UPLOAD_FILE_START)
    {
        _updaterError.clear();

        _authenticatedUpdate = !passwordReq || server.authenticateDigest(www_username, www_credentials);
        if (!_authenticatedUpdate)
        {
            RINFO("Unauthenticated Update");
            return;
        }
        RINFO("Update: %s", upload.filename.c_str());
        verify = !strcmp(server.arg("action").c_str(), "verify");
        size = atoi(server.arg("size").c_str());
        md5 = server.arg("md5").c_str();

        // We are updating.  If size and MD5 provided, save them
        firmwareSize = size;
        if (strlen(md5) > 0)
            strlcpy(firmwareMD5, md5, sizeof(firmwareMD5));

        uint32_t maxSketchSpace = ESP.getFreeSketchSpace();
        RINFO("Available space for upload: %lu", maxSketchSpace);
        RINFO("Firmware size: %s", (firmwareSize > 0) ? std::to_string(firmwareSize).c_str() : "Unknown");
        RINFO("Flash chip speed %d MHz", ESP.getFlashChipSpeed() / 1000000);
        // struct eboot_command ebootCmd;
        // eboot_command_read(&ebootCmd);
        // RINFO("eboot_command: 0x%08X 0x%08X [0x%08X 0x%08X 0x%08X (%d)]", ebootCmd.magic, ebootCmd.action, ebootCmd.args[0], ebootCmd.args[1], ebootCmd.args[2], ebootCmd.args[2]);
        if (!verify)
        {
            // Close HomeKit server so we don't have to handle HomeKit network traffic during update
            // Only if not verifying as either will have been shutdown on immediately prior upload, or we
            // just want to verify without disrupting operation of the HomeKit service.
            arduino_homekit_close();
        }
        if (!verify && !Update.begin((firmwareSize > 0) ? firmwareSize : maxSketchSpace, U_FLASH))
        {
            _setUpdaterError();
        }
        else if (strlen(firmwareMD5) > 0)
        {
            // uncomment for testing...
            // char firmwareMD5[] = "675cbfa11d83a792293fdc3beb199cXX";
            RINFO("Expected MD5: %s", firmwareMD5);
            Update.setMD5(firmwareMD5);
            if (firmwareSize > 0)
            {
                uploadProgress = 0;
                nextPrintPercent = 10;
                RINFO("%s progress: 00%%", verify ? "Verify" : "Update");
            }
        }
    }
    else if (_authenticatedUpdate && upload.status == UPLOAD_FILE_WRITE && !_updaterError.length())
    {
        // Progress dot dot dot
        Serial.printf(".");
        if (firmwareSize > 0)
        {
            uploadProgress += upload.currentSize;
            unsigned int uploadPercent = (uploadProgress * 100) / firmwareSize;
            if (uploadPercent >= nextPrintPercent)
            {
                Serial.printf("\n"); // newline after the dot dot dots
                RINFO("%s progress: %i%%", verify ? "Verify" : "Update", uploadPercent);
                SSEheartbeat(firmwareUpdateSub); // keep SSE connection alive.
                nextPrintPercent += 10;
                // Report percentage to browser client if it is listening
                if (firmwareUpdateSub && firmwareUpdateSub->client.connected())
                {
                    START_JSON(json);
                    ADD_INT(json, "uploadPercent", uploadPercent);
                    END_JSON(json);
                    REMOVE_NL(json);
                    firmwareUpdateSub->client.printf_P(PSTR("event: uploadStatus\ndata: %s\n\n"), json);
                }
            }
        }
        if (!verify)
        {
            // Don't write if verifying... we will just check MD5 of the flash at the end.
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
                _setUpdaterError();
        }
    }
    else if (_authenticatedUpdate && upload.status == UPLOAD_FILE_END && !_updaterError.length())
    {
        Serial.printf("\n"); // newline after last of the dot dot dots
        if (!verify)
        {
            if (Update.end(true))
            {
                RINFO("Upload size: %zu", upload.totalSize);
            }
            else
            {
                _setUpdaterError();
            }
        }
    }
    else if (_authenticatedUpdate && upload.status == UPLOAD_FILE_ABORTED)
    {
        if (!verify)
            Update.end();
        RINFO("%s was aborted", verify ? "Verify" : "Update");
    }
    esp_yield();
}
