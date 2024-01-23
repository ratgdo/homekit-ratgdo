// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// Copyright (c) 2023-24 David Kerr, https://github.com/dkerr64
// All rights reserved. GPLv3 License

#define TAG ("WEB")

#include <esp_system.h>
#include <esp_http_server.h>
#include <esp_log.h>

#include <hap.h>

#define HTTPD_RESP_USE_STRLEN -1  // tell httpd_resp_send to use strlen to calculate the response
                                  // length, so I don't have to pass it myself.

esp_err_t handle_reset(httpd_req_t *req);
esp_err_t handle_reboot(httpd_req_t *req);
// void handle_notfound();      // esp refactor
// void handle_handlestatus();  // esp refactor

static httpd_handle_t server = NULL;
httpd_config_t config = HTTPD_DEFAULT_CONFIG();

httpd_uri_t reset_uri = {
    .uri       = "/reset",
    .method    = HTTP_POST,
    .handler   = handle_reset,
    .user_ctx  = NULL
};

httpd_uri_t reboot_uri = {
    .uri       = "/reboot",
    .method    = HTTP_POST,
    .handler   = handle_reboot,
    .user_ctx  = NULL
};

/********* main loop **********/

void setup_web()
{
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    esp_err_t err = httpd_start(&server, &config) == ESP_OK;
    if (!err) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &reset_uri);
        httpd_register_uri_handler(server, &reboot_uri);
        return;
    }

    ESP_LOGI(TAG, "Error starting server! %s", esp_err_to_name(err));
}

/********* handlers **********/

esp_err_t handle_reset(httpd_req_t *req) {
    ESP_LOGI(TAG, "... reset requested");
    hap_reset_homekit_data();
    const char* resp = "<p>This device has been un-paired from HomeKit.</p><p><a href=\"/\">Back</a></p>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t handle_reboot(httpd_req_t *req) {
    ESP_LOGI(TAG, "... reboot requested");
    const char* resp =
        "<head>"
        "<meta http-equiv=\"refresh\" content=\"15;url=/\" />"
        "</head>"
        "<body>"
        "<p>RATGDO restarting. Please wait. Reconnecting in 15 seconds...</p>"
        "<p><a href=\"/\">Back</a></p>"
        "</body>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    httpd_stop(server);
    hap_reboot_accessory();

    // unreachable
    return ESP_OK;
}

#if 0  // esp refactor

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

#endif // esp refactor
