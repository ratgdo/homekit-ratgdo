// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// Copyright (c) 2023-24 David Kerr, https://github.com/dkerr64
// All rights reserved. GPLv3 License

// Browser cache control, time in seconds after which browser cache invalid
// This is used for CSS, JS and IMAGE file types.  Set to 30 days !!
#define CACHE_CONTROL (60 * 60 * 24 * 30)

#include <string>
#include <tuple>
#include <unordered_map>

#include "www/build/webcontent.h"

#include "ratgdo.h"
#include "comms.h"
#include "log.h"
#include "web.h"
#include "utilities.h"
#include "wifi.h"

#ifdef ENABLE_CRASH_LOG
#include "EspSaveCrash.h"
#endif
#include <arduino_homekit_server.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>
#include <eboot_command.h>
#include <MD5Builder.h>

#ifdef ENABLE_CRASH_LOG
#ifdef LOG_MSG_BUFFER
EspSaveCrash saveCrash(1408, 1024, true, &crashCallback);
#else
EspSaveCrash saveCrash(1408, 1024, true);
#endif
#endif

// Declare web server on HTTP port 80.
ESP8266WebServer server(80);

// Connection throttling
#define MAX_CONCURRENT_REQUESTS 4
#define REQUEST_TIMEOUT_MS 5000
struct ActiveRequest {
    IPAddress clientIP;
    unsigned long startTime;
    bool inUse;
};
ActiveRequest activeRequests[MAX_CONCURRENT_REQUESTS];
int activeRequestCount = 0;

// Helper functions for connection throttling
bool registerRequest() {
    IPAddress clientIP = server.client().remoteIP();
    unsigned long now = millis();
    
    // Clean up timed-out requests
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (activeRequests[i].inUse && (now - activeRequests[i].startTime > REQUEST_TIMEOUT_MS)) {
            RINFO("Request timeout for client %s", activeRequests[i].clientIP.toString().c_str());
            activeRequests[i].inUse = false;
            activeRequestCount--;
        }
    }
    
    // Check if we're at capacity
    if (activeRequestCount >= MAX_CONCURRENT_REQUESTS) {
        RINFO("Max concurrent requests reached, rejecting %s", clientIP.toString().c_str());
        return false;
    }
    
    // Find a free slot
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (!activeRequests[i].inUse) {
            activeRequests[i].clientIP = clientIP;
            activeRequests[i].startTime = now;
            activeRequests[i].inUse = true;
            activeRequestCount++;
            return true;
        }
    }
    
    return false;
}

void unregisterRequest() {
    IPAddress clientIP = server.client().remoteIP();
    
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (activeRequests[i].inUse && activeRequests[i].clientIP == clientIP) {
            activeRequests[i].inUse = false;
            if (activeRequestCount > 0) activeRequestCount--; // Prevent negative count
            break;
        }
    }
}

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
void handle_accesspoint();
void handle_setssid();

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

// Garage door status
extern struct GarageDoor garage_door;
// Local copy of door status
GarageDoor last_reported_garage_door;
bool last_reported_paired = false;
uint32_t lastDoorUpdateAt = 0;
GarageDoorCurrentState lastDoorState = (GarageDoorCurrentState)0xff;

// Track our memory usage
extern "C" uint32_t free_heap;
extern "C" uint32_t min_heap;

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

const char softAPhttpPreamble[] PROGMEM = "HTTP/1.1 200 OK\nContent-Type: text/html\nCache-Control: no-cache, no-store\nConnection: close\n\n<!DOCTYPE html>";
const char softAPstyle[] PROGMEM = R"(<style>
.adv {
 display: none;
}
td,th {
 text-align: left;
}
th:nth-child(1n+4), td:nth-child(1n+4) {
 display: none;
 text-align: right;
}
</style>)";
const char softAPscript[] PROGMEM = R"(<script>
const warnTxt = 'Selecting SSID in advanced mode locks the device to a specific WiFi ' +
 'access point by its unique hardware BSSID. If that access point goes offline, or you replace ' +
 'it, then the device will NOT connect to WiFi.';
const setTxt = 'Set SSID and password, are you sure?';
function shAdv(checked) {
 Array.from(document.getElementsByClassName('adv')).forEach((elem) => {
  elem.style.display = checked ? 'table-row' : 'none';
 });
 Array.from(document.querySelectorAll('th:nth-child(1n+4), td:nth-child(1n+4)')).forEach((elem) => {
  elem.style.display = checked ? 'table-cell' : 'none';
 });
 document.getElementById('warn').innerHTML = checked ? '<p><b>WARNING: </b>' + warnTxt + '</p>' : '';
}
function confirmAdv() {
 if (document.getElementById('adv').checked) {
  return confirm('WARNING: ' + warnTxt + '\n\n' + setTxt);
 } else {
  return confirm(setTxt);
 }
}
</script>)";
const char softAPtableHead[] PROGMEM = R"(
<p>Select from available networks, or manually enter SSID:</p>
<form action='/setssid' method='post'>
<table>
<tr><td><input id='adv' name='advanced' type='checkbox' onclick='shAdv(this.checked)'></td><td colspan='2'>Advanced</td></tr>
<tr><th></th><th>SSID</th><th>RSSI</th><th>Chan</th><th>Hardware BSSID</th></tr>)";
const char softAPtableRow[] PROGMEM = R"(
<tr %s><td><input type='radio' name='net' value='%d' %s></td><td>%s</td><td>%ddBm</td><td>%d</td><td>&nbsp;&nbsp;%02x:%02x:%02x:%02x:%02x:%02x</td></tr>)";
const char softAPtableLastRow[] PROGMEM = R"(
<tr><td><input type='radio' name='net' value='%d'></td><td colspan='2'><input type='text' name='userSSID' placeholder='SSID' value='%s'></td></tr>)";
const char softAPtableFoot[] PROGMEM = R"(
</table>
<br><label for='pw'>Network password:&nbsp;</label>
<input id='pw' name='pw' type='password' placeholder='password'>
<p id='warn'></p>
<input type='submit' value='Submit' onclick='return confirmAdv();'>&nbsp;
<input type='submit' value='Rescan' formaction='/rescan'>&nbsp;
<input type='submit' value='Cancel' formaction='/reboot'
    onclick='return confirm("Reboot without changes, are you sure?");'>
</form>)";

// For Server Sent Events (SSE) support
// Just reloading page causes register on new channel.  So we need a reasonable number
// to accommodate "extra" until old one is detected as disconnected.
#define SSE_MAX_CHANNELS 8
struct SSESubscription
{
    IPAddress clientIP;
    WiFiClient client;
    Ticker heartbeatTimer;
    float heartbeatInterval;
    bool SSEconnected;
    int SSEfailCount;
    String clientUUID;
    bool logViewer;
};
SSESubscription subscription[SSE_MAX_CHANNELS];
// During firmware update note which subscribed client is updating
SSESubscription *firmwareUpdateSub = NULL;

uint8_t subscriptionCount = 0;

// Performance management - removed redundant connection tracking
#define MIN_REQUEST_INTERVAL_MS 100
static unsigned long last_request_time = 0;

// JSON response caching
#define JSON_BUFFER_SIZE 1280
#define JSON_CACHE_TIMEOUT_MS 500
char *json = NULL;
static char *cached_json = NULL;
static unsigned long json_cache_time = 0;
static bool json_cache_valid = false;
size_t json_offset = 0;


// Function to invalidate JSON cache when state changes
void invalidate_json_cache() {
    json_cache_valid = false;
}

// Performance monitoring
static unsigned long request_count = 0;
static unsigned long cache_hits = 0;
static unsigned long dropped_connections = 0;
static unsigned long max_response_time = 0;

// Safe string concatenation helper
static bool safe_strcat(char *dest, size_t dest_size, const char *src) {
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    
    if (dest_len + src_len >= dest_size) {
        RERROR("JSON buffer overflow prevented! Current: %d, Adding: %d, Max: %d", 
               dest_len, src_len, dest_size);
        return false;
    }
    
    strcat(dest, src);
    return true;
}

#define SAFE_STRCAT(dest, src) safe_strcat(dest, JSON_BUFFER_SIZE, src)

#define START_JSON(s)     \
    {                     \
        s[0] = 0;         \
        SAFE_STRCAT(s, "{\n"); \
    }
#define END_JSON(s)           \
    {                         \
        if (strlen(s) >= 2) { \
            s[strlen(s) - 2] = 0; \
            SAFE_STRCAT(s, "\n}");     \
        } \
    }
#define ADD_INT(s, k, v)                      \
    {                                         \
        char temp[32];                        \
        snprintf(temp, sizeof(temp), "\"%s\": %d,\n", k, (int)(v)); \
        SAFE_STRCAT(s, temp);                 \
    }
#define ADD_LONG(s, k, v)                     \
    {                                         \
        char temp[32];                        \
        snprintf(temp, sizeof(temp), "\"%s\": %lu,\n", k, (unsigned long)(v)); \
        SAFE_STRCAT(s, temp);                 \
    }
#define ADD_TIME(s, k, v)                     \
    {                                         \
        char temp[32];                        \
        snprintf(temp, sizeof(temp), "\"%s\": %lld,\n", k, (long long)(v)); \
        SAFE_STRCAT(s, temp);                 \
    }
#define ADD_STR(s, k, v)      \
    {                         \
        SAFE_STRCAT(s, "\"");      \
        SAFE_STRCAT(s, k); \
        SAFE_STRCAT(s, "\": \"");  \
        SAFE_STRCAT(s, (v));       \
        SAFE_STRCAT(s, "\",\n");   \
    }
#define ADD_BOOL(s, k, v)                  \
    {                                      \
        char temp[64];                     \
        snprintf(temp, sizeof(temp), "\"%s\": %s,\n", k, (v) ? "true" : "false"); \
        SAFE_STRCAT(s, temp);              \
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
    loop_id = LOOP_WEB;
    unsigned long upTime = millis();
    START_JSON(json);
    if (garage_door.active && garage_door.current_state != lastDoorState)
    {
        RINFO("Current Door State changing from %d to %d", lastDoorState, garage_door.current_state);
#ifdef NTP_CLIENT
        if (enableNTP && clockSet)
        {
            if (lastDoorState == 0xff)
            {
                // initialize with saved time.
                // lastDoorUpdateAt is milliseconds relative to system reboot time.
                lastDoorUpdateAt = (userConfig->doorUpdateAt != 0) ? ((userConfig->doorUpdateAt - time(NULL)) * 1000) + upTime : 0;
            }
            else
            {
                // first state change after a reboot, so really is a state change.
                userConfig->doorUpdateAt = time(NULL);
                write_config_to_file();
                lastDoorUpdateAt = upTime;
            }
        }
        else
        {
            lastDoorUpdateAt = (lastDoorState == 0xff) ? 0 : upTime;
        }
#else
        lastDoorUpdateAt = (lastDoorState == 0xff) ? 0 : upTime;
#endif
        lastDoorState = garage_door.current_state;
        // We send milliseconds relative to current time... ie updated X milliseconds ago
        // First time through, zero offset from upTime, which is when we last rebooted)
        ADD_LONG(json, "lastDoorUpdateAt", (upTime - lastDoorUpdateAt));
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
        ADD_LONG(json, "upTime", upTime);
        END_JSON(json);
        REMOVE_NL(json);
        SSEBroadcastState(json);
    }
    if ((userConfig->rebootSeconds != 0) && ((unsigned long)userConfig->rebootSeconds < millis() / 1000))
    {
        // Reboot the system if we have reached time...
        RINFO("Rebooting system as %i seconds expired", userConfig->rebootSeconds);
        server.stop();
        sync_and_restart();
        return;
    }
    
    // Rate limiting - minimum interval between requests
    unsigned long current_time = millis();
    if (current_time - last_request_time < MIN_REQUEST_INTERVAL_MS) {
        return; // Skip this cycle to enforce rate limit
    }
    
    server.handleClient();
    
    // Update last request time after handling client
    last_request_time = current_time;
}

void setup_web()
{
    RINFO("=== Starting HTTP web server ===");
    
    IRAM_START
    // IRAM heap is used only for allocating critical globals during initialization.
    json = (char *)malloc(JSON_BUFFER_SIZE);
    if (!json) {
        RERROR("Failed to allocate JSON buffer, size: %d", JSON_BUFFER_SIZE);
        sync_and_restart();
        return;
    }
    RINFO("Allocated buffer for JSON, size: %d", JSON_BUFFER_SIZE);
    
    last_reported_paired = homekit_is_paired();

    if (motionTriggers.asInt == 0)
    {
        // maybe just initialized. If we have motion sensor then set that and write back to file
        if (garage_door.has_motion_sensor)
        {
            motionTriggers.bit.motion = 1;
            userConfig->motionTriggers = motionTriggers.asInt;
            write_config_to_file();
        }
    }
    else if (garage_door.has_motion_sensor != (bool)motionTriggers.bit.motion)
    {
        // sync up web page tracking of whether we have motion sensor or not.
        RINFO("Motion trigger mismatch, reset to %d", (uint8_t)garage_door.has_motion_sensor);
        motionTriggers.bit.motion = (uint8_t)garage_door.has_motion_sensor;
        userConfig->motionTriggers = motionTriggers.asInt;
        write_config_to_file();
    }
    RINFO("Motion triggers, motion : %d, obstruction: %d, light key: %d, door key: %d, lock key: %d, asInt: %d",
          motionTriggers.bit.motion,
          motionTriggers.bit.obstruction,
          motionTriggers.bit.lightKey,
          motionTriggers.bit.doorKey,
          motionTriggers.bit.lockKey,
          motionTriggers.asInt);
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

    RINFO("Registering URI handlers");
    server.on("/update", HTTP_POST, handle_update, handle_firmware_upload);
    server.onNotFound(handle_everything);
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
    
    // Initialize connection tracking
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        activeRequests[i].inUse = false;
    }
    activeRequestCount = 0;
    IRAM_END("HTTP server started");
    return;
}

/********* handlers **********/
void handle_notfound()
{
    RINFO("Sending 404 Not Found for: %s with method: %s to client: %s", server.uri().c_str(), http_methods[server.method()], server.client().remoteIP().toString().c_str());
    server.send_P(404, type_txt, response404);
}

void handle_auth()
{
    if (userConfig->wwwPWrequired && !server.authenticateDigest(userConfig->wwwUsername, userConfig->wwwCredentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    server.send_P(200, type_txt, PSTR("Authenticated"));
    return;
}

void handle_reset()
{
    if (userConfig->wwwPWrequired && !server.authenticateDigest(userConfig->wwwUsername, userConfig->wwwCredentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    RINFO("... reset requested");
    homekit_storage_reset();
    server.client().setNoDelay(true);
    server.send_P(200, type_txt, PSTR("Device has been un-paired from HomeKit. Rebooting...\n"));
    // Allow time to process send() before terminating web server...
    delay(500);
    server.stop();
    sync_and_restart();
    return;
}

void handle_reboot()
{
    RINFO("... reboot requested");
    server.client().setNoDelay(true);
    server.send_P(200, type_txt, PSTR("Rebooting...\n"));
    // Allow time to process send() before terminating web server...
    delay(500);
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
        snprintf(cacheHdr, sizeof(cacheHdr), "max-age=%i", CACHE_CONTROL);
        cache = true;
    }
    if (server.hasHeader(F("If-None-Match")))
        strlcpy(matchHdr, server.header(F("If-None-Match")).c_str(), sizeof(matchHdr));

    HTTPMethod method = server.method();
    if (strcmp(crc32, matchHdr))
    {
#if defined(CHUNK_WEB_PAGES)
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
    // Connection throttling
    if (!registerRequest()) {
        server.send(503, "text/plain", "Server too busy, please try again");
        return;
    }
    
    HTTPMethod method = server.method();
    String page = server.uri();
    const char *uri = page.c_str();

    if ((WiFi.getMode() & WIFI_AP) == WIFI_AP)
    {
        // If we are in Soft Access Point mode
        RINFO("WiFi Soft Access Point mode");
        if (page == "/" || page == "/ap") {
            handle_accesspoint();
            unregisterRequest();
            return;
        }
        else if (page == "/setssid" && method == HTTP_POST) {
            handle_setssid();
            unregisterRequest();
            return;
        }
        else if (page == "/reboot" && method == HTTP_POST) {
            handle_reboot();
            unregisterRequest();
            return;
        }
        else if (page == "/rescan" && method == HTTP_POST)
        {
            wifi_scan();
            handle_accesspoint();
            unregisterRequest();
            return;
        }
        else {
            handle_notfound();
            unregisterRequest();
            return;
        }
    }

    if (builtInUri.count(uri) > 0)
    {
        // requested page matches one of our built-in handlers
        RINFO("Client %s requesting: %s (method: %s)", server.client().remoteIP().toString().c_str(), uri, http_methods[method]);
        if (method == builtInUri.at(uri).first) {
            builtInUri.at(uri).second();
        } else {
            handle_notfound();
        }
        unregisterRequest();
        return;
    }
    else if ((method == HTTP_GET) && (!strncmp_P(uri, restEvents, strlen(restEvents))))
    {
        // Request for "/rest/events/" with a channel number appended
        uri += strlen(restEvents);
        unsigned int channel = atoi(uri);
        if (channel < SSE_MAX_CHANNELS) {
            SSEHandler(channel);
        } else {
            handle_notfound();
        }
        unregisterRequest();
        return;
    }
    else if (method == HTTP_GET || method == HTTP_HEAD)
    {
        // HTTP_GET that does not match a built-in handler
        if (page == "/") {
            load_page("/index.html");
        } else {
            load_page(uri);
        }
        unregisterRequest();
        return;
    }
    // it is a HTTP_POST for unknown URI
    handle_notfound();
    unregisterRequest();
    return;
}

void handle_status()
{
    unsigned long start_time = millis();
    unsigned long upTime = start_time;
    request_count++;
    
    // Check if we can use cached JSON response
    if (json_cache_valid && (upTime - json_cache_time < JSON_CACHE_TIMEOUT_MS)) {
        cache_hits++;
        server.sendHeader(F("Cache-Control"), F("no-cache, no-store"));
        server.send_P(200, type_json, cached_json);
        unsigned long response_time = millis() - start_time;
        if (response_time > max_response_time) max_response_time = response_time;
        RINFO("JSON cached response, length: %d, time: %lums", strlen(cached_json), response_time);
        return;
    }
    
#define paired homekit_is_paired()
#define accessoryID arduino_homekit_get_running_server() ? arduino_homekit_get_running_server()->accessory_id : "Inactive"
#define clientCount arduino_homekit_get_running_server() ? arduino_homekit_get_running_server()->nfds : 0
#define IPaddr WiFi.localIP().toString().c_str()
#define subnetMask WiFi.subnetMask().toString().c_str()
#define gatewayIP WiFi.gatewayIP().toString().c_str()
#define nameserverIP WiFi.dnsIP().toString().c_str()
#define macAddress WiFi.macAddress().c_str()
#define wifiSSID WiFi.SSID().c_str()
#define GDOSecurityType std::to_string(gdoSecurityType).c_str()
    // Build the JSON string
    START_JSON(json);
    ADD_LONG(json, "upTime", upTime);
    ADD_STR(json, "deviceName", device_name);
    ADD_STR(json, "userName", userConfig->wwwUsername);
    ADD_BOOL(json, "paired", paired);
    ADD_STR(json, "firmwareVersion", std::string(AUTO_VERSION).c_str());
    ADD_STR(json, "accessoryID", accessoryID);
    ADD_INT(json, "clients", clientCount);
    ADD_STR(json, "localIP", IPaddr);
    ADD_STR(json, "subnetMask", subnetMask);
    ADD_STR(json, "gatewayIP", gatewayIP);
    ADD_STR(json, "nameserverIP", nameserverIP);
    ADD_STR(json, "macAddress", macAddress);
    ADD_STR(json, "wifiSSID", wifiSSID);
    ADD_STR(json, "wifiRSSI", (std::to_string(WiFi.RSSI()) + " dBm, Channel " + std::to_string(WiFi.channel())).c_str());
    ADD_STR(json, "wifiBSSID", WiFi.BSSIDstr().c_str());
    ADD_BOOL(json, "lockedAP", wifiConf.bssid_set)
    ADD_INT(json, "GDOSecurityType", userConfig->gdoSecurityType);
    ADD_STR(json, "garageDoorState", garage_door.active ? DOOR_STATE(garage_door.current_state) : DOOR_STATE(255));
    ADD_STR(json, "garageLockState", LOCK_STATE(garage_door.current_lock));
    ADD_BOOL(json, "garageLightOn", garage_door.light);
    ADD_BOOL(json, "garageMotion", garage_door.motion);
    ADD_BOOL(json, "garageObstructed", garage_door.obstructed);
    ADD_BOOL(json, "passwordRequired", userConfig->wwwPWrequired);
    ADD_INT(json, "rebootSeconds", userConfig->rebootSeconds);
    ADD_INT(json, "freeHeap", free_heap);
    ADD_INT(json, "minHeap", min_heap);
    ADD_INT(json, "minStack", ESP.getFreeContStack());
    ADD_INT(json, "crashCount", crashCount);
    ADD_INT(json, "wifiPhyMode", userConfig->wifiPhyMode);
    ADD_INT(json, "wifiPower", userConfig->wifiPower);
    ADD_BOOL(json, "staticIP", userConfig->staticIP);
    ADD_BOOL(json, "syslogEn", userConfig->syslogEn);
    ADD_STR(json, "syslogIP", userConfig->syslogIP);
    ADD_INT(json, "syslogPort", userConfig->syslogPort);
    ADD_INT(json, "TTCseconds", userConfig->TTCdelay);
    ADD_INT(json, "motionTriggers", motionTriggers.asInt);
    ADD_INT(json, "LEDidle", led.getIdleState());
    // We send milliseconds relative to current time... ie updated X milliseconds ago
    ADD_LONG(json, "lastDoorUpdateAt", (upTime - lastDoorUpdateAt));
    // Web performance metrics
    ADD_LONG(json, "webRequests", request_count);
    ADD_LONG(json, "webCacheHits", cache_hits);
    ADD_LONG(json, "webDroppedConns", dropped_connections);
    ADD_LONG(json, "webMaxResponseTime", max_response_time);
#ifdef NTP_CLIENT
    ADD_BOOL(json, "enableNTP", enableNTP);
    if (enableNTP)
    {
        if (clockSet)
        {
            ADD_TIME(json, "serverTime", time(NULL));
        }
        ADD_STR(json, "timeZone", userConfig->timeZone);
    }
#endif
    ADD_BOOL(json, "checkFlashCRC", flashCRC);
    END_JSON(json);

    // send JSON straight to serial port
    Serial.printf("%s\n", json);
    last_reported_garage_door = garage_door;

    // Cache the JSON response for performance
    if (cached_json == NULL) {
        cached_json = (char*)malloc(JSON_BUFFER_SIZE);
        if (!cached_json) {
            RINFO("Failed to allocate cached JSON buffer, caching disabled");
        }
    }
    if (cached_json != NULL) {
        strncpy(cached_json, json, JSON_BUFFER_SIZE - 1);
        cached_json[JSON_BUFFER_SIZE - 1] = '\0';
        json_cache_time = millis();
        json_cache_valid = true;
    }

    server.sendHeader(F("Cache-Control"), F("no-cache, no-store"));
    server.send_P(200, type_json, json);
    unsigned long response_time = millis() - start_time;
    if (response_time > max_response_time) max_response_time = response_time;
    RINFO("JSON length: %d, time: %lums", strlen(json), response_time);
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
    bool configChanged = false;

    if (!((server.args() == 1) && (server.argName(0) == "timeZone")))
    {
        // We will allow setting of time zone without authentication
        if (userConfig->wwwPWrequired && !server.authenticateDigest(userConfig->wwwUsername, userConfig->wwwCredentials))
        {
            RINFO("In setGDO request authentication");
            return server.requestAuthentication(DIGEST_AUTH, www_realm);
        }
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
            char *newUsername = strstr(value, "username");
            char *newCredentials = strstr(value, "credentials");
            if (newUsername && newCredentials)
            {
                // JSON string of passed in.
                // Very basic parsing, not using library functions to save memory
                // find the colon after the key string
                char *colon = strchr(newUsername, ':');
                if (!colon) { error = true; continue; }
                newUsername = colon + 1;
                
                colon = strchr(newCredentials, ':');
                if (!colon) { error = true; continue; }
                newCredentials = colon + 1;
                
                // for strings find the double quote
                char *quote = strchr(newUsername, '"');
                if (!quote) { error = true; continue; }
                newUsername = quote + 1;
                
                quote = strchr(newCredentials, '"');
                if (!quote) { error = true; continue; }
                newCredentials = quote + 1;
                
                // null terminate the strings (at closing quote).
                char *endQuote = strchr(newUsername, '"');
                if (!endQuote) { error = true; continue; }
                *endQuote = (char)0;
                
                endQuote = strchr(newCredentials, '"');
                if (!endQuote) { error = true; continue; }
                *endQuote = (char)0;
                
                // save values...
                strlcpy(userConfig->wwwUsername, newUsername, sizeof(userConfig->wwwUsername));
                strlcpy(userConfig->wwwCredentials, newCredentials, sizeof(userConfig->wwwCredentials));
                configChanged = true;
            }
            else
            {
                // old implementation... before JSON form of parameter.
                strlcpy(userConfig->wwwCredentials, value, sizeof(userConfig->wwwCredentials));
                configChanged = true;
            }
        }
        else if (!strcmp(key, "GDOSecurityType"))
        {
            int type = atoi(value);
            if ((type == 1) || (type == 2) || (type==3))
            {
                RINFO("SetGDO security type to %i", type);
                // reset the door opener ID, rolling code and presence of motion sensor.
                reset_door();
                userConfig->gdoSecurityType = type;
                configChanged = true;
                reboot = true;
            }
            else
            {
                error = true;
            }
        }
        else if (!strcmp(key, "resetDoor"))
        {
            RINFO("Request to reset door rolling codes");
            // reset the door opener ID, rolling code and presence of motion sensor.
            reset_door();
            reboot = true;
        }
        else if (!strcmp(key, "softAPmode"))
        {
            RINFO("Request to boot into soft access point mode");
            userConfig->softAPmode = true;
            configChanged = true;
            reboot = true;
        }
        else if (!strcmp(key, "passwordRequired"))
        {
            userConfig->wwwPWrequired = atoi(value) != 0;
            configChanged = true;
        }
        else if (!strcmp(key, "rebootSeconds"))
        {
            userConfig->rebootSeconds = atoi(value);
            configChanged = true;
            reboot = (userConfig->rebootSeconds != 0);
        }
        else if (!strcmp(key, "deviceName"))
        {
            if (strlen(value) > 0)
            {
                strlcpy(device_name, value, sizeof(device_name));
                strlcpy(userConfig->deviceName, value, sizeof(userConfig->deviceName));
                configChanged = true;
            }
        }
        else if (!strcmp(key, "wifiPhyMode"))
        {
            int wifiPhyMode = atoi(value);
            if (userConfig->wifiPhyMode != wifiPhyMode)
            {
                // Setting has changed.  Write new value and note that change has taken place
                userConfig->wifiPhyMode = wifiPhyMode;
                userConfig->wifiSettingsChanged = true;
                configChanged = true;
                reboot = true;
            }
        }
        else if (!strcmp(key, "wifiPower"))
        {
            int wifiPower = atoi(value);
            if (userConfig->wifiPower != wifiPower)
            {
                // Setting has changed.  Write new value and note that change has taken place
                userConfig->wifiPower = wifiPower;
                userConfig->wifiSettingsChanged = true;
                configChanged = true;
                reboot = true;
            }
        }
        else if (!strcmp(key, "staticIP"))
        {
            userConfig->staticIP = atoi(value) != 0;
            userConfig->wifiSettingsChanged = true;
            configChanged = true;
            reboot = true;
        }
        else if (!strcmp(key, "subnetMask"))
        {
            if (strlen(value) > 0)
            {
                strlcpy(userConfig->IPnetmask, value, sizeof(userConfig->IPnetmask));

                userConfig->wifiSettingsChanged = true;
                configChanged = true;
                reboot = true;
            }
        }
        else if (!strcmp(key, "gatewayIP"))
        {
            if (strlen(value) > 0)
            {
                strlcpy(userConfig->IPgateway, value, sizeof(userConfig->IPgateway));

                userConfig->wifiSettingsChanged = true;
                configChanged = true;
                reboot = true;
            }
        }
        else if (!strcmp(key, "nameserverIP"))
        {
            if (strlen(value) > 0)
            {
                strlcpy(userConfig->IPnameserver, value, sizeof(userConfig->IPnameserver));

                userConfig->wifiSettingsChanged = true;
                configChanged = true;
                reboot = true;
            }
        }
        else if (!strcmp(key, "localIP"))
        {
            if (strlen(value) > 0)
            {
                strlcpy(userConfig->IPaddress, value, sizeof(userConfig->IPaddress));
                userConfig->wifiSettingsChanged = true;
                configChanged = true;
                reboot = true;
            }
        }
        else if (!strcmp(key, "syslogEn"))
        {
            RINFO("Setting SyslogEN to %s", value);
            syslogEn = atoi(value) != 0;
            userConfig->syslogEn = syslogEn;
            configChanged = true;
        }
        else if (!strcmp(key, "syslogIP"))
        {
            if (strlen(value) > 0)
            {
                RINFO("Setting SyslogIP to %s", value);
                strlcpy(userConfig->syslogIP, value, sizeof(userConfig->syslogIP));
                configChanged = true;
            }
        }
        else if (!strcmp(key, "syslogPort"))
        {
            userConfig->syslogPort = atoi(value);
            configChanged = true;
        }
        else if (!strcmp(key, "TTCseconds"))
        {
            userConfig->TTCdelay = atoi(value);
            configChanged = true;
        }
        else if (!strcmp(key, "motionTriggers"))
        {
            uint8_t triggers = (uint8_t)atoi(value);
            // Only reboot if need for motion sensor accessory changes...
            reboot = (((triggers == 0) && (motionTriggers.asInt != 0)) || ((triggers != 0) && (motionTriggers.asInt == 0)));
            motionTriggers.asInt = triggers;
            userConfig->motionTriggers = motionTriggers.asInt;
            configChanged = true;
        }
        else if (!strcmp(key, "LEDidle"))
        {
            userConfig->ledIdleState = atoi(value);
            led.setIdleState(userConfig->ledIdleState);
            configChanged = true;
        }
#ifdef NTP_CLIENT
        else if (!strcmp(key, "enableNTP"))
        {
            userConfig->enableNTP = atoi(value) != 0;
            configChanged = true;
            reboot = true;
        }
        else if (!strcmp(key, "timeZone"))
        {
            strlcpy(userConfig->timeZone, value, sizeof(userConfig->timeZone));
            configChanged = true;
            // semicolon separates continent/city from POSIX time zone string
            char *tz = strchr(userConfig->timeZone, ';');
            if (tz)
                configTime(tz + 1, NTP_SERVER);
        }
#endif
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

    if (error)
    {
        // Simple error handling...
        RINFO("Sending %s, for: %s", response400invalid, server.uri().c_str());
        server.send_P(400, type_txt, response400invalid);
        return;
    }

    if (configChanged)
    {
        write_config_to_file();
    }
    if (reboot)
    {
        // Some settings require reboot to take effect
        server.send_P(200, type_html, PSTR("<p>Success. Reboot.</p>"));
        RINFO("SetGDO Restart required");
        // Allow time to process send() before terminating web server...
        delay(500);
        server.stop();
        sync_and_restart();
    }
    else
    {
        server.send_P(200, type_html, PSTR("<p>Success.</p>"));
    }
    return;
}

void SSEheartbeat(SSESubscription *s)
{
    // Serial.printf("Heartbeat\n");
    if (!s)
        return;

    if (!(s->clientIP))
        return;

    if (!(s->SSEconnected))
    {
        if (s->SSEfailCount++ >= 5)
        {
            // 5 heartbeats have failed... assume client will not connect
            // and free up the slot
            if (subscriptionCount > 0) subscriptionCount--; // Prevent negative count
            RINFO("Client %s timeout waiting to listen, remove SSE subscription.  Total subscribed: %d", s->clientIP.toString().c_str(), subscriptionCount);
            s->heartbeatTimer.detach();
            s->clientIP = INADDR_NONE;
            s->clientUUID.clear();
            s->SSEconnected = false;
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
        static int8_t lastRSSI = 0;
        static int lastClientCount = 0;

        START_JSON(json);
        ADD_LONG(json, "upTime", millis());
        ADD_INT(json, "freeHeap", free_heap);
        ADD_INT(json, "minHeap", min_heap);
        ADD_INT(json, "minStack", ESP.getFreeContStack());
        ADD_BOOL(json, "checkFlashCRC", flashCRC);
        if (lastRSSI != WiFi.RSSI())
        {
            lastRSSI = WiFi.RSSI();
            ADD_STR(json, "wifiRSSI", (std::to_string(lastRSSI) + " dBm, Channel " + std::to_string(WiFi.channel())).c_str());
        }
        if (arduino_homekit_get_running_server() && arduino_homekit_get_running_server()->nfds != lastClientCount)
        {
            lastClientCount = arduino_homekit_get_running_server()->nfds;
            ADD_INT(json, "clients", lastClientCount);
        }
        END_JSON(json);
        REMOVE_NL(json);
        // retry needed to before event:
        s->client.printf("retry: 15000\nevent: message\ndata: %s\n\n", json);
    }
    else
    {
        if (subscriptionCount > 0) subscriptionCount--; // Prevent negative count
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
    if (s.heartbeatInterval)
    {
        s.heartbeatTimer.attach_scheduled(s.heartbeatInterval, [channel, &s]{ SSEheartbeat(&s); });
    }
    // do one now, why make client wait
    SSEheartbeat(&s);
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

    // find the UUID and whether client wants to receive log messages and setting a heartbeat interval time
    int id = 0;
    bool logViewer = false;
    int heartbeatIntervalArgIdx = -1;
    for (int i = 0; i < server.args(); i++)
    {
        if (server.argName(i) == "id")
            id = i;
        else if (server.argName(i) == "log")
            logViewer = true;
        else if (server.argName(i) == "heartbeat")
            heartbeatIntervalArgIdx = i;
    }

    // check if we already have a subscription for this UUID
    bool foundExisting = false;
    for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
    {
        if (subscription[channel].clientUUID == server.arg(id))
        {
            foundExisting = true;
            if (subscription[channel].SSEconnected)
            {
                // Already connected.  We need to close it down as client will be reconnecting
                RINFO("SSE Subscribe - client %s with IP %s already connected on channel %d, remove subscription", server.arg(id).c_str(), clientIP.toString().c_str(), channel);
                subscription[channel].heartbeatTimer.detach();
                subscription[channel].client.flush();
                subscription[channel].client.stop();
                subscription[channel].SSEconnected = false;
            }
            else
            {
                // Subscribed but not connected yet, so nothing to close down.
                RINFO("SSE Subscribe - client %s with IP %s already subscribed but not connected on channel %d", server.arg(id).c_str(), clientIP.toString().c_str(), channel);
            }
            break;
        }
    }

    if (!foundExisting)
    {
        // Need to allocate a new slot
        for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
            if (!subscription[channel].clientIP)
                break;
        
        if (channel < SSE_MAX_CHANNELS) {
            subscriptionCount++;
        }
    }
    
    // Check if we found a free slot
    if (channel >= SSE_MAX_CHANNELS) {
        RINFO("SSE subscription failed - no free slots available");
        server.send(503, "text/plain", "No free subscription slots available");
        return;
    }
    
    // Validate client before assignment
    WiFiClient client = server.client();
    if (!client || !client.connected()) {
        RINFO("Invalid client for SSE subscription");
        server.send(400, "text/plain", "Invalid client connection");
        return;
    }

    // validate optional heartbeat interval
    float heartbeatInterval = 1.0;  // default
    if (heartbeatIntervalArgIdx >= 0) {
        float hbi = server.arg(heartbeatIntervalArgIdx).toFloat();
        // in range of 0 (no heartbeat) to 60 seconds
        if (hbi < 0.0 || hbi > 60.00) {
            RINFO("Invalid client for SSE subscription");
            server.send(400, "text/plain", "Invalid heartbeat interval (0 - 60)");
            return;
        }
        else {
            // set to validated interval
            heartbeatInterval = hbi;
        }
    }
    if (heartbeatInterval > 0) {
        RINFO("SSE Subscription for client %s has specified a heartbeat Interval of %2.1f seconds", server.arg(id).c_str(), heartbeatInterval);
    }
    else {
        RINFO("SSE Subscription for client %s has specified a heartbeat disabled", server.arg(id).c_str());
    }

    // Safe assignment with validation
    subscription[channel].clientIP = clientIP;
    subscription[channel].client = client;
    subscription[channel].heartbeatTimer = Ticker();
    subscription[channel].SSEconnected = false;
    subscription[channel].SSEfailCount = 0;
    subscription[channel].clientUUID = server.arg(id);
    subscription[channel].logViewer = logViewer;
    subscription[channel].heartbeatInterval = heartbeatInterval;
    
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
    if (userConfig->wwwPWrequired && !server.authenticateDigest(userConfig->wwwUsername, userConfig->wwwCredentials))
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
    // Flash LED to signal activity
    led.flash(FLASH_MS);

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
    if (userConfig->wwwPWrequired && !server.authenticateDigest(userConfig->wwwUsername, userConfig->wwwCredentials))
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
        // Allow time to process send() before terminating web server...
        delay(500);
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

        _authenticatedUpdate = !userConfig->wwwPWrequired || server.authenticateDigest(userConfig->wwwUsername, userConfig->wwwCredentials);
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
            IRAM_START
            IRAM_END("HomeKit Server Closed");
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

void handle_accesspoint()
{
    bool connected = WiFi.isConnected();
    String previousSSID = "";
    bool match = false;
    if (connected)
    {
        previousSSID = WiFi.SSID();
    }
    RINFO("Number of WiFi networks: %d", wifiNets.size());
    String currentSSID = "";
    WiFiClient client = server.client();

    client.print(softAPhttpPreamble);
    client.print("<html><head>");
    client.print(softAPstyle);
    client.print(softAPscript);
    client.print("</head><body style='font-family: monospace'");
    client.print(softAPtableHead);
    int i = 0;
    for (wifiNet_t net : wifiNets)
    {
        bool hide = true;
        bool matchSSID = (previousSSID == net.ssid);
        if (matchSSID)
            match = true;
        if (currentSSID != net.ssid)
        {
            currentSSID = net.ssid;
            hide = false;
        }
        else
        {
            matchSSID = false;
        }
        client.printf_P(softAPtableRow, (hide) ? "class='adv'" : "", i, (matchSSID) ? "checked='checked'" : "",
                        net.ssid.c_str(), net.rssi, net.channel,
                        net.bssid[0], net.bssid[1], net.bssid[2], net.bssid[3], net.bssid[4], net.bssid[5]);
        i++;
    }
    // user entered value
    client.printf_P(softAPtableLastRow, i, (!match) ? previousSSID.c_str() : "");
    client.print(softAPtableFoot);
    client.print("</body></html>");
    client.flush();
    client.stop();
    return;
}

void handle_setssid()
{
    if (server.args() < 3)
    {
        RINFO("Sending %s, for: %s as invalid number of args", response400invalid, server.uri().c_str());
        server.send_P(400, type_txt, response400invalid);
        return;
    }

    const unsigned int net = atoi(server.arg("net").c_str());
    const char *pw = server.arg("pw").c_str();
    const char *userSSID = server.arg("userSSID").c_str();
    const char *ssid = userSSID;
    bool advanced = server.arg("advanced") == "on";
    wifiNet_t wifiNet;

    if (net < wifiNets.size())
    {
        // User selected network from within scanned range
        wifiNet = *std::next(wifiNets.begin(), net);
        ssid = wifiNet.ssid.c_str();
    }
    else
    {
        // Outside scanned range, do not allow locking to access point
        advanced = false;
    }

    if (advanced)
    {
        RINFO("Requested WiFi SSID: %s (%d) at AP: %02x:%02x:%02x:%02x:%02x:%02x",
              ssid, net, wifiNet.bssid[0], wifiNet.bssid[1], wifiNet.bssid[2], wifiNet.bssid[3], wifiNet.bssid[4], wifiNet.bssid[5]);
        snprintf_P(json, JSON_BUFFER_SIZE, PSTR("Setting SSID to: %s locked to Access Point: %02x:%02x:%02x:%02x:%02x:%02x\nRATGDO rebooting.\nPlease wait 30 seconds and connect to RATGDO on new network."),
                   ssid, wifiNet.bssid[0], wifiNet.bssid[1], wifiNet.bssid[2], wifiNet.bssid[3], wifiNet.bssid[4], wifiNet.bssid[5]);
    }
    else
    {
        RINFO("Requested WiFi SSID: %s (%d)", ssid);
        snprintf_P(json, JSON_BUFFER_SIZE, PSTR("Setting SSID to: %s\nRATGDO rebooting.\nPlease wait 30 seconds and connect to RATGDO on new network."), ssid);
    }
    server.client().setNoDelay(true);
    server.send_P(200, type_txt, json);
    delay(500);
    server.stop();

    const bool connected = WiFi.isConnected();
    String previousSSID;
    String previousPSK;
    String previousBSSID;
    if (connected)
    {
        previousSSID = WiFi.SSID();
        previousPSK = WiFi.psk();
        previousBSSID = WiFi.BSSIDstr();
        RINFO("Current SSID: %s / BSSID:%s", previousSSID.c_str(), previousBSSID.c_str());
        WiFi.disconnect();
    }

    if (connect_wifi(ssid, pw, (advanced) ? wifiNet.bssid : NULL))
    {
        RINFO("WiFi Successfully connects to SSID: %s", ssid);
        // We should reset WiFi if changing networks or were not currently connected.
        if (!connected || previousBSSID != ssid)
        {
            userConfig->staticIP = false;
            userConfig->wifiPower = 20;
            userConfig->wifiPhyMode = 0;
#ifdef NTP_CLIENT
            userConfig->timeZone[0] = 0;
#endif
        }
    }
    else
    {
        RINFO("WiFi Failed to connect to SSID: %s", ssid);
        if (connected)
        {
            RINFO("Resetting WiFi to previous SSID: %s, removing any Access Point BSSID lock", previousSSID.c_str());
            connect_wifi(previousSSID.c_str(), previousPSK.c_str());
        }
        else
        {
            // We were not connected, and we failed to connext to new SSID,
            // so best to reset any wifi settings.
            userConfig->staticIP = false;
            userConfig->wifiPower = 20;
            userConfig->wifiPhyMode = 0;
#ifdef NTP_CLIENT
            userConfig->timeZone[0] = 0;
#endif
        }
    }
    sync_and_restart();
    return;
}
