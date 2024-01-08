// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// Copyright (c) 2023-24 David Kerr, https://github.com/dkerr64
// All rights reserved. GPLv3 License

// Browser cache control, time in seconds after which browser cache invalid
// This is used for CSS, JS and IMAGE file types.  Set to 30 days !!
#define CACHE_CONTROL (60*60*24*30)

#include <string>
#include <tuple>
#include <unordered_map>

#include "www/build/webcontent.h"

#include "ratgdo.h"
#include "comms.h"

#include <arduino_homekit_server.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <LittleFS.h>
#include "log.h"
#include "utilities.h"

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater(true);

void handle_reset();
void handle_reboot();
void handle_status();
void handle_settings();
void handle_everything();
void handle_setgdo();
void handle_logout();

// Make device_name available
extern "C" char device_name[];
// Garage door status
extern struct GarageDoor garage_door;

// userid/password
const char www_username[] = "admin";
const char www_password[] = "password";
const char www_realm[] = "RATGDO Login Required";

// MD5 Hash of "user:realm:password"
char www_credentials[48] = "10d3c00fa1e09696601ef113b99f8a87";
const char credentials_file[] = "www_credentials";

void web_loop()
{
    server.handleClient();
}

const std::unordered_multimap<std::string, std::pair<const HTTPMethod, void (*)()>> builtInUri = {
    {"/status.json", {HTTP_GET, handle_status}},
    {"/reset", {HTTP_POST, handle_reset}},
    {"/reboot", {HTTP_POST, handle_reboot}},
    {"/setgdo", {HTTP_POST, handle_setgdo}},
    {"/logout", {HTTP_GET, handle_logout}},
    {"/settings.html", {HTTP_GET, handle_settings}},
    {"/", {HTTP_GET, handle_everything}}};

void setup_web()
{
    RINFO("Starting server");

    // www_credentials = server.credentialHash(www_username, www_realm, www_password);
    File file = LittleFS.open(credentials_file, "r");
    if (!file)
    {
        RINFO("www_credentials file doesn't exist. creating...");
        file = LittleFS.open(credentials_file, "w");
        file.print(www_credentials);
    }
    else
    {
        RINFO("Reading www_credentials from file");
        strncpy(www_credentials, file.readString().c_str(), 48);
    }
    file.close();
    RINFO("WWW Credentials: %s", www_credentials);

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
    // xSemaphoreHttpd = xSemaphoreCreateMutex();
    server.onNotFound(handle_everything);
    httpUpdater.setup(&server);
    server.begin();
    RINFO("HTTP server started");
    return;
}

/********* handlers **********/
void handle_reset()
{
    if (!server.authenticateDigest(www_username, www_credentials))
    {
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }
    RINFO("... reset requested");
    const char *resp = "<p>This device has been un-paired from HomeKit.</p><p><a href=\"/\">Back</a></p>";
    homekit_storage_reset();
    server.send(200, resp);
    server.stop();
    sync_and_restart();
    return;
}

void handle_reboot()
{
    RINFO("... reboot requested");
    const char *resp =
        "<head>"
        "<meta http-equiv=\"refresh\" content=\"15;url=/\" />"
        "</head>"
        "<body>"
        "<p>RATGDO restarting. Please wait. Reconnecting in 15 seconds...</p>"
        "<p><a href=\"/\">Back</a></p>"
        "</body>";
    server.send(200, resp);
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
            strncpy(matchHdr, server.header("If-None-Match").c_str(), 8);

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
        RINFO("Sending 404 not found for: %s", page);
        server.send(404, "text/plain", "404: Not Found");
    }
    return;
}

void handle_everything()
{
    String page = server.uri();
    if (page == "/")
        load_page("/index.html");
    else
        load_page(page.c_str());
}

void handle_status()
{
    bool all = true;
    char json[512] = ""; // Maximum length of JSON response

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
#define localIP WiFi.localIP().toString().c_str()
#define subnetMask WiFi.subnetMask().toString().c_str()
#define gatewayIP WiFi.gatewayIP().toString().c_str()
#define macAddress WiFi.macAddress().c_str()
#define wifiSSID WiFi.SSID().c_str()
// Helper macros to add int, string or boolean to a json format string.
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

    // Build the JSON string
    strcat(json, "{\n");
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
        ADD_STR(json, "localIP", localIP);
    if (all)
        ADD_STR(json, "subnetMask", subnetMask);
    if (all)
        ADD_STR(json, "gatewayIP", gatewayIP);
    if (all)
        ADD_STR(json, "macAddress", macAddress);
    if (all)
        ADD_STR(json, "wifiSSID", wifiSSID);
    if (all || HAS_ARG("doorstate"))
    {
        switch (garage_door.current_state)
        {
        case 0:
            ADD_STR(json, "garageDoorState", "Open");
            break;
        case 1:
            ADD_STR(json, "garageDoorState", "Closed");
            break;
        case 2:
            ADD_STR(json, "garageDoorState", "Opening");
            break;
        case 3:
            ADD_STR(json, "garageDoorState", "Closing");
            break;
        case 4:
            ADD_STR(json, "garageDoorState", "Stopped");
            break;
        default:
            ADD_STR(json, "garageDoorState", "Unknown");
        }
    }
    if (all || HAS_ARG("lockstate"))
    {
        switch (garage_door.current_lock)
        {
        case 0:
            ADD_STR(json, "garageLockState", "Unsecured");
            break;
        case 1:
            ADD_STR(json, "garageLockState", "Secured");
            break;
        case 2:
            ADD_STR(json, "garageLockState", "Jammed");
            break;
        default:
            ADD_STR(json, "garageLockState", "Unknown");
        }
    }
    if (all || HAS_ARG("lighton"))
        ADD_BOOL(json, "garageLightOn", garage_door.light)
    if (all || HAS_ARG("motion"))
        ADD_BOOL(json, "garageMotion", garage_door.motion)
    if (all || HAS_ARG("obstruction"))
        ADD_BOOL(json, "garageObstructed", garage_door.obstructed)

    // remove the final comma/newline to ensure valid JSON syntax
    json[strlen(json) - 2] = 0;
    // Terminate json with close curly
    strcat(json, "\n}");
    // Only log if all requested (no arguments).
    // Avoids spaming console log if repeated requests for one value.
    if (all)
        RINFO("Status requested:\n%s", json);
    server.sendHeader("Cache-Control", "no-cache, no-store");
    server.send(200, "application/json", json);
    return;
}

void handle_settings()
{
    if (!server.authenticateDigest(www_username, www_credentials))
    {
        RINFO("In settings request authentication");
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
    RINFO("In setGDO");
    if (!server.authenticateDigest(www_username, www_credentials))
    {
        RINFO("In setGDO request authentication");
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }

    strncpy(key, server.argName(0).c_str(), 20);
    strncpy(value, server.arg(0).c_str(), 48);
    RINFO("Key: %s, Value: %s", key, value);
    if (strlen(key) == 0 || strlen(value) == 0)
    {
        RINFO("Sending 400 bad request, missing argument, for: %s", server.uri().c_str());
        server.send(400, "text/plain", "400: Bad Request, missing argument");
        return;
    }
    if (!server.authenticateDigest(www_username, www_credentials))
    {
        RINFO("In setGDO request authentication");
        return server.requestAuthentication(DIGEST_AUTH, www_realm);
    }

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
        strncpy(www_credentials, server.arg(0).c_str(), 48);
        RINFO("Writing new www_credentials to file: %s", www_credentials);
        File file = LittleFS.open(credentials_file, "w");
        file.print(www_credentials);
        file.close();
    }
    else
    {
        RINFO("Sending 400 bad request, missing argument, for: %s", server.uri().c_str());
        server.send(400, "text/plain", "400: Bad Request, missing argument");
    }
    RINFO("SetGDO Complete");
    server.send(200, "text/html", "<p>Success.</p>");
    return;
}
