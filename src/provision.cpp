/****************************************************************************
 * RATGDO HomeKit
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-25 David A Kerr... https://github.com/dkerr64/
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 * With thanks to
 * https://github.com/jnthas/improv-wifi-demo
 *
 */

// C/C++ language includes
// none

// Arduino includes
// none

// 3rd party includes
#include <improv.h>

// RATGDO project includes
#include "ratgdo.h"
#include "config.h"
#include "provision.h"
#include "softAP.h"
#include "wifi_8266.h"

// Logger tag
static const char *TAG = "ratgdo-improv";

static bool improv_setup_done = false;

#define MAX_ATTEMPTS_WIFI_CONNECTION 20

void blink_led(int d, int times)
{
    for (int j = 0; j < times; j++)
    {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(d);
        digitalWrite(LED_BUILTIN, LOW);
        delay(d);
    }
}

void setup_improv()
{
#ifndef ESP8266
    ESP_LOGI(TAG, "Disable HomeSpan logging and serial port input");
    // This is necessary so as not to interfere with Improv use of serial port
    homeSpan.setLogLevel(-1);
    homeSpan.setSerialInputDisable(true);
#endif
    blink_led(100, 5);
    improv_setup_done = true;
    return;
}

void set_state(improv::State state)
{

    std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
    data.resize(11);
    data[6] = improv::IMPROV_SERIAL_VERSION;
    data[7] = improv::TYPE_CURRENT_STATE;
    data[8] = 1;
    data[9] = state;

    uint8_t checksum = 0x00;
    for (uint8_t d : data)
        checksum += d;
    data[10] = checksum;

    Serial.write(data.data(), data.size());
}

void send_response(std::vector<uint8_t> &response)
{
    std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
    data.resize(9);
    data[6] = improv::IMPROV_SERIAL_VERSION;
    data[7] = improv::TYPE_RPC_RESPONSE;
    data[8] = response.size();
    data.insert(data.end(), response.begin(), response.end());

    uint8_t checksum = 0x00;
    for (uint8_t d : data)
        checksum += d;
    data.push_back(checksum);

    Serial.write(data.data(), data.size());
}

void set_error(improv::Error error)
{
    std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
    data.resize(11);
    data[6] = improv::IMPROV_SERIAL_VERSION;
    data[7] = improv::TYPE_ERROR_STATE;
    data[8] = 1;
    data[9] = error;

    uint8_t checksum = 0x00;
    for (uint8_t d : data)
        checksum += d;
    data[10] = checksum;

    Serial.write(data.data(), data.size());
}

std::vector<std::string> getLocalUrl()
{
    return {
        // URL where user can finish onboarding or use device
        // Recommended to use website hosted by device
        String("http://" + WiFi.localIP().toString()).c_str()};
}

void getAvailableWifiNetworks()
{
    wifi_scan();
    String currentSSID = "";
    for (wifiNet_t net : wifiNets)
    {
        // wifiNets may have multiple entries for a SSID sorted by RSSI,
        // we use the first (strongest signal) in the list.
        if (currentSSID != net.ssid)
        {
            currentSSID = net.ssid;
#ifdef ESP8266
            std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_WIFI_NETWORKS,
                                                                   {net.ssid, String(net.rssi),
                                                                    (net.encryptionType == AUTH_OPEN ? "NO" : "YES")},
                                                                   false);
#else
            std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_WIFI_NETWORKS,
                                                                   {net.ssid, String(net.rssi),
                                                                    (net.encryptionType == WIFI_AUTH_OPEN ? "NO" : "YES")},
                                                                   false);
#endif
            send_response(data);
            delay(1);
        }
    }
    // final response
    std::vector<uint8_t> data =
        improv::build_rpc_response(improv::GET_WIFI_NETWORKS, std::vector<std::string>{}, false);
    send_response(data);
}

void onErrorCallback(improv::Error err)
{
    blink_led(2000, 3);
}

bool onCommandCallback(improv::ImprovCommand cmd)
{
    // As soon as we recognize an Improv command we must suppress any logging
    // to serial port so as not to interfere with Improv comms.
    // Reset only on reboot.
    suppressSerialLog = true;

    switch (cmd.command)
    {
    case improv::Command::GET_CURRENT_STATE:
    {
        if ((WiFi.status() == WL_CONNECTED))
        {
            set_state(improv::State::STATE_PROVISIONED);
            std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_CURRENT_STATE, getLocalUrl(), false);
            send_response(data);
        }
        else
        {
            set_state(improv::State::STATE_AUTHORIZED);
        }

        break;
    }

    case improv::Command::WIFI_SETTINGS:
    {
        if (cmd.ssid.length() == 0)
        {
            set_error(improv::Error::ERROR_INVALID_RPC);
            break;
        }

        set_state(improv::STATE_PROVISIONING);

        if (connect_wifi(cmd.ssid.c_str(), cmd.password.c_str()))
        {

            blink_led(100, 3);
#ifdef ESP8266
            WiFi.persistent(true); // Set persist to store wifi credentials
            // Call begin with connect = false because we are allready connected in fn connect_wifi()
            WiFi.begin(cmd.ssid.c_str(), cmd.password.c_str(), 0, NULL, false);
            WiFi.persistent(false); // clear the persist flag so other settings do not get written to flash
#else
            homeSpan.setWifiCredentials(cmd.ssid.c_str(), cmd.password.c_str());
#endif
            userConfig->set(cfg_staticIP, false);
            userConfig->set(cfg_wifiPower, WIFI_POWER_MAX);
            userConfig->set(cfg_wifiPhyMode, 0);
            userConfig->set(cfg_timeZone, "");

            set_state(improv::STATE_PROVISIONED);
            std::vector<uint8_t> data = improv::build_rpc_response(improv::WIFI_SETTINGS, getLocalUrl(), false);
            send_response(data);
            delay(100);
            sync_and_restart();
        }
        else
        {
            set_state(improv::STATE_STOPPED);
            set_error(improv::Error::ERROR_UNABLE_TO_CONNECT);
        }

        break;
    }

    case improv::Command::GET_DEVICE_INFO:
    {
        std::vector<std::string> infos = {
            // Firmware name
            "HomeKit-ratgdo32",
            // Firmware version
            AUTO_VERSION,
            // Hardware chip/variant
            "ESP32",
            // Device name
            "Ratgdo32"};
        std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_DEVICE_INFO, infos, false);
        send_response(data);
        break;
    }

    case improv::Command::GET_WIFI_NETWORKS:
    {
        getAvailableWifiNetworks();
        break;
    }

    default:
    {
        set_error(improv::ERROR_UNKNOWN_RPC);
        return false;
    }
    }

    return true;
}

void improv_loop()
{
    static uint8_t x_buffer[16];
    static uint8_t x_position = 0;

    if (!improv_setup_done)
        return;

    if (Serial.available() > 0)
    {
        uint8_t b = Serial.read();

        if (improv::parse_improv_serial_byte(x_position, b, x_buffer, onCommandCallback, onErrorCallback))
        {
            x_buffer[x_position++] = b;
        }
        else
        {
            x_position = 0;
        }
    }
}
