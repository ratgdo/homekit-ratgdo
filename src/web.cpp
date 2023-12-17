// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// Copyright (c) 2023-24 David Kerr, https://github.com/dkerr64
// All rights reserved. GPLv3 License

#include "www/build/webcontent.h"

#include <arduino_homekit_server.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include "log.h"
#include "ratgdo.h"

#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater(true);

void handle_reset();
void handle_reboot();
void handle_notfound();
void handle_handlestatus();

extern struct GarageDoor garage_door;

// Make device_name available
extern "C" char device_name[];

/********* main loop **********/

void setup_web()
{
    server.on("/status.json", HTTP_GET, handle_handlestatus);
    server.on("/reset", HTTP_POST, handle_reset);
    server.on("/reboot", HTTP_POST, handle_reboot);

    server.onNotFound(handle_notfound);

    httpUpdater.setup(&server);

    server.begin();
    RINFO("HTTP server started");
}

void web_loop()
{
    server.handleClient();
}

/********* handlers **********/

void handle_reset()
{
    RINFO("... reset requested");
    homekit_storage_reset();
    server.send(
        200,
        "text/html",
        "<p>This device has been un-paired from HomeKit.</p>"
        "<p><a href=\"/\">Back</a></p>");
}

void handle_reboot()
{
    RINFO("... reboot requested");
    server.send(
        200,
        "text/html",
        "<head>"
        "<meta http-equiv=\"refresh\" content=\"15;url=/\" />"
        "</head>"
        "<body>"
        "<p>RATGDO restarting. Please wait. Reconnecting in 15 seconds...</p>"
        "<p><a href=\"/\">Back</a></p>"
        "</body>");

    server.stop();
    delay(10); // give a bit of time for the connection to close fully
    ESP.restart();
}

void handle_notfound()
{
    String page = server.uri();

    if (page == "/")
    {
        page = "/index.html";
    }

    if (webcontent.count(page.c_str()) > 0)
    {
        unsigned char *data;
        unsigned int length;
        char *type;
        std::tie(data, length, type) = webcontent[page.c_str()];
        RINFO("Sending gzip data for: %s (type %s, length %i)", page.c_str(), type, length);
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, type, (const char *)data, length);
    }
    else
    {
        RINFO("Sending 404 not found for: %s", page.c_str());
        server.send(404, "text/plain", "404: Not Found");
    }
    return;
}

void handle_handlestatus()
{
    homekit_server_t *hks = arduino_homekit_get_running_server();

    // If arguments passed with the URL then only a subset of the
    // status fields are requested...
    std::unordered_map<std::string, bool> argReq;
    bool all = true;
    if (server.args() > 0)
    {
        all = false;
        for (int i = 0; i < server.args(); i++)
        {
            argReq[server.argName(i).c_str()] = true;
        }
    }

    // Build the JSON string
    // Note that newlines and indentation are not required within the json,
    // but are included to improve readability in the console log.
    std::string json = "{\n"; // open the json
    if (all || argReq["uptime"])
        json.append(std::string("  \"upTime\": ") + std::to_string(millis()) + ",\n");
    if (all)
        json.append(std::string("  \"deviceName\": \"") + device_name + "\",\n");
    if (all)
        json.append(std::string("  \"paired\": ") + (homekit_is_paired() ? "true" : "false") + ",\n");
    if (all)
        json.append(std::string("  \"firmwareVersion\": \"") + std::string(AUTO_VERSION) + "\",\n");
    if (all)
        json.append(std::string("  \"accessoryID\": \"") + hks->accessory_id + "\",\n");
    if (all)
        json.append(std::string("  \"localIP\": \"") + WiFi.localIP().toString().c_str() + "\",\n");
    if (all)
        json.append(std::string("  \"subnetMask\": \"") + WiFi.subnetMask().toString().c_str() + "\",\n");
    if (all)
        json.append(std::string("  \"gatewayIP\": \"") + WiFi.gatewayIP().toString().c_str() + "\",\n");
    if (all)
        json.append(std::string("  \"macAddress\": \"") + WiFi.macAddress().c_str() + "\",\n");
    if (all)
        json.append(std::string("  \"wifiSSID\": \"") + WiFi.SSID().c_str() + "\",\n");
    if (all || argReq["doorstate"])
    {
        std::string doorState = "";
        switch (garage_door.current_state)
        {
        case 0:
            doorState = "Open";
            break;
        case 1:
            doorState = "Closed";
            break;
        case 2:
            doorState = "Opening";
            break;
        case 3:
            doorState = "Closing";
            break;
        case 4:
            doorState = "Stopped";
            break;
        default:
            doorState = "Unknown";
        }
        json.append(std::string("  \"garageDoorState\": \"") + doorState + "\",\n");
    }
    if (all || argReq["lockstate"])
    {
        std::string lockState = "";
        switch (garage_door.current_lock)
        {
        case 0:
            lockState = "Unsecured";
            break;
        case 1:
            lockState = "Secured";
            break;
        case 2:
            lockState = "Jammed";
            break;
        default:
            lockState = "Unknown";
        }
        json.append(std::string("  \"garageLockState\": \"") + lockState + "\",\n");
    }
    if (all || argReq["lighton"])
        json.append(std::string("  \"garageLightOn\": ") + (garage_door.light ? "true" : "false") + ",\n");
    if (all || argReq["motion"])
        json.append(std::string("  \"garageMotion\": ") + (garage_door.motion ? "true" : "false") + ",\n");
    if (all || argReq["obstruction"])
        json.append(std::string("  \"garageObstructed\": ") + (garage_door.obstructed ? "true" : "false") + ",\n");

    // remove the final comma/newline to ensure valid JSON syntax
    json.erase(json.rfind(",\n"));
    json.append("\n}"); // close the json

    // Only log if all requested (no arguments).
    // Avoids spaming console log if repeated requests for one value.
    if (all)
        RINFO("Status requested:\n%s", json.c_str());

    server.send(200, "application/json", json.c_str());
    return;
}
