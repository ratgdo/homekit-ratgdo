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

#include "EspSaveCrash.h"
#include <arduino_homekit_server.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <Ticker.h>
#include "log.h"
#include "utilities.h"
#include <umm_malloc/umm_malloc.h>
#include <umm_malloc/umm_heap_select.h>

EspSaveCrash saveCrash(1408, 1024, true);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater(true);

void handle_reset();
void handle_reboot();
void handle_status();
void handle_settings();
void handle_everything();
void handle_setgdo();
void handle_logout();
void handle_auth();
void handle_subscribe();
void handle_crashlog();
void handle_clearcrashlog();
#ifdef CRASH_DEBUG
void handle_forcecrash();
char* test_str = NULL;
#endif
void SSEHandler(uint8_t);
void SSEBroadcastState(const char *);
void handle_notfound();

// Make device_name available
extern "C" char device_name[];
// filename to save device name
extern "C" const char device_name_file[] = "device_name";

// Garage door status
extern struct GarageDoor garage_door;
// Local copy of door status
GarageDoor last_reported_garage_door;
bool last_reported_paired = false;
// Garage door security type
extern uint8_t gdoSecurityType;

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

// number of times the device has crashed
int crashCount = 0;

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
} subscription[SSE_MAX_CHANNELS];

uint8_t subscriptionCount = 0;

char *json; // Maximum length of JSON response
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
        strcat(s, (k));                       \
        strcat(s, "\": ");                    \
        strcat(s, std::to_string(v).c_str()); \
        strcat(s, ",\n");                     \
    }
#define ADD_STR(s, k, v)     \
    {                        \
        strcat(s, "\"");     \
        strcat(s, (k));      \
        strcat(s, "\": \""); \
        strcat(s, (v));      \
        strcat(s, "\",\n");  \
    }
#define ADD_BOOL(s, k, v)                  \
    {                                      \
        strcat(s, "\"");                   \
        strcat(s, (k));                    \
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
    START_JSON(json);
    ADD_BOOL_C(json, "paired", homekit_is_paired(), last_reported_paired);
    ADD_STR_C(json, "garageDoorState", DOOR_STATE(garage_door.current_state), garage_door.current_state, last_reported_garage_door.current_state);
    ADD_STR_C(json, "garageLockState", LOCK_STATE(garage_door.current_lock), garage_door.current_lock, last_reported_garage_door.current_lock);
    ADD_BOOL_C(json, "garageLightOn", garage_door.light, last_reported_garage_door.light);
    ADD_BOOL_C(json, "garageMotion", garage_door.motion, last_reported_garage_door.motion);
    ADD_BOOL_C(json, "garageObstructed", garage_door.obstructed, last_reported_garage_door.obstructed);
    if (strlen(json) > 2)
    {
        // Have we added anything to the JSON string?
        ADD_INT(json, "upTime", millis());
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

const std::unordered_multimap<std::string, std::pair<const HTTPMethod, void (*)()>> builtInUri = {
    {"/status.json", {HTTP_GET, handle_status}},
    {"/reset", {HTTP_POST, handle_reset}},
    {"/reboot", {HTTP_POST, handle_reboot}},
    {"/setgdo", {HTTP_POST, handle_setgdo}},
    {"/logout", {HTTP_GET, handle_logout}},
    {"/settings.html", {HTTP_GET, handle_settings}},
    {"/auth", {HTTP_GET, handle_auth}},
    {"/crashlog", {HTTP_GET, handle_crashlog}},
    {"/clearcrashlog", {HTTP_GET, handle_clearcrashlog}},
#ifdef CRASH_DEBUG
    {"/forcecrash", {HTTP_GET, handle_forcecrash}},
#endif
    {"/rest/events/subscribe", {HTTP_GET, handle_subscribe}},
    {"/", {HTTP_GET, handle_everything}}};

void setup_web()
{
    RINFO("Starting server");
    {
        HeapSelectIram ephemeral;
        json = (char *)malloc(1024);
    }
    last_reported_paired = homekit_is_paired();
    // www_credentials = server.credentialHash(www_username, www_realm, www_password);
    read_string_from_file(credentials_file, www_credentials, www_credentials, 48);
    RINFO("WWW Credentials: %s", www_credentials);
    passwordReq = (read_int_from_file(www_pw_required_file) != 0);
    RINFO("WWW Password %s required", (passwordReq) ? "is" : "not");
    wifiPhyMode = (WiFiPhyMode_t)read_int_from_file(wifiPhyModeFile);

    crashCount = saveCrash.count();
    if (crashCount == 255)
    {
        saveCrash.clear();
        crashCount = 0;
    }
    rebootSeconds = read_int_from_file(system_reboot_timer, REBOOT_SECONDS);
    if (rebootSeconds > 0)
    {
        RINFO("System will reboot every %i seconds", rebootSeconds);
    }
    RINFO("Registering URI handlers");
    // Register URI handlers for URIs that have built-in handlers in this source file.
    for (auto uri : builtInUri)
    {
        HTTPMethod method = std::get<1>(uri).first;
        void (*handler)() = std::get<1>(uri).second;
        RINFO("Register: %s", uri.first.c_str());
        server.on(uri.first.c_str(), method, handler);
    }
    // Register URI handlers for URIs that are "external" files
    for (auto uri : webcontent)
    {
        // Only register those that are not duplicates of built-in handlers.
        if (builtInUri.find(uri.first) == builtInUri.end())
        {
            RINFO("Register: %s", uri.first.c_str());
            server.on(uri.first.c_str(), HTTP_GET, handle_everything);
        }
    }

    server.onNotFound(handle_everything);
    httpUpdater.setup(&server);
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
    RINFO("Sending 404 Not Found for: %s", server.uri().c_str());
    server.send(404, "text/plain", "Not Found");
}

void handle_auth()
{
    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    const char *resp = "Authenticated";
    server.send(200, resp);
    return;
}

void handle_reset()
{
    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    RINFO("... reset requested");
    const char *resp = "<p>This device has been un-paired from HomeKit.</p><p><a href=\"/\">Back</a></p>";
    homekit_storage_reset();
    server.send(200, resp);
    delay(100);
    server.stop();
    sync_and_restart();
    return;
}

void handle_reboot()
{
    RINFO("... reboot requested");
    const char *resp =
        "<head><meta http-equiv=\"refresh\" content=\"30;url=/\" /></head>"
        "<body><p>RATGDO restarting. Please wait. Reconnecting in 30 seconds...</p><p><a href=\"/\">Back</a></p></body>";
    server.send(200, resp);
    delay(100);
    server.stop();
    sync_and_restart();
    return;
}

void load_page(const char *page)
{
    if (webcontent.count(page) > 0)
    {
        const unsigned char *data = std::get<0>(webcontent.at(page));
        const unsigned int length = std::get<1>(webcontent.at(page));
        const char *type = std::get<2>(webcontent.at(page));
        // Following for browser cache control...
        const char *crc32 = std::get<3>(webcontent.at(page)).c_str();
        bool cache = false;
        char cacheHdr[24] = "no-cache, no-store";
        char matchHdr[8] = "";

        if ((CACHE_CONTROL > 0) &&
            (!strcmp(type, "text/css") || !strcmp(type, "text/javascript") || strstr(type, "image")))
        {
            sprintf(cacheHdr, "max-age=%i", CACHE_CONTROL);
            cache = true;
        }

        server.sendHeader("Cache-Control", cacheHdr);
        if (cache)
            server.sendHeader("ETag", crc32);

        if (server.hasHeader("If-None-Match"))
            strlcpy(matchHdr, server.header("If-None-Match").c_str(), 8);

        if (strcmp(crc32, matchHdr))
        {
            RINFO("Sending gzip data for: %s (type %s, length %i)", page, type, length);
            server.sendHeader("Content-Encoding", "gzip");
            server.send_P(200, type, (const char *)data, length);
        }
        else
        {
            RINFO("Sending 304 Not Modified for: %s (type %s)", page, type);
            server.send(304, type, "", 0);
        }
    }
    else
    {
        handle_notfound();
    }
    return;
}

void handle_everything()
{
    String page = server.uri();
    if (page == "/")
    {
        load_page("/index.html");
    }
    else
    {
        const char *uri = server.uri().c_str();
        const char *restEvents = "/rest/events/";
        if (!strncmp_P(uri, restEvents, strlen(restEvents)))
        {
            uri += strlen(restEvents); // Skip the "/rest/events/" and get to the channel number
            unsigned int channel = atoi(uri);
            if (channel < SSE_MAX_CHANNELS)
            {
                return SSEHandler(channel);
            }
        }
        load_page(page.c_str());
    }
}

void handle_status()
{
    bool all = true;
    // find query string and macro to test if arg is present
    std::unordered_map<std::string, bool> argReq;
    if (server.args() > 0)
    {
        all = false;
        for (int i = 0; i < server.args(); i++)
        {
            argReq[server.argName(i).c_str()] = true;
        }
    }

#define HAS_ARG(arg) argReq[arg]
#define upTime millis()
#define paired homekit_is_paired()
#define accessoryID arduino_homekit_get_running_server()->accessory_id
#define IPaddr WiFi.localIP().toString().c_str()
#define subnetMask WiFi.subnetMask().toString().c_str()
#define gatewayIP WiFi.gatewayIP().toString().c_str()
#define macAddress WiFi.macAddress().c_str()
#define wifiSSID WiFi.SSID().c_str()
#define GDOSecurityType std::to_string(gdoSecurityType).c_str()
    // Helper macros to add int, string or boolean to a json format string.

    // Build the JSON string
    START_JSON(json);
    if (all || HAS_ARG("uptime"))
        ADD_INT(json, "upTime", upTime);
    if (all)
        ADD_STR(json, "deviceName", device_name);
    if (all || HAS_ARG("paired"))
        ADD_BOOL(json, "paired", paired);
    if (all)
        ADD_STR(json, "firmwareVersion", std::string(AUTO_VERSION).c_str());
    if (all)
        ADD_STR(json, "accessoryID", accessoryID);
    if (all)
        ADD_STR(json, "localIP", IPaddr);
    if (all)
        ADD_STR(json, "subnetMask", subnetMask);
    if (all)
        ADD_STR(json, "gatewayIP", gatewayIP);
    if (all)
        ADD_STR(json, "macAddress", macAddress);
    if (all)
        ADD_STR(json, "wifiSSID", wifiSSID);
    if (all)
        ADD_STR(json, "wifiRSSI", (std::to_string(WiFi.RSSI()) + " dBm").c_str());
    if (all)
        ADD_STR(json, "GDOSecurityType", GDOSecurityType);
    if (all || HAS_ARG("doorstate"))
        ADD_STR(json, "garageDoorState", DOOR_STATE(garage_door.current_state));
    if (all || HAS_ARG("lockstate"))
        ADD_STR(json, "garageLockState", LOCK_STATE(garage_door.current_lock));
    if (all || HAS_ARG("lighton"))
        ADD_BOOL(json, "garageLightOn", garage_door.light);
    if (all || HAS_ARG("motion"))
        ADD_BOOL(json, "garageMotion", garage_door.motion);
    if (all || HAS_ARG("obstruction"))
        ADD_BOOL(json, "garageObstructed", garage_door.obstructed);
    if (all)
        ADD_BOOL(json, "passwordRequired", passwordReq);
    if (all)
        ADD_INT(json, "rebootSeconds", rebootSeconds);
    uint32_t free_heap = system_get_free_heap_size();
    if (free_heap < min_heap)
        min_heap = free_heap;
    if (all)
        ADD_INT(json, "freeHeap", free_heap);
    if (all)
        ADD_INT(json, "minHeap", min_heap);
    if (all)
        ADD_INT(json, "minStack", ESP.getFreeContStack());
    if (all)
        ADD_INT(json, "crashCount", crashCount);
    if (all)
        ADD_INT(json, "wifiPhyMode", wifiPhyMode);

    END_JSON(json);
    // Only log if all requested (no arguments).
    // Avoids spaming console log if repeated requests for one value.
    if (all)
        RINFO("Status requested:\n%s\nlength %d\n", json, strlen(json));
    last_reported_garage_door = garage_door;

    server.sendHeader("Cache-Control", "no-cache, no-store");
    server.send(200, "application/json", json);
    return;
}

void handle_settings()
{
    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    return load_page("/settings.html");
}

void handle_logout()
{
    RINFO("Handle logout");
    /*
    int nHeaders = server.headers();
    for (int i = 0; i < nHeaders; i++) {
        RINFO("%s : %s", server.headerName(i).c_str(), server.header(i).c_str());
    }
    */
    return server.requestAuthentication(DIGEST_AUTH, www_realm);
}

void handle_setgdo()
{
    char key[20] = "";
    char value[48] = "";
    bool reboot = false;
    bool error = false;

    RINFO("In setGDO");
    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        RINFO("In setGDO request authentication");
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }

    // Loop over all the GDO settings passed in...
    for (int i = 0; i < server.args(); i++)
    {
        strlcpy(key, server.argName(i).c_str(), 20);
        strlcpy(value, server.arg(i).c_str(), 48);
        RINFO("Key: %s, Value: %s", key, value);
        if (strlen(key) == 0 || strlen(value) == 0)
        {
            RINFO("Sending 400 bad request, missing argument, for: %s", server.uri().c_str());
            server.send(400, "text/plain", "400: Bad Request, missing argument");
            return;
        }

        // Check against each known setting
        if (!strcmp(key, "lighton"))
        {
            set_light(!strcmp(value, "1") ? true : false);
        }
        else if (!strcmp(key, "doorstate"))
        {
            if (!strcmp(value, "1"))
                open_door();
            else
                close_door();
        }
        else if (!strcmp(key, "lockstate"))
        {
            set_lock(!strcmp(value, "1") ? 1 : 0);
        }
        else if (!strcmp(key, "credentials"))
        {
            strlcpy(www_credentials, value, 48);
            RINFO("Writing new www_credentials to file: %s", www_credentials);
            write_string_to_file(credentials_file, www_credentials);
        }
        else if (!strcmp(key, "gdoSecurity"))
        {
            uint32_t type = atoi(value);
            if ((type == 1) || (type == 2))
            {
                RINFO("SetGDO security type to %i", type);
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
            write_int_to_file(www_pw_required_file, &required);
            reboot = true;
        }
        else if (!strcmp(key, "rebootSeconds"))
        {
            uint32_t seconds = atoi(value);
            write_int_to_file(system_reboot_timer, &seconds);
            reboot = true;
        }
        else if (!strcmp(key, "newDeviceName"))
        {
            if (strlen(value) > 0)
            {
                strlcpy(device_name, value, 32);
                RINFO("Writing new device name to file: %s", device_name);
                write_string_to_file(device_name_file, device_name);
                reboot = true;
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
        else
        {
            error = true;
        }
    }
    RINFO("SetGDO Complete");
    // Simple error handling...
    if (error)
    {
        RINFO("Sending 400 bad request, missing or invalid argument, for: %s", server.uri().c_str());
        server.send(400, "text/plain", "400: Bad Request, invalid argument");
    }
    else
    {
        server.send(200, "text/html", "<p>Success.</p>");
    }
    // Some settings require reboot to take effect...
    if (reboot)
    {
        RINFO("SetGDO Restart required");
        delay(100);
        server.stop();
        sync_and_restart();
    }
    return;
}

void SSEheartbeat(uint8_t channel, SSESubscription *s)
{
    // RINFO("SSEheartbeat - Client %s on channel %d", s->clientIP.toString().c_str(), channel);
    size_t txsize;
    uint32_t free_heap = system_get_free_heap_size();
    if (free_heap < min_heap)
        min_heap = free_heap;

    START_JSON(json);
    ADD_INT(json, "upTime", millis());
    ADD_INT(json, "freeHeap", free_heap);
    ADD_INT(json, "minHeap", min_heap);
    ADD_STR(json, "wifiRSSI", (std::to_string(WiFi.RSSI()) + " dBm").c_str());
    END_JSON(json);
    REMOVE_NL(json);

    if (!(s->clientIP))
        return;

    if (!(s->SSEconnected))
    {
        if (s->SSEfailCount++ >= 5)
        {
            // 5 heartbeats have failed... assume client will not connect
            // and free up the slot
            RINFO("SSEheartbeat - Timeout waiting for %s on channel %d to listen for events", s->clientIP.toString().c_str(), channel);
            s->heartbeatTimer.detach();
            s->clientIP = INADDR_NONE;
            s->clientUUID.clear();
            subscriptionCount--;
            // no need to stop client socket because it is not live yet.
        }
        else
        {
            RINFO("SSEheartbeat - Client %s on channel %d not yet listening for events", s->clientIP.toString().c_str(), channel);
        }
        return;
    }

    txsize = 0;
    if (s->client.connected())
    {
        txsize = s->client.printf("event: message\nretry: 15000\ndata: %s\n\n", json);
    }
    if (txsize == 0)
    {
        RINFO("SSEheartbeat - client not listening on channel %d, remove subscription", channel);
        s->heartbeatTimer.detach();
        s->client.flush();
        s->client.stop();
        s->clientIP = INADDR_NONE;
        s->clientUUID.clear();
        s->SSEconnected = false;
        subscriptionCount--;
    }
}

void SSEHandler(uint8_t channel)
{
    if (server.args() != 1)
    {
        RINFO("Sending 400 bad request, missing argument, for: %s", server.uri().c_str());
        server.send(400, "text/plain", "400: Bad Request, missing argument");
        return;
    }
    WiFiClient client = server.client();
    SSESubscription &s = subscription[channel];
    if (s.clientUUID != server.arg(0))
    {
        RINFO("SSEHandler - unregistered client %s with IP %s tries to listen", server.arg(0).c_str(), client.remoteIP().toString().c_str());
        return handle_notfound();
    }
    client.setNoDelay(true);
    client.setSync(true);
    client.setTimeout(500);
    s.client = client;                               // capture SSE server client connection
    server.setContentLength(CONTENT_LENGTH_UNKNOWN); // the payload can go on forever
    /*
    server.sendHeader("Cache-Control", "no-cache, no-store");
    server.sendHeader("Connection", "keep-alive");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    */
    server.sendContent("HTTP/1.1 200 OK\nContent-Type: text/event-stream;\nConnection: keep-alive\nCache-Control: no-cache\nAccess-Control-Allow-Origin: *\n\n");
    s.SSEconnected = true;
    s.SSEfailCount = 0;
    s.heartbeatTimer.attach_scheduled(1.0, [channel, &s]
                                      { SSEheartbeat(channel, &s); });
    RINFO("SSEHandler - Client %s listening for events on channel %d", client.remoteIP().toString().c_str(), channel);
}

void handle_subscribe()
{
    if (server.args() != 1)
    {
        RINFO("Sending 400 bad request, missing argument, for: %s", server.uri().c_str());
        server.send(400, "text/plain", "400: Bad Request, missing argument");
        return;
    }

    if (subscriptionCount == SSE_MAX_CHANNELS - 1)
    {
        return handle_notfound(); // We ran out of channels
    }
    uint8_t channel;
    IPAddress clientIP = server.client().remoteIP(); // get IP address of client
    String SSEurl = "/rest/events/";

    // check if we already have a subscription for this UUID
    for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
    {
        if (subscription[channel].clientUUID == server.arg(0))
        {
            if (subscription[channel].SSEconnected)
            {
                // Already connected.  We need to close it down as client will be reconnecting
                RINFO("SSE Subscribe - client %s with IP %s already connected on channel %d, remove subscription", server.arg(0).c_str(), clientIP.toString().c_str(), channel);
                subscription[channel].heartbeatTimer.detach();
                subscription[channel].client.flush();
                subscription[channel].client.stop();
            }
            else
            {
                // Subscribed but not connected yet, so nothing to close down.
                RINFO("SSE Subscribe - client %s with IP %s already subscribed but not connected on channel %d", server.arg(0).c_str(), clientIP.toString().c_str(), channel);
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
    subscription[channel] = {clientIP, server.client(), Ticker(), false, 0, server.arg(0)};
    SSEurl += channel;
    RINFO("Subscription for client %s with IP %s: event bus location: %s", server.arg(0).c_str(), clientIP.toString().c_str(), SSEurl.c_str());
    server.sendHeader("Cache-Control", "no-cache, no-store");
    server.send(200, "text/plain", SSEurl.c_str());
}

void handle_crashlog()
{
    RINFO("Request to display crash log...");
    WiFiClient client = server.client();
    client.print("HTTP/1.1 200 OK\n");
    client.print("Content-Type: text/plain\n");
    client.print("Connection: close\n");
    client.print("\n");
    saveCrash.print(client);
}

void handle_clearcrashlog()
{
    if (passwordReq && !server.authenticateDigest(www_username, www_credentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    RINFO("Clear saved crash log...");
    saveCrash.clear();
    crashCount = 0;
    server.send(200, "text/plain", "Crash log cleared");
}

#ifdef CRASH_DEBUG
void handle_forcecrash()
{
    RINFO("Attempting to null ptr deref...");
    server.send(200, "text/plain", "Attempting to null ptr deref");
    delay(1000);
    RINFO("Result: %s", test_str);
}
#endif

void SSEBroadcastState(const char *data)
{
    // if nothing subscribed, then return
    if (subscriptionCount == 0)
        return;

    for (uint8_t i = 0; i < SSE_MAX_CHANNELS; i++)
    {
        if (subscription[i].SSEconnected && subscription[i].clientIP)
        {
            String IPaddrstr = IPAddress(subscription[i].clientIP).toString();
            if (subscription[i].client.connected())
            {
                RINFO("broadcast status change to client IP %s on channel %d with new state %s", IPaddrstr.c_str(), i, data);
                subscription[i].client.printf("event: message\ndata: %s\n\n", data);
            }
            else
            {
                RINFO("SSEBroadcastState - client %s registered on channel %d but not listening", IPaddrstr.c_str(), i);
            }
        }
    }
}
