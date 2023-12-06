#include <arduino_homekit_server.h>
#include <ESP8266WebServer.h>

ESP8266WebServer server(80);

/********* forward decl *********/

void handle_root();
void handle_reset();

/********* main loop **********/

void setup_web() {
    server.on("/", HTTP_GET, handle_root);
    server.on("/reset", HTTP_POST, handle_reset);

    server.onNotFound([]() {
            server.send(404, "text/plain", "404: Not Found");
            });

    server.begin();
    Serial.println("HTTP server started");
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
        );
    } else {
        server.send(
            200,
            "text/html",
            "<center>"
            "<img src=\"https://ratgdo.github.io/homekit-ratgdo/qr.png\"/>"
            "</center>"
            "<p>If you wish to re-pair to another HomeKit Home, you must first click the following button:</p>"
            "<form action=\"/reset\" method=\"POST\">"
            "<input type=\"submit\" value=\"Un-pair HomeKit\">"
            "</form>"
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
