// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include <arduino_homekit_server.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include "log.h"

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater(true);

/********* forward decl *********/

void handle_root();
void handle_reset();
void handle_reboot();

/********* main loop **********/

void setup_web() {
    server.on("/", HTTP_GET, handle_root);
    server.on("/reset", HTTP_POST, handle_reset);
    server.on("/reboot", HTTP_POST, handle_reboot);

    server.onNotFound([]() {
        server.send(404, "text/plain", "404: Not Found");
    });

    httpUpdater.setup(&server);


    server.begin();
    RINFO("HTTP server started");
}

void web_loop() {
    server.handleClient();
}

/********* handlers **********/

void handle_root() {
    if (homekit_is_paired()) {
        server.send(
            200,
            "text/html",
            "<p>If you wish to re-pair to another HomeKit Home, you must first click the following button:</p>"
            "<form action=\"/reset\" method=\"POST\">"
            "<input type=\"submit\" value=\"Un-pair HomeKit\">"
            "</form>"
            "<form action=\"/reboot\" method=\"POST\">"
            "<input type=\"submit\" value=\"Reboot RATGDO\">"
            "</form>"
            "<p>" AUTO_VERSION "</p>"
            "<p><a href=\"/update\">Firmware update</a></p>"
        );
    } else {
        server.send(
            200,
            "text/html",
            #include "qrcode.h"
            "<p>If you wish to re-pair to another HomeKit Home, you must first click the following button:</p>"
            "<form action=\"/reset\" method=\"POST\">"
                "<input type=\"submit\" value=\"Un-pair HomeKit\">"
            "</form>"
            "<p>To reboot the RATGDO, click the following button:</p>"
            "<form action=\"/reboot\" method=\"POST\">"
                "<input type=\"submit\" value=\"Reboot RATGDO\">"
            "</form>"
            "<p>" AUTO_VERSION "</p>"
            "<p><a href=\"/update\">Firmware update</a></p>"
        );
    }
}

void handle_reset() {
    homekit_storage_reset();

    server.send(
        200,
        "text/html",
        "<p>This device has been un-paired from HomeKit.</p>"
        "<p><a href=\"/\">Back</a></p>"
    );
}

void handle_reboot() {
    server.send(
        200,
        "text/html",
        "<head>"
            "<meta http-equiv=\"refresh\" content=\"15;url=/\" />"
        "</head>"
        "<body>"
            "<p>RATGDO restarting. Please wait. Reconnecting in 15 seconds...</p>"
            "<p><a href=\"/\">Back</a></p>"
        "</body>"
    );
    server.stop(); // ensure that delivery is complete?
    delay(10);     // give a bit of time for the connection to close fully
    ESP.restart();
}
