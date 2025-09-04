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
 * Mitchell Solomon... https://github.com/mitchjs
 *
 */

// C/C++ language includes
#include <string>
#include <tuple>
#include <unordered_map>
#include <time.h>

// ESP system includes
#include <Ticker.h>
#include <MD5Builder.h>
#include <StreamString.h>
#ifdef ESP8266
#include <arduino_homekit_server.h>
#include <eboot_command.h>
#else
#include "esp_core_dump.h"
#endif

// RATGDO project includes
#include "ratgdo.h"
#include "config.h"
#include "comms.h"
#include "web.h"
#include "homekit.h"
#include "softAP.h"
#include "json.h"
#include "led.h"
#ifdef ESP8266
#include "wifi_8266.h"
#include "EspSaveCrash.h"
#else
#include "vehicle.h"
#endif
#include "www/build/webcontent.h"

// Logger tag
static const char *TAG = "ratgdo-http";

// Browser cache control, time in seconds after which browser cache invalid
// This is used for CSS, JS and IMAGE file types.  Set to 30 days !!
#define CACHE_CONTROL (60 * 60 * 24 * 30)

// Forward declare the internal URI handling functions...
void handle_reset();
void handle_status();
void handle_everything();
void handle_setgdo();
void handle_logout();
void handle_auth();
void handle_subscribe();
void handle_showlog();
void handle_showrebootlog();
void handle_crashlog();
void handle_clearcrashlog();
#ifdef CRASH_DEBUG
void handle_forcecrash();
void handle_crash_oom();
void *crashptr;
char *test_str = NULL;
#endif
void handle_update();
void handle_firmware_upload();
void SSEHandler(uint32_t channel);

#ifdef ESP8266
EspSaveCrash saveCrash(1408, 1024, true, &crashCallback);
#endif

// Built in URI handlers
const char restEvents[] = "/rest/events/";
const std::unordered_map<std::string, std::pair<const HTTPMethod, void (*)()>> builtInUri = {
    {"/status.json", {HTTP_GET, handle_status}},
    {"/reset", {HTTP_POST, handle_reset}},
    {"/reboot", {HTTP_POST, handle_reboot}},
    {"/setgdo", {HTTP_POST, handle_setgdo}},
    {"/logout", {HTTP_GET, handle_logout}},
    {"/auth", {HTTP_GET, handle_auth}},
    {"/showlog", {HTTP_GET, handle_showlog}},
    {"/showrebootlog", {HTTP_GET, handle_showrebootlog}},
    {"/wifiap", {HTTP_POST, handle_wifiap}},
    {"/wifinets", {HTTP_GET, handle_wifinets}},
    {"/setssid", {HTTP_POST, handle_setssid}},
    {"/rescan", {HTTP_POST, handle_rescan}},
    {"/crashlog", {HTTP_GET, handle_crashlog}},
    {"/clearcrashlog", {HTTP_GET, handle_clearcrashlog}},
#ifdef CRASH_DEBUG
    {"/forcecrash", {HTTP_POST, handle_forcecrash}},
    {"/crashoom", {HTTP_POST, handle_crash_oom}},
#endif
    {"/rest/events/subscribe", {HTTP_GET, handle_subscribe}}};

// Declare web server on HTTP port 80.
#ifdef ESP8266
ESP8266WebServer server(80);
#else
WebServer server(80);
#endif

// Local copy of door status
GarageDoor last_reported_garage_door;
bool last_reported_paired = false;
bool last_reported_assist_laser = false;
_millis_t lastDoorUpdateAt;
GarageDoorCurrentState lastDoorState = (GarageDoorCurrentState)0xff;

static bool web_setup_done = false;

// Implement our own firmware update so can enforce MD5 check.
// Based on ESP8266HTTPUpdateServer
std::string _updaterError;
bool _authenticatedUpdate;
char firmwareMD5[36] = "";
size_t firmwareSize = 0;

// Common HTTP responses
const char response400missing[] = "400: Bad Request, missing argument\n";
const char response400invalid[] = "400: Bad Request, invalid argument\n";
const char response404[] = "404: Not Found\n";
const char response503[] = "503: Service Unavailable.\n";
const char response200[] = "HTTP/1.1 200 OK\nContent-Type: text/plain\nConnection: close\n\n";

const char *http_methods[] = {"HTTP_ANY", "HTTP_GET", "HTTP_HEAD", "HTTP_POST", "HTTP_PUT", "HTTP_PATCH", "HTTP_DELETE", "HTTP_OPTIONS"};

// For Server Sent Events (SSE) support
// Just reloading page causes register on new channel.  So we need a reasonable number
// to accommodate "extra" until old one is detected as disconnected.
#define SSE_MAX_CHANNELS 8
struct SSESubscription
{
    IPAddress clientIP;
    WiFiClient client;
    Ticker heartbeatTimer;
    uint32_t heartbeatInterval;
    bool SSEconnected;
    int SSEfailCount;
    String clientUUID;
    bool logViewer;
};
SSESubscription subscription[SSE_MAX_CHANNELS];
// During firmware update note which subscribed client is updating
SSESubscription *firmwareUpdateSub = NULL;
uint32_t subscriptionCount = 0;

// Performance management - removed redundant connection tracking
#define MIN_REQUEST_INTERVAL_MS 100

// Performance monitoring
static uint32_t request_count = 0;
static uint32_t dropped_connections = 0;

// JSON response caching
#ifdef ESP8266
#define STATUS_JSON_BUFFER_SIZE (256 * 7)
#else
#define STATUS_JSON_BUFFER_SIZE (256 * 8)
#endif
// #define STATUS_JSON_CACHE_TIMEOUT_MS 500
#define LOOP_JSON_BUFFER_SIZE 512

static char *status_json = NULL;
static char *loop_json = NULL;
#ifdef ESP8266
// ESP8266 is single core / single threaded, no mutex's.
#define TAKE_MUTEX()
#define GIVE_MUTEX()
#else
// ESP32 is multi-core, need to serialize access to JSON buffers
static SemaphoreHandle_t jsonMutex = NULL;
#define TAKE_MUTEX() xSemaphoreTake(jsonMutex, portMAX_DELAY)
#define GIVE_MUTEX() xSemaphoreGive(jsonMutex)
#endif

#define DOOR_STATE(s) (s == 0) ? "Open" : (s == 1) ? "Closed"  \
                                      : (s == 2)   ? "Opening" \
                                      : (s == 3)   ? "Closing" \
                                      : (s == 4)   ? "Stopped" \
                                                   : "Unknown"
#define LOCK_STATE(s) (s == 0) ? "Enabled" : (s == 1) ? "Disabled" \
                                           : (s == 2)   ? "Jammed"  \
                                                        : "Unknown"

// Connection throttling
#define MAX_CONCURRENT_REQUESTS 8
#define REQUEST_TIMEOUT_MS 2000
struct ActiveRequest
{
    IPAddress clientIP;
    _millis_t startTime;
    bool inUse;
};
ActiveRequest activeRequests[MAX_CONCURRENT_REQUESTS];
int activeRequestCount = 0;

#define CLIENT_WRITE_TIMEOUT 500
static char writeBuffer[512];
bool clientWrite(WiFiClient client, const char *data)
{
    size_t len = strlen(data);
    size_t written = 0;
#ifdef ESP8266
    client.flush(); // make sure previous data all sent.
#endif
    written = client.write(data, len);
    if (written == 0)
    {
        YIELD();
        client.stop();
        ESP_LOGW(TAG, "Failed writing to WiFi Client (%d of %d), connection closed.", written, len);
        return false;
    }
    return true;
}

// Helper functions for connection throttling
bool registerRequest()
{
    IPAddress clientIP = server.client().remoteIP();
    _millis_t now = _millis();

    // Clean up timed-out requests
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++)
    {
        if (activeRequests[i].inUse && (now - activeRequests[i].startTime > REQUEST_TIMEOUT_MS))
        {
            ESP_LOGD(TAG, "Request timeout for client %s", activeRequests[i].clientIP.toString().c_str());
            activeRequests[i].inUse = false;
            activeRequestCount--;
        }
    }

    // Check if we're at capacity
    if (activeRequestCount >= MAX_CONCURRENT_REQUESTS)
    {
        ESP_LOGI(TAG, "Max concurrent requests reached, rejecting %s", clientIP.toString().c_str());
        return false;
    }

    // Find a free slot
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++)
    {
        if (!activeRequests[i].inUse)
        {
            activeRequests[i].clientIP = clientIP;
            activeRequests[i].startTime = now;
            activeRequests[i].inUse = true;
            activeRequestCount++;
            return true;
        }
    }

    return false;
}

void unregisterRequest()
{
    IPAddress clientIP = server.client().remoteIP();

    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++)
    {
        if (activeRequests[i].inUse && activeRequests[i].clientIP == clientIP)
        {
            activeRequests[i].inUse = false;
            if (activeRequestCount > 0)
                activeRequestCount--; // Prevent negative count
            break;
        }
    }
}

void web_loop()
{
    if (!web_setup_done)
        return;

    static char *json = loop_json;
    _millis_t upTime = _millis();
    static _millis_t last_request_time = 0;
    TAKE_MUTEX();
    JSON_START(json);
    if (garage_door.active && garage_door.current_state != lastDoorState)
    {
        ESP_LOGI(TAG, "Current Door State changing from %s to %s", DOOR_STATE(lastDoorState), DOOR_STATE(garage_door.current_state));
        if (enableNTP && clockSet)
        {
            if (lastDoorState == 0xff)
            {
                // initialize with saved time.
                // lastDoorUpdateAt is milliseconds relative to system reboot time.
                lastDoorUpdateAt = (userConfig->getDoorUpdateAt() != 0) ? ((userConfig->getDoorUpdateAt() - time(NULL)) * 1000) + upTime : 0;
            }
            else
            {
                // first state change after a reboot, so really is a state change.
                userConfig->set(cfg_doorUpdateAt, (int)time(NULL));
                ESP8266_SAVE_CONFIG();
                lastDoorUpdateAt = upTime;
            }
        }
        else
        {
            lastDoorUpdateAt = (lastDoorState == 0xff) ? 0 : upTime;
        }
        // if no NTP....  lastDoorUpdateAt = (lastDoorState == 0xff) ? 0 : upTime;
        lastDoorState = garage_door.current_state;
        // We send milliseconds relative to current time... ie updated X milliseconds ago
        // First time through, zero offset from upTime, which is when we last rebooted)
        JSON_ADD_INT("lastDoorUpdateAt", (upTime - lastDoorUpdateAt));
    }
#ifndef ESP8266
    // Feature not available on ESP8266
    if (garage_door.has_distance_sensor)
    {
        if (vehicleStatusChange)
        {
            vehicleStatusChange = false;
            JSON_ADD_STR("vehicleStatus", vehicleStatus);
        }
        JSON_ADD_BOOL_C("assistLaser", laser.state(), last_reported_assist_laser);
    }
#endif
    // Conditional macros, only add if value has changed
    JSON_ADD_BOOL_C("paired", homekit_is_paired(), last_reported_paired);
    JSON_ADD_STR_C("garageDoorState", DOOR_STATE(garage_door.current_state), garage_door.current_state, last_reported_garage_door.current_state);
    JSON_ADD_STR_C("garageLockState", LOCK_STATE(garage_door.current_lock), garage_door.current_lock, last_reported_garage_door.current_lock);
    JSON_ADD_BOOL_C("garageLightOn", garage_door.light, last_reported_garage_door.light);
    JSON_ADD_BOOL_C("garageMotion", garage_door.motion, last_reported_garage_door.motion);
    JSON_ADD_BOOL_C("garageObstructed", garage_door.obstructed, last_reported_garage_door.obstructed);
    JSON_ADD_BOOL_C("garageSec1Emulated", garage_door.wallPanelEmulated, last_reported_garage_door.wallPanelEmulated);
    if (doorControlType == 2)
    {
        JSON_ADD_INT_C("batteryState", garage_door.batteryState, last_reported_garage_door.batteryState);
        JSON_ADD_INT_C("openingsCount", garage_door.openingsCount, last_reported_garage_door.openingsCount);
    }
    JSON_ADD_INT_C("openDuration", garage_door.openDuration, last_reported_garage_door.openDuration);
    JSON_ADD_INT_C("closeDuration", garage_door.closeDuration, last_reported_garage_door.closeDuration);
    if (strlen(json) > 2)
    {
        // Have we added anything to the JSON string?
        JSON_ADD_INT("upTime", upTime);
        JSON_END();
        if (strlen(json) > LOOP_JSON_BUFFER_SIZE * 8 / 10)
        {
            ESP_LOGW(TAG, "WARNING web_loop JSON length: %d is over 80%% of available buffer", strlen(json));
        }
        JSON_REMOVE_NL(json);
        SSEBroadcastState(json);
    }
    GIVE_MUTEX();

    // Rate limiting - minimum interval between requests
    _millis_t current_time = _millis();
    if (current_time - last_request_time < MIN_REQUEST_INTERVAL_MS)
    {
        return; // Skip this cycle to enforce rate limit
    }

    server.handleClient();
    // Update last request time after handling client
    last_request_time = current_time;
}

void setup_web()
{
    if (web_setup_done)
        return;

    ESP_LOGI(TAG, "=== Starting HTTP web server ===");
    IRAM_START(TAG);
    // IRAM heap is used only for allocating globals, to leave as much regular heap
    // available during operations.  We need to carefully monitor useage so as not
    // to exceed available IRAM.  We can adjust the LOG_BUFFER_SIZE (in log.h) if we
    // need to make more space available for initialization.
    status_json = (char *)malloc(STATUS_JSON_BUFFER_SIZE);
    if (!status_json)
    {
        ESP_LOGE(TAG, "Failed to allocated buffer for status JSON, size: %d", STATUS_JSON_BUFFER_SIZE);
        return;
    }
    ESP_LOGI(TAG, "Allocated buffer for status JSON, size: %d", STATUS_JSON_BUFFER_SIZE);
    loop_json = (char *)malloc(LOOP_JSON_BUFFER_SIZE);
    if (!loop_json)
    {
        ESP_LOGE(TAG, "Failed to allocated buffer for loop JSON, size: %d", LOOP_JSON_BUFFER_SIZE);
        return;
    }
    ESP_LOGI(TAG, "Allocated buffer for loop JSON, size: %d", LOOP_JSON_BUFFER_SIZE);
#ifndef ESP8266
    // We allocated json as a global block.  We are on dual core CPU.  We need to serialize access to the resource.
    jsonMutex = xSemaphoreCreateMutex();
#endif
    last_reported_paired = homekit_is_paired();

    if (motionTriggers.asInt == 0)
    {
        // maybe just initialized. If we have motion sensor then set that and write back to file
        if (garage_door.has_motion_sensor)
        {
            motionTriggers.bit.motion = 1;
            userConfig->set(cfg_motionTriggers, motionTriggers.asInt);
            ESP8266_SAVE_CONFIG();
        }
    }
    else if (garage_door.has_motion_sensor != (bool)motionTriggers.bit.motion)
    {
        // sync up web page tracking of whether we have motion sensor or not.
        ESP_LOGI(TAG, "Motion trigger mismatch, reset to %d", (int)garage_door.has_motion_sensor);
        motionTriggers.bit.motion = (uint8_t)garage_door.has_motion_sensor;
        userConfig->set(cfg_motionTriggers, motionTriggers.asInt);
        ESP8266_SAVE_CONFIG();
    }
    ESP_LOGI(TAG, "Motion triggers, motion : %d, obstruction: %d, light key: %d, door key: %d, lock key: %d, asInt: %d",
             motionTriggers.bit.motion,
             motionTriggers.bit.obstruction,
             motionTriggers.bit.lightKey,
             motionTriggers.bit.doorKey,
             motionTriggers.bit.lockKey,
             motionTriggers.asInt);
    lastDoorUpdateAt = 0;
    lastDoorState = (GarageDoorCurrentState)0xff;

#ifdef ESP8266 // ESP8266 only
    crashCount = saveCrash.count();
    if (crashCount == 255)
    {
        saveCrash.clear();
        crashCount = 0;
    }
#endif

    ESP_LOGI(TAG, "Registering URI handlers");
    server.on("/update", HTTP_POST, handle_update, handle_firmware_upload);
    server.onNotFound(handle_everything);
    // here the list of headers to be recorded
    const char *headerkeys[] = {"If-None-Match"};
    size_t headerkeyssize = sizeof(headerkeys) / sizeof(char *);
    // ask server to track these headers
    server.collectHeaders(headerkeys, headerkeyssize);
    server.begin();
    // initialize all the Server-Sent Events (SSE) slots.
    for (uint32_t i = 0; i < SSE_MAX_CHANNELS; i++)
    {
        subscription[i].SSEconnected = false;
        subscription[i].clientIP = INADDR_NONE;
        subscription[i].clientUUID.clear();
    }

    // Initialize connection tracking
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++)
    {
        activeRequests[i].inUse = false;
    }
    activeRequestCount = 0;

    IRAM_END(TAG);
    web_setup_done = true;
    return;
}

void handle_notfound()
{
    ESP_LOGI(TAG, "Sending 404 Not Found for: %s with method: %s to client: %s", server.uri().c_str(), http_methods[server.method()], server.client().remoteIP().toString().c_str());
    server.send_P(404, type_txt, response404);
    return;
}

#ifdef ESP8266
#define AUTHENTICATE()                                                                                                                  \
    if (userConfig->getPasswordRequired() && !server.authenticateDigest(userConfig->getwwwUsername(), userConfig->getwwwCredentials())) \
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
#else
String *ratgdoAuthenticate(HTTPAuthMethod mode, String enteredUsernameOrReq, String extraParams[])
{
    // ESP_LOGI(TAG, "Auth method: %d", mode);                // DIGEST_AUTH
    // ESP_LOGI(TAG, "User: %s", enteredUsernameOrReq);       // Username
    // ESP_LOGI(TAG, "Param 0: %s", extraParams[0].c_str());  // Realm
    // ESP_LOGI(TAG, "Param 1: %s", extraParams[1].c_str());  // URI
    String *pw = new String(nvRam->read(nvram_ratgdo_pw, "password").c_str());
    return pw;
}

#define AUTHENTICATE()                                                                 \
    if (userConfig->getPasswordRequired() && !server.authenticate(ratgdoAuthenticate)) \
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
#endif

void handle_auth()
{
    AUTHENTICATE();
    server.send_P(200, type_txt, PSTR("Authenticated"));
    return;
}

void handle_reset()
{
    AUTHENTICATE();
    ESP_LOGI(TAG, "... reset requested");
#ifdef ESP8266
    homekit_storage_reset();
#else
    homekit_unpair();
#endif
    server.client().setNoDelay(true);
    server.send_P(200, type_txt, PSTR("Device has been un-paired from HomeKit. Rebooting...\n"));
    ESP_LOGI(TAG, "System boot time:   %s", timeString(lastRebootAt));
    ESP_LOGI(TAG, "Un-pair restart at: %s", timeString());
    // Allow time to process send() before terminating web server...
    delay(500);
    server.stop();
    sync_and_restart();
    return;
}

void handle_reboot()
{
    ESP_LOGI(TAG, "System boot time:    %s", timeString(lastRebootAt));
    ESP_LOGI(TAG, "Reboot requested at: %s", timeString());
    const char *resp = "Rebooting...\n";
    server.client().setNoDelay(true);
    server.send(200, type_txt, resp);
    // Allow time to process send() before terminating web server...
    delay(500);
    server.stop();
    sync_and_restart();
    return;
}

void load_page(const char *page)
{
    if (webcontent.count(page) == 0)
        return handle_notfound();

    const char *data = (char *)webcontent.at(page).data;
    int length = webcontent.at(page).length;
    const char *typeP = webcontent.at(page).type;
    const char *crc32 = webcontent.at(page).crc32.c_str();
    // need local copy as strcmp_P cannot take two PSTR()'s
    char type[MAX_MIME_TYPE_LEN];
    strncpy_P(type, typeP, MAX_MIME_TYPE_LEN);

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
        server.sendHeader(F("Content-Encoding"), F("gzip"));
        server.sendHeader(F("Cache-Control"), cacheHdr);
        if (cache)
            server.sendHeader(F("ETag"), crc32);
        if (method == HTTP_HEAD)
        {
            ESP_LOGI(TAG, "Client %s requesting: %s (HTTP_HEAD, type: %s)", server.client().remoteIP().toString().c_str(), page, type);
            server.send_P(200, type, "", 0);
        }
        else
        {
            ESP_LOGI(TAG, "Client %s requesting: %s (HTTP_GET, type: %s, length: %i)", server.client().remoteIP().toString().c_str(), page, type, length);
            server.send_P(200, type, data, length);
        }
    }
    else
    {
        ESP_LOGI(TAG, "Sending 304 not modified to client %s requesting: %s (method: %s, type: %s)", server.client().remoteIP().toString().c_str(), page, http_methods[method], type);
        server.send_P(304, type, "", 0);
    }
    return;
}

void handle_everything()
{
    // Connection throttling
    if (!registerRequest())
    {
        server.send(503, type_txt, response503);
        ESP_LOGW(TAG, "Reject request, server too busy (handle_everything)");
        return;
    }

    HTTPMethod method = server.method();
    String page = server.uri();
    const char *uri = page.c_str();

    // too verbose... ESP_LOGI(TAG, "Handle everything for %s", uri);
    if (builtInUri.count(uri) > 0)
    {
        // requested page matches one of our built-in handlers
        ESP_LOGI(TAG, "Client %s requesting: %s (method: %s)", server.client().remoteIP().toString().c_str(), uri, http_methods[method]);
        if (method == builtInUri.at(uri).first)
        {
            builtInUri.at(uri).second();
        }
        else
        {
            handle_notfound();
        }
        unregisterRequest();
        return;
    }
    else if ((method == HTTP_GET) && (!strncmp_P(uri, restEvents, strlen(restEvents))))
    {
        // Request for "/rest/events/" with a channel number appended
        uri += strlen(restEvents);
        uint32_t channel = atoi(uri);
        if (channel < SSE_MAX_CHANNELS)
        {
            SSEHandler(channel);
        }
        else
        {
            handle_notfound();
        }
        unregisterRequest();
        return;
    }
    else if (method == HTTP_GET || method == HTTP_HEAD)
    {
        // HTTP_GET that does not match a built-in handler
        if (page == "/")
        {
            load_page("/index.html");
        }
        else
        {
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
    static uint32_t max_response_time = 0;
    _millis_t startTime = _millis();
    _millis_t upTime = startTime;
    uint32_t response_time;
    uint32_t build_time;
    static char *json = status_json;

    TAKE_MUTEX();
    request_count++;

    // Build the JSON string
    JSON_START(json);
#ifdef ESP8266
    JSON_ADD_STR("gitRepo", "homekit-ratgdo");
#else
    JSON_ADD_STR("gitRepo", "homekit-ratgdo32");
#endif
    JSON_ADD_INT("upTime", upTime);
    JSON_ADD_STR(cfg_deviceName, userConfig->getDeviceName());
    JSON_ADD_STR("userName", userConfig->getwwwUsername());
    JSON_ADD_BOOL("paired", homekit_is_paired());
    JSON_ADD_STR("firmwareVersion", std::string(AUTO_VERSION).c_str());
    JSON_ADD_STR(cfg_localIP, userConfig->getLocalIP());
    JSON_ADD_STR(cfg_subnetMask, userConfig->getSubnetMask());
    JSON_ADD_STR(cfg_gatewayIP, userConfig->getGatewayIP());
    JSON_ADD_STR(cfg_nameserverIP, userConfig->getNameserverIP());
    JSON_ADD_STR("macAddress", WiFi.macAddress().c_str());
    JSON_ADD_STR("wifiSSID", WiFi.SSID().c_str());
    JSON_ADD_STR("wifiRSSI", (std::to_string(WiFi.RSSI()) + " dBm, Channel " + std::to_string(WiFi.channel())).c_str());
    JSON_ADD_STR("wifiBSSID", WiFi.BSSIDstr().c_str());
#ifdef ESP8266
    JSON_ADD_BOOL("lockedAP", wifiConf.bssid_set);
#else
    JSON_ADD_BOOL("lockedAP", false);
#endif
    JSON_ADD_INT("wifiPower", userConfig->getWifiPower());
    JSON_ADD_INT(cfg_GDOSecurityType, (uint32_t)userConfig->getGDOSecurityType());
    JSON_ADD_BOOL("garageSec1Emulated", garage_door.wallPanelEmulated);
    JSON_ADD_BOOL(cfg_useToggleToClose, userConfig->getUseToggleToClose());
    JSON_ADD_STR("garageDoorState", garage_door.active ? DOOR_STATE(garage_door.current_state) : DOOR_STATE(255));
    JSON_ADD_STR("garageLockState", LOCK_STATE(garage_door.current_lock));
    JSON_ADD_BOOL("garageLightOn", garage_door.light);
    JSON_ADD_BOOL("garageMotion", garage_door.motion);
    JSON_ADD_BOOL("garageObstructed", garage_door.obstructed);
    JSON_ADD_BOOL(cfg_passwordRequired, userConfig->getPasswordRequired());
    JSON_ADD_INT(cfg_rebootSeconds, (uint32_t)userConfig->getRebootSeconds());
    JSON_ADD_INT("freeHeap", free_heap);
    JSON_ADD_INT("minHeap", min_heap);
    JSON_ADD_INT("crashCount", abs(crashCount));
    JSON_ADD_BOOL(cfg_staticIP, userConfig->getStaticIP());
    JSON_ADD_BOOL(cfg_syslogEn, userConfig->getSyslogEn());
    JSON_ADD_STR(cfg_syslogIP, userConfig->getSyslogIP());
    JSON_ADD_INT(cfg_syslogPort, userConfig->getSyslogPort());
    JSON_ADD_INT(cfg_logLevel, userConfig->getLogLevel());
    JSON_ADD_INT(cfg_TTCseconds, userConfig->getTTCseconds());
    JSON_ADD_BOOL(cfg_TTClight, userConfig->getTTClight());
    JSON_ADD_INT(cfg_motionTriggers, (uint32_t)motionTriggers.asInt);
    JSON_ADD_INT(cfg_LEDidle, userConfig->getLEDidle());
    // We send milliseconds relative to current time... ie updated X milliseconds ago
    JSON_ADD_INT("lastDoorUpdateAt", (upTime - lastDoorUpdateAt));
    JSON_ADD_BOOL("enableNTP", enableNTP);
    if (enableNTP && (bool)clockSet)
    {
        JSON_ADD_INT("serverTime", time(NULL));
    }
    JSON_ADD_STR(cfg_timeZone, userConfig->getTimeZone());
    JSON_ADD_BOOL(cfg_dcOpenClose, userConfig->getDCOpenClose());
    JSON_ADD_BOOL(cfg_obstFromStatus, userConfig->getObstFromStatus());
    JSON_ADD_INT(cfg_dcDebounceDuration, userConfig->getDCDebounceDuration());
    JSON_ADD_STR("qrPayload", qrPayload);
    if (doorControlType == 2)
    {
        JSON_ADD_INT("batteryState", garage_door.batteryState);
        JSON_ADD_INT("openingsCount", garage_door.openingsCount);
    }
    if (garage_door.openDuration)
    {
        JSON_ADD_INT("openDuration", garage_door.openDuration);
    }
    if (garage_door.closeDuration)
    {
        JSON_ADD_INT("closeDuration", garage_door.closeDuration);
    }
#ifdef ESP8266
#define accessoryID arduino_homekit_get_running_server() ? arduino_homekit_get_running_server()->accessory_id : "Inactive"
#define clientCount arduino_homekit_get_running_server() ? arduino_homekit_get_running_server()->nfds : 0
    JSON_ADD_STR("accessoryID", accessoryID);
    JSON_ADD_INT("clients", clientCount);
    JSON_ADD_BOOL("lockedAP", wifiConf.bssid_set);
    JSON_ADD_INT("wifiPhyMode", userConfig->getWifiPhyMode());
    JSON_ADD_INT("minStack", ESP.getFreeContStack());
#else
    JSON_ADD_INT(cfg_occupancyDuration, userConfig->getOccupancyDuration());
    JSON_ADD_BOOL(cfg_enableIPv6, userConfig->getEnableIPv6());
    JSON_ADD_STR("ipv6Addresses", ipv6_addresses);
    JSON_ADD_BOOL(cfg_builtInTTC, userConfig->getBuiltInTTC());
#ifdef USE_GDOLIB
    JSON_ADD_BOOL(cfg_useSWserial, userConfig->getUseSWserial());
#endif
    JSON_ADD_BOOL("distanceSensor", garage_door.has_distance_sensor);
    if (garage_door.has_distance_sensor)
    {
        JSON_ADD_STR("vehicleStatus", vehicleStatus);
        JSON_ADD_INT("vehicleDist", (uint32_t)vehicleDistance);
        last_reported_assist_laser = laser.state();
        JSON_ADD_BOOL("assistLaser", last_reported_assist_laser);
    }
    JSON_ADD_BOOL(cfg_vehicleHomeKit, userConfig->getVehicleHomeKit());
    JSON_ADD_INT(cfg_vehicleThreshold, userConfig->getVehicleThreshold());
    JSON_ADD_BOOL(cfg_laserEnabled, userConfig->getLaserEnabled());
    JSON_ADD_BOOL(cfg_laserHomeKit, userConfig->getLaserHomeKit());
    JSON_ADD_INT(cfg_assistDuration, userConfig->getAssistDuration());
#endif
    JSON_ADD_INT("webRequests", request_count);
    JSON_ADD_INT("webDroppedConns", dropped_connections);
    JSON_ADD_INT("webMaxResponseTime", max_response_time);
    JSON_END();
    build_time = (uint32_t)(_millis() - startTime);

    last_reported_garage_door = garage_door;
    server.sendHeader(F("Cache-Control"), F("no-cache, no-store"));
    server.send_P(200, type_json, json);
    response_time = _millis() - startTime;
    max_response_time = std::max(max_response_time, response_time);
    if (strlen(json) > STATUS_JSON_BUFFER_SIZE * 85 / 100)
    {
        ESP_LOGW(TAG, "WARNING status JSON length: %d is over 85%% of available buffer, build time %lums, response time: %lums", strlen(json), build_time, response_time);
    }
    else
    {
        ESP_LOGI(TAG, "JSON length: %d, build time %lums, response time: %lums", strlen(json), build_time, response_time);
    }
    GIVE_MUTEX();
    return;
}

void handle_logout()
{
    ESP_LOGI(TAG, "Handle logout");
    return server.requestAuthentication(DIGEST_AUTH, www_realm);
}

bool helperResetDoor(const std::string &key, const char *value, configSetting *action)
{
    reset_door();
    return true;
}

bool helperGarageLightOn(const std::string &key, const char *value, configSetting *action)
{
    set_light((atoi(value) == 1) ? true : false);
    return true;
}

bool helperGarageDoorState(const std::string &key, const char *value, configSetting *action)
{
    if (atoi(value) == 1)
        open_door();
    else
        close_door();
    return true;
}

bool helperGarageLockState(const std::string &key, const char *value, configSetting *action)
{
    set_lock((atoi(value) == 1) ? 1 : 0);
    return true;
}

bool helperCredentials(const std::string &key, const char *value, configSetting *action)
{
    char *newUsername = strstr(value, "username");
    char *newCredentials = strstr(value, "credentials");
    char *newPassword = strstr(value, "password");
    if (!(newUsername && newCredentials && newPassword))
        return false;

    // JSON string passed in.
    // Very basic parsing, not using library functions to save memory
    // find the colon after the key string
    newUsername = strchr(newUsername, ':') + 1;
    newCredentials = strchr(newCredentials, ':') + 1;
    newPassword = strchr(newPassword, ':') + 1;
    // for strings find the double quote
    newUsername = strchr(newUsername, '"') + 1;
    newCredentials = strchr(newCredentials, '"') + 1;
    newPassword = strchr(newPassword, '"') + 1;
    // null terminate the strings (at closing quote).
    *strchr(newUsername, '"') = (char)0;
    *strchr(newCredentials, '"') = (char)0;
    *strchr(newPassword, '"') = (char)0;
    // save values...
    ESP_LOGI(TAG, "Set user credentials: %s : %s (%s)", newUsername, newPassword, newCredentials);
    userConfig->set(cfg_wwwUsername, newUsername);
    userConfig->set(cfg_wwwCredentials, newCredentials);
#ifndef ESP8266
    // We only need to save password (distinct from credentials) on ESP32
    nvRam->write(nvram_ratgdo_pw, newPassword);
#endif
    ESP8266_SAVE_CONFIG();
    return true;
}

bool helperUpdateUnderway(const std::string &key, const char *value, configSetting *action)
{
    firmwareSize = 0;
    firmwareUpdateSub = NULL;
    char *md5 = strstr(value, "md5");
    char *size = strstr(value, "size");
    char *uuid = strstr(value, "uuid");

    if (!(md5 && size && uuid))
        return false;

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
    // ESP_LOGI(TAG,"MD5: %s, UUID: %s, Size: %d", md5, uuid, atoi(size));
    // save values...
    strlcpy(firmwareMD5, md5, sizeof(firmwareMD5));
    firmwareSize = atoi(size);
    for (uint32_t channel = 0; channel < SSE_MAX_CHANNELS; channel++)
    {
        if (subscription[channel].SSEconnected && subscription[channel].clientUUID == uuid && subscription[channel].client.connected())
        {
            firmwareUpdateSub = &subscription[channel];
            break;
        }
    }
    return true;
}

bool helperFactoryReset(const std::string &key, const char *value, configSetting *action)
{
    ESP_LOGI(TAG, "System boot time: %s", timeString(lastRebootAt));
    ESP_LOGI(TAG, "Factory reset at: %s", timeString());
#ifdef ESP8266
    userConfig->erase();
    reset_door();
    sync_and_restart();
#else
    nvRam->erase();
    reset_door();
    homeSpan.processSerialCommand("F");
#endif
    return true;
}

#ifndef ESP8266
bool helperAssistLaser(const std::string &key, const char *value, configSetting *action)
{
    if (atoi(value) == 1)
        laser.on();
    else
        laser.off();
    notify_homekit_laser(atoi(value) == 1);
    return true;
}
#endif

void handle_setgdo()
{
    // Build-in handlers that do not set a configuration value, or if they do they set multiple values.
    // key, {reboot, wifiChanged, value, fn to call}
    static const std::unordered_map<std::string, configSetting> setGDOhandlers = {
        {"resetDoor", {true, false, 0, helperResetDoor}},
        {"garageLightOn", {false, false, 0, helperGarageLightOn}},
        {"garageDoorState", {false, false, 0, helperGarageDoorState}},
        {"garageLockState", {false, false, 0, helperGarageLockState}},
        {"credentials", {false, false, 0, helperCredentials}}, // parse out wwwUsername and credentials
        {"updateUnderway", {false, false, 0, helperUpdateUnderway}},
        {"factoryReset", {true, false, 0, helperFactoryReset}},
#ifndef ESP8266
        {"assistLaser", {false, false, 0, helperAssistLaser}},
#endif
    };
    bool reboot = false;
    bool error = false;
    bool wifiChanged = false;
    bool saveSettings = false;
    std::string key;
    std::string value;
    configSetting actions;

    if (!((server.args() == 1) && (server.argName(0) == cfg_timeZone)))
    {
        // We will allow setting of time zone without authentication
        AUTHENTICATE();
    }

    // Loop over all the GDO settings passed in...
    for (int i = 0; i < server.args(); i++)
    {
        key = server.argName(i).c_str();
        value = server.arg(i).c_str();

        if (setGDOhandlers.count(key))
        {
            ESP_LOGI(TAG, "Call handler for Key: %s, Value: %s", key.c_str(), value.c_str());
            actions = setGDOhandlers.at(key);
            if (actions.fn)
            {
                error = error || !actions.fn(key, value.c_str(), &actions);
            }
            reboot = reboot || actions.reboot;
            wifiChanged = wifiChanged || actions.wifiChanged;
        }
        else if (userConfig->contains(key))
        {
            ESP_LOGI(TAG, "Configuration set for Key: %s, Value: %s", key.c_str(), value.c_str());
            actions = userConfig->getDetail(key);
            if (actions.fn)
            {
                // Value will be set within called function
                error = error || !actions.fn(key, value.c_str(), &actions);
            }
            else
            {
                // No function to call, set value directly.
                userConfig->set(key, value.c_str());
            }
            reboot = reboot || actions.reboot;
            wifiChanged = wifiChanged || actions.wifiChanged;
            saveSettings = true;
        }
        else
        {
            ESP_LOGW(TAG, "Invalid Key: %s, Value: %s (F)", key.c_str(), value.c_str());
            error = true;
        }
        YIELD(); // Yield while looping over all received settings, just-in-case!
        if (error)
            break;
    }

    ESP_LOGI(TAG, "SetGDO Complete");

    if (error)
    {
        // Simple error handling...
        ESP_LOGI(TAG, "Sending %s, for: %s", response400invalid, server.uri().c_str());
        server.send_P(400, type_txt, response400invalid);
        return;
    }

    if (saveSettings)
    {
        userConfig->set(cfg_wifiChanged, wifiChanged);
        ESP8266_SAVE_CONFIG();
    }
    if (reboot)
    {
        // Some settings require reboot to take effect
        server.send_P(200, type_html, PSTR("<p>Success. Reboot.</p>"));
        ESP_LOGI(TAG, "System boot time:  %s", timeString(lastRebootAt));
        ESP_LOGI(TAG, "SetGDO Restart at: %s", timeString());
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

void removeSSEsubscription(SSESubscription *s)
{
    if (subscriptionCount > 0)
        subscriptionCount--; // Prevent negative count
    s->heartbeatTimer.detach();
    ESP_LOGI(TAG, "Client %s (%s) not listening, remove SSE subscription. Total subscribed: %d", s->clientIP.toString().c_str(), s->clientUUID.c_str(), subscriptionCount);
    s->client.stop();
    s->clientIP = INADDR_NONE;
    s->clientUUID.clear();
    s->SSEconnected = false;
}

void SSEheartbeat(SSESubscription *s)
{
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
            removeSSEsubscription(s);
        }
        else
        {
            ESP_LOGI(TAG, "Client %s (%s) not yet listening for SSE", s->clientIP.toString().c_str(), s->clientUUID.c_str());
        }
        return;
    }

    if (s->client.connected())
    {
        static int8_t lastRSSI = 0;
        static int32_t lastVehicleDistance = 0;
        static char *json = loop_json;
        TAKE_MUTEX();
        JSON_START(json);
        JSON_ADD_INT("upTime", _millis());
        JSON_ADD_INT("freeHeap", free_heap);
        JSON_ADD_INT("minHeap", min_heap);
        // TODO monitor stack... JSON_ADD_INT("minStack", ESP.getFreeContStack());
#ifndef ESP8266
        if (garage_door.has_distance_sensor && (lastVehicleDistance != vehicleDistance))
        {
            lastVehicleDistance = vehicleDistance;
            JSON_ADD_INT("vehicleDist", (uint32_t)vehicleDistance);
        }
#endif
        if (lastRSSI != WiFi.RSSI())
        {
            lastRSSI = WiFi.RSSI();
            JSON_ADD_STR("wifiRSSI", (std::to_string(lastRSSI) + " dBm, Channel " + std::to_string(WiFi.channel())).c_str());
        }
#ifdef ESP8266
        static int lastClientCount = 0;
        if (arduino_homekit_get_running_server() && arduino_homekit_get_running_server()->nfds != lastClientCount)
        {
            lastClientCount = arduino_homekit_get_running_server()->nfds;
            JSON_ADD_INT("clients", lastClientCount);
        }
#endif
        JSON_END();
        JSON_REMOVE_NL(json);
        YIELD();
        // retry needed to before event:
        snprintf(writeBuffer, sizeof(writeBuffer), "retry: 15000\nevent: message\ndata: %s\n\n", json);
        clientWrite(s->client, writeBuffer);
        GIVE_MUTEX();
    }
    else
    {
        removeSSEsubscription(s);
        YIELD();
    }
}

void SSEHandler(uint32_t channel)
{
    if (server.args() != 1)
    {
        ESP_LOGI(TAG, "Sending %s, for: %s", response400missing, server.uri().c_str());
        server.send_P(400, type_txt, response400missing);
        return;
    }

    SSESubscription &s = subscription[channel];
    s.client = server.client(); // capture SSE server client connection
    if (s.clientUUID != server.arg(0))
    {
        ESP_LOGI(TAG, "Client %s (%s) tries to listen for SSE but not subscribed", s.client.remoteIP().toString().c_str(), server.arg(0).c_str());
        return handle_notfound();
    }
    s.client.setNoDelay(true);
    s.client.setTimeout(CLIENT_WRITE_TIMEOUT);       // default is 5000ms which is way too long (Watchdog will fire)
    server.setContentLength(CONTENT_LENGTH_UNKNOWN); // the payload can go on forever
    server.sendContent_P(PSTR("HTTP/1.1 200 OK\nContent-Type: text/event-stream;\nConnection: keep-alive\nCache-Control: no-cache\nAccess-Control-Allow-Origin: *\n\n"));
    s.SSEconnected = true;
    s.SSEfailCount = 0;
    if (s.heartbeatInterval)
    {
        s.heartbeatTimer.attach_ms(s.heartbeatInterval * 1000, [&s]
                                   {
#ifdef ESP8266
                                       schedule_recurrent_function_us([&s]()
                                                                      {
                                                                          SSEheartbeat(&s);
                                                                          return false; // run the fn only once
                                                                      },
                                                                      0); // zero micro seconds (run asap)
#else
                                       SSEheartbeat(&s);
                                       return;
#endif
                                   });
    }
    ESP_LOGI(TAG, "Client %s (%s) listening for SSE events on channel %d", s.client.remoteIP().toString().c_str(), s.clientUUID.c_str(), channel);
}

void handle_subscribe()
{
    uint32_t channel;
    IPAddress clientIP = server.client().remoteIP(); // get IP address of client
    std::string SSEurl = restEvents;

    if (subscriptionCount == SSE_MAX_CHANNELS)
    {
        ESP_LOGI(TAG, "Client %s SSE Subscription declined, subscription count: %d", clientIP.toString().c_str(), subscriptionCount);
        for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
        {
            ESP_LOGI(TAG, "Client %d: %s at %s", channel, subscription[channel].clientUUID.c_str(), subscription[channel].clientIP.toString().c_str());
        }
        return handle_notfound(); // We ran out of channels
    }

    if (clientIP == INADDR_NONE)
    {
        ESP_LOGI(TAG, "Sending %s, for: %s as clientIP missing", response400invalid, server.uri().c_str());
        server.send_P(400, type_txt, response400invalid);
        return;
    }

    // check we were passed at least one argument
    if (server.args() < 1)
    {
        ESP_LOGI(TAG, "Sending %s, for: %s", response400missing, server.uri().c_str());
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
                ESP_LOGI(TAG, "Client %s (%s) already connected on channel %d, remove SSE subscription", clientIP.toString().c_str(), server.arg(id).c_str(), channel);
                removeSSEsubscription(&subscription[channel]);
            }
            else
            {
                // Subscribed but not connected yet, so nothing to close down.
                ESP_LOGI(TAG, "Client %s (%s) already subscribed for SSE but not connected on channel %d", clientIP.toString().c_str(), server.arg(id).c_str(), channel);
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

        if (channel < SSE_MAX_CHANNELS)
        {
            subscriptionCount++;
        }
    }

    // Check if we found a free slot
    if (channel >= SSE_MAX_CHANNELS)
    {
        ESP_LOGI(TAG, "SSE subscription failed - no free slots available");
        server.send(503, type_txt, "No free subscription slots available");
        return;
    }

    // Validate client before assignment
    WiFiClient client = server.client();
    if (!client || !client.connected())
    {
        ESP_LOGI(TAG, "Invalid client for SSE subscription");
        server.send(400, type_txt, "Invalid client connection");
        return;
    }

    // validate optional heartbeat interval
    uint32_t heartbeatInterval = 1; // default
    if (heartbeatIntervalArgIdx >= 0)
    {
        int hbi = server.arg(heartbeatIntervalArgIdx).toInt();
        // in range of 0 (no heartbeat) to 60 seconds
        if (hbi < 0 || hbi > 60)
        {
            ESP_LOGI(TAG, "Invalid client for SSE subscription");
            server.send(400, type_txt, "Invalid heartbeat interval (0 - 60)");
            return;
        }
        else
        {
            // set to validated interval
            heartbeatInterval = (uint32_t)hbi;
        }
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

    SSEurl += std::to_string(channel);
    ESP_LOGI(TAG, "Client %s (%s) SSE subscription: %s, Total subscribed: %d, Heartbeat interval: %d, Log viewer: %d", clientIP.toString().c_str(), server.arg(id).c_str(), SSEurl.c_str(), subscriptionCount, heartbeatInterval, (int)logViewer);
    server.sendHeader(F("Cache-Control"), F("no-cache, no-store"));
    server.send_P(200, type_txt, SSEurl.c_str());
}

void handle_crashlog()
{
    server.client().print(response200);
#ifdef ESP8266
    // We save data from crash EEPROM into a temp file so when we send to the
    // browser client we chunk it in smaller pieces.  This improves reliability
    // on slow network links avoiding watchdog timeouts
    constexpr char CRASH_TEMP_FILE[] = "/crash_temp";
    File crashTempFile = LittleFS.open(CRASH_TEMP_FILE, "w");
    crashTempFile.truncate(0);
    crashTempFile.seek(0, fs::SeekSet);
    saveCrash.print(crashTempFile);
    ratgdoLogger->printSavedLog(crashTempFile);
    crashTempFile.close();
    if (crashCount > 0)
        ratgdoLogger->printSavedLog(server.client());
#else
    ratgdoLogger->printCrashLog(server.client());
#endif
}

void handle_showlog()
{
    server.client().print(response200);
    ratgdoLogger->printMessageLog(server.client());
}

void handle_showrebootlog()
{
    server.client().print(response200);
#ifdef ESP8266
    File file = LittleFS.open(REBOOT_LOG_MSG_FILE, "r");
    ratgdoLogger->printSavedLog(file, server.client());
    file.close();
#else
    ratgdoLogger->printSavedLog(server.client());
#endif
}

void handle_clearcrashlog()
{
    AUTHENTICATE();
    ESP_LOGI(TAG, "Clear saved crash log");
#ifdef ESP8266
    saveCrash.clear();
#else
    esp_core_dump_image_erase();
#endif
    crashCount = 0;
    server.send_P(200, type_txt, PSTR("Crash log cleared\n"));
}

#ifdef CRASH_DEBUG
void handle_crash_oom()
{
    ESP_LOGI(TAG, "Attempting to use up all memory");
    server.send_P(200, type_txt, PSTR("Attempting to use up all memory\n"));
    delay(1000);
    for (int i = 0; i < 30; i++)
    {
        ESP_LOGI(TAG, "malloc(1024)");
        crashptr = malloc(1024);
    }
}

void handle_forcecrash()
{
    ESP_LOGI(TAG, "Attempting to null ptr deref");
    server.send_P(200, type_txt, PSTR("Attempting to null ptr deref\n"));
    delay(1000);
    ESP_LOGI(TAG, "Result: %s", test_str);
}
#endif // CRASH_DEBUG

void SSEBroadcastState(const char *data, BroadcastType type)
{
    if (!web_setup_done)
        return;

    // Flash LED to signal activity
    led.flash(FLASH_MS);

    // if nothing subscribed, then return
    if (subscriptionCount == 0)
        return;

    for (uint32_t i = 0; i < SSE_MAX_CHANNELS; i++)
    {
        YIELD(); // yield between each SSE client
        if (subscription[i].SSEconnected)
        {
            if (subscription[i].client.connected())
            {
                if (type == LOG_MESSAGE)
                {
                    if (subscription[i].logViewer)
                    {
                        if (snprintf(writeBuffer, sizeof(writeBuffer), "event: logger\ndata: %s\n\n", data) >= (int)sizeof(writeBuffer))
                        {
                            // Will not fit in our write buffer, let system printf handle
#ifdef ESP8266
                            subscription[i].client.flush(); // make sure previous data all sent.
#endif
                            subscription[i].client.printf("event: logger\ndata: %s\n\n", data);
                        }
                        else
                        {
                            clientWrite(subscription[i].client, writeBuffer);
                        }
                    }
                }
                else if (type == RATGDO_STATUS)
                {
                    String IPaddrstr = IPAddress(subscription[i].clientIP).toString();
                    ESP_LOGV(TAG, "Client %s (%s) send status SSE on channel %d, data: %s", IPaddrstr.c_str(), subscription[i].clientUUID.c_str() , i, data);
                    if (snprintf(writeBuffer, sizeof(writeBuffer), "event: message\ndata: %s\n\n", data) >= (int)sizeof(writeBuffer))
                    {
                        // Will not fit in our write buffer, let system printf handle
#ifdef ESP8266
                        subscription[i].client.flush(); // make sure previous data all sent.
#endif
                        subscription[i].client.printf("event: message\ndata: %s\n\n", data);
                    }
                    else
                    {
                        clientWrite(subscription[i].client, writeBuffer);
                    }
                }
            }
            else
            {
                // Client connection has gone.  Remove from our subscribed client list
                removeSSEsubscription(&subscription[i]);
            }
        }
    }
    YIELD();
}

// Implement our own firmware update so can enforce MD5 check.
// Based on HTTPUpdateServer
void _setUpdaterError()
{
    StreamString str;
    Update.printError(str);
    _updaterError = str.c_str();
    ESP_LOGI(TAG, "Update error: %s", str.c_str());
}

void handle_update()
{
    bool verify = !strcmp(server.arg("action").c_str(), "verify");

    server.sendHeader(F("Access-Control-Allow-Headers"), "*");
    server.sendHeader(F("Access-Control-Allow-Origin"), "*");
    AUTHENTICATE();

    server.client().setNoDelay(true);
    if (!verify && Update.hasError())
    {
        // Error logged in _setUpdaterError
#ifdef ESP8266
        eboot_command_clear();
#else
        // TODO how to handle firmware upload failure on ESP32?
#endif
        ESP_LOGE(TAG, "Firmware upload error. Aborting update, not rebooting");
        server.send(400, type_txt, _updaterError.c_str());
        return;
    }

    if (server.args() > 0)
    {
        // Don't reboot, user/client must explicitly request reboot.
        server.send_P(200, type_txt, PSTR("Upload Success.\n"));
    }
    else
    {
        // Legacy... no query string args, so automatically reboot...
        server.send_P(200, type_txt, PSTR("Upload Success. Rebooting...\n"));
        ESP_LOGI(TAG, "System boot time:   %s", timeString(lastRebootAt));
        ESP_LOGI(TAG, "Firmware update at: %s", timeString());
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
    static uint32_t nextPrintPercent;
    HTTPUpload &upload = server.upload();
    static bool verify = false;
    static size_t size = 0;
    static const char *md5 = NULL;

    if (upload.status == UPLOAD_FILE_START)
    {
        _updaterError.clear();

#ifdef ESP8266
        _authenticatedUpdate = !userConfig->getPasswordRequired() || server.authenticateDigest(userConfig->getwwwUsername(), userConfig->getwwwCredentials());
#else
        _authenticatedUpdate = !userConfig->getPasswordRequired() || server.authenticate(ratgdoAuthenticate);
#endif
        if (!_authenticatedUpdate)
        {
            ESP_LOGI(TAG, "Unauthenticated Update");
            return;
        }
        ESP_LOGI(TAG, "Update: %s", upload.filename.c_str());
        verify = !strcmp(server.arg("action").c_str(), "verify");
        size = atoi(server.arg("size").c_str());
        md5 = server.arg("md5").c_str();

        // We are updating.  If size and MD5 provided, save them
        firmwareSize = size;
        if (strlen(md5) > 0)
            strlcpy(firmwareMD5, md5, sizeof(firmwareMD5));

        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        ESP_LOGI(TAG, "Available space for upload: %lu", maxSketchSpace);
        ESP_LOGI(TAG, "Firmware size: %s", (firmwareSize > 0) ? std::to_string(firmwareSize).c_str() : "Unknown");
        ESP_LOGI(TAG, "Flash chip speed %d MHz", ESP.getFlashChipSpeed() / 1000000);
        // struct eboot_command ebootCmd;
        // eboot_command_read(&ebootCmd);
        // ESP_LOGI(TAG, "eboot_command: 0x%08X 0x%08X [0x%08X 0x%08X 0x%08X (%d)]", ebootCmd.magic, ebootCmd.action, ebootCmd.args[0], ebootCmd.args[1], ebootCmd.args[2], ebootCmd.args[2]);
        if (!verify)
        {
            // Close HomeKit server so we don't have to handle HomeKit network traffic during update
            // Only if not verifying as either will have been shutdown on immediately prior upload, or we
            // just want to verify without disrupting operation of the HomeKit service.
#ifdef ESP8266
            arduino_homekit_close();
            IRAM_START(TAG);
            IRAM_END(TAG);
#else
            // TODO close HomeKit server during OTA update...
#endif
        }
        if (!verify && !Update.begin((firmwareSize > 0) ? firmwareSize : maxSketchSpace, U_FLASH))
        {
            _setUpdaterError();
        }
        else if (strlen(firmwareMD5) > 0)
        {
            // uncomment for testing...
            // char firmwareMD5[] = "675cbfa11d83a792293fdc3beb199cXX";
            ESP_LOGI(TAG, "Expected MD5: %s", firmwareMD5);
            Update.setMD5(firmwareMD5);
            if (firmwareSize > 0)
            {
                uploadProgress = 0;
                nextPrintPercent = 10;
                ESP_LOGI(TAG, "%s progress: 00", verify ? "Verify" : "Update");
            }
        }
    }
    else if (_authenticatedUpdate && upload.status == UPLOAD_FILE_WRITE && !_updaterError.length())
    {
        // Progress dot dot dot
        Serial.print(".");
        if (firmwareSize > 0)
        {
            uploadProgress += upload.currentSize;
            uint32_t uploadPercent = (uploadProgress * 100) / firmwareSize;
            if (uploadPercent >= nextPrintPercent)
            {
                Serial.print("\n"); // newline after the dot dot dots
                ESP_LOGI(TAG, "%s progress: %i", verify ? "Verify" : "Update", uploadPercent);
                SSEheartbeat(firmwareUpdateSub); // keep SSE connection alive.
                nextPrintPercent += 10;
                // Report percentage to browser client if it is listening
                if (firmwareUpdateSub && firmwareUpdateSub->client.connected())
                {
                    static char *json = loop_json;
                    TAKE_MUTEX();
                    JSON_START(json);
                    JSON_ADD_INT("uploadPercent", uploadPercent);
                    JSON_END();
                    JSON_REMOVE_NL(json);
                    snprintf(writeBuffer, sizeof(writeBuffer), "event: uploadStatus\ndata: %s\n\n", json);
                    clientWrite(firmwareUpdateSub->client, writeBuffer);
                    GIVE_MUTEX();
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
        Serial.print("\n"); // newline after last of the dot dot dots
        if (!verify)
        {
            if (Update.end(true))
            {
                ESP_LOGI(TAG, "Upload size: %zu", upload.totalSize);
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
        ESP_LOGI(TAG, "%s was aborted", verify ? "Verify" : "Update");
    }
    YIELD(); // Not sure if this is necessary as we should be returning to main loop?
}
