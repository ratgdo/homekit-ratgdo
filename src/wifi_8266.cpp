/****************************************************************************
 * RATGDO HomeKit
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023 Brandon Matthews... https://github.com/thenewwazoo
 * Copyright (c) 2023-25 David A Kerr... https://github.com/dkerr64/
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 * This approach relies on the ESP8266 Arduino WiFi class storing credentials
 * in EEPROM on our behalf, and its ability to continue to attempt to
 * reconnect. If there are stored credentials, `WiFi.begin()` will use them. If
 * not, Improv will eventually call `WiFi.begin`, and in doing so will store
 * the credentials. If connection does not succeed after 2 seconds, the stored
 * credentials will be erased.
 *
 * Portions of this code written by Jonathas Barbosa <jnths@gmail.com>, and adapted from
 *   https://github.com/jnthas/improv-wifi-demo
 *
 * Contributions acknowledged from
 * Brandon Matthews... https://github.com/thenewwazoo
 * Jonathan Stroud...  https://github.com/jgstroud
 *
 */
#ifdef ESP8266
// This whole file only applies for ESP8266.
// On ESP32, WiFi is handled by the HomeSpan library

// Arduino system includes
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266Ping.h>

// RATGDO project includes
#include "ratgdo.h"
#include "config.h"
#include "softAP.h"
#include "wifi_8266.h"

// Logger tag
static const char *TAG = "ratgdo-wifi";

static bool wifi_setup_done = false;

// support for changeing WiFi settings
_millis_t wifiConnectStart = 0;
bool wifiConnectActive = false;
station_config wifiConf;

uint8_t x_buffer[128];
uint8_t x_position = 0;

WiFiEventHandler connectedHandler;
WiFiEventHandler disconnectedHandler;
WiFiEventHandler gotIPHandler;
WiFiEventHandler dhcpTimeoutHandler;

void onConnected(const WiFiEventStationModeConnected &evt)
{
    ESP_LOGI(TAG, "WiFi connected SSID: %s, Channel: %d", evt.ssid.c_str(), evt.channel);
}

void onDisconnected(const WiFiEventStationModeDisconnected &evt)
{
    ESP_LOGI(TAG, "WiFi disconnected SSID: %s, BSSID: %02x:%02x:%02x:%02x:%02x:%02x, Reason: %d", evt.ssid.c_str(),
             evt.bssid[0], evt.bssid[1], evt.bssid[2], evt.bssid[3], evt.bssid[4], evt.bssid[5], evt.reason);
}

void onGotIP(const WiFiEventStationModeGotIP &evt)
{
    ESP_LOGI(TAG, "WiFi Got IP: %s, Mask: %s, Gateway: %s, DNS: %s", evt.ip.toString().c_str(), evt.mask.toString().c_str(),
             evt.gw.toString().c_str(), (WiFi.dnsIP().isSet()) ? WiFi.dnsIP().toString().c_str() : evt.gw.toString().c_str());
    ESP_LOGI(TAG, "WiFi SSID %s at %ddBm on channel %d to access point %s", WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.channel(), WiFi.BSSIDstr().c_str());

    if (softAPmode)
        return;

    // Update saved IP address info (only if not in soft AP mode)
    userConfig->set(cfg_localIP, evt.ip.toString().c_str());
    userConfig->set(cfg_gatewayIP, evt.gw.toString().c_str());
    userConfig->set(cfg_subnetMask, evt.mask.toString().c_str());
    userConfig->set(cfg_nameserverIP, (WiFi.dnsIP().isSet()) ? WiFi.dnsIP().toString().c_str() : evt.gw.toString().c_str());
    ESP8266_SAVE_CONFIG();
    wifi_got_ip = true;
}

void onDHCPTimeout()
{
    ESP_LOGI(TAG, "WiFi DHCP Timeout");
}

void wifi_connect()
{
    if (wifi_setup_done)
        return;

    ESP_LOGI(TAG, "=== Initialize WiFi %s", (softAPmode) ? "Soft Access Point" : "Station");
    IRAM_START(TAG);
    // IRAM heap is used only for allocating globals, to leave as much regular heap
    // available during operations.  We need to carefully monitor useage so as not
    // to exceed available IRAM.  We can adjust the LOG_BUFFER_SIZE (in log.h) if we
    // need to make more space available for initialization.
    WiFi.persistent(false);
    if (softAPmode)
    {
        ESP_LOGI(TAG, "Start AP mode for: %s", device_name_rfc952);
        bool apStarted = WiFi.softAP(device_name_rfc952);
        if (apStarted)
        {
            ESP_LOGI(TAG, "AP started with IP %s", WiFi.softAPIP().toString().c_str());
        }
        else
        {
            ESP_LOGI(TAG, "Error starting AP mode");
        }
        userConfig->set(cfg_wifiChanged, false);
        wifi_scan();
    }
    else
    {
        if (userConfig->getWifiChanged())
        {
            ESP_LOGI(TAG, "WARNING: WiFi settings changed. Will check for connection after 30 seconds.");
        }
        switch (userConfig->getWifiPhyMode())
        {
        case WIFI_PHY_MODE_11B:
            ESP_LOGI(TAG, "Setting WiFi preference to 802.11b (Wi-Fi 1)");
            break;
        case WIFI_PHY_MODE_11G:
            ESP_LOGI(TAG, "Setting WiFi preference to 802.11g (Wi-Fi 3)");
            break;
        case WIFI_PHY_MODE_11N:
            ESP_LOGI(TAG, "Setting WiFi preference to 802.11n (Wi-Fi 4)");
            break;
        default:
            ESP_LOGI(TAG, "Setting WiFi version preference to automatic");
        }
        WiFi.mode(WIFI_STA);
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        WiFi.setPhyMode((WiFiPhyMode_t)userConfig->getWifiPhyMode());
        if (userConfig->getWifiPower() < 20)
        {
            // Only set WiFi power if set to less than the maximum
            ESP_LOGI(TAG, "Setting WiFi power to %d", userConfig->getWifiPower());
            WiFi.setOutputPower((float)userConfig->getWifiPower());
        }
        WiFi.setAutoReconnect(true); // don't require explicit attempts to reconnect in the main loop

        ESP_LOGI(TAG, "Set WiFi Host Name: %s", device_name_rfc952);
        WiFi.hostname((const char *)device_name_rfc952);

        if (userConfig->getStaticIP())
        {
            IPAddress ip;
            IPAddress gw;
            IPAddress nm;
            IPAddress dns;
            if (ip.fromString(userConfig->getLocalIP()) &&
                gw.fromString(userConfig->getGatewayIP()) &&
                nm.fromString(userConfig->getSubnetMask()) &&
                dns.fromString(userConfig->getNameserverIP()))
            {
                WiFi.config(ip, gw, nm, dns);
            }
            else
            {
                ESP_LOGI(TAG, "Failed to set static IP address, error parsing addresses");
            }
        }
    }
    // Set callbacks so we can monitor connection status
    connectedHandler = WiFi.onStationModeConnected(&onConnected);
    disconnectedHandler = WiFi.onStationModeDisconnected(&onDisconnected);
    gotIPHandler = WiFi.onStationModeGotIP(&onGotIP);
    dhcpTimeoutHandler = WiFi.onStationModeDHCPTimeout(&onDHCPTimeout);

    wifi_station_get_config_default(&wifiConf);
    if (strEmptyOrSpaces((const char *)wifiConf.ssid))
    {
        ESP_LOGE(TAG, "ERROR: Invalid SSID value (%s) boot into soft access point mode", (const char *)wifiConf.ssid);
        userConfig->set(cfg_softAPmode, true);
        ESP8266_SAVE_CONFIG();
        sync_and_restart();
    }
    if (wifiConf.bssid_set)
    {
        ESP_LOGI(TAG, "Connecting to SSID: %s locked to Access Point: %02x:%02x:%02x:%02x:%02x:%02x, ", wifiConf.ssid,
                 wifiConf.bssid[0], wifiConf.bssid[1], wifiConf.bssid[2], wifiConf.bssid[3], wifiConf.bssid[4], wifiConf.bssid[5]);
    }
    else
    {
        ESP_LOGI(TAG, "Connecting to SSID: %s", wifiConf.ssid);
    }
    ESP_LOGI(TAG, "Starting WiFi connecting in background");
    wifiConnectStart = _millis();
    wifiConnectActive = true;
    WiFi.begin(); // use credentials stored in flash
    IRAM_END(TAG);
    wifi_setup_done = true;
}

void wifi_loop()
{
#ifdef GW_PING_CHECK
    static _millis_t gw_ping_start = 0;
    static _millis_t gw_report_start = 0;
    static bool gw_ping_init = false;
    static bool gw_report_init = false;

    if (!gw_ping_init)
    {
        gw_ping_start = _millis();
        gw_ping_init = true;
    }
    if (!gw_report_init)
    {
        gw_report_start = _millis();
        gw_report_init = true;
    }

    // Once a minute ping the Gateway and log
    _millis_t now = _millis();
    if (now - gw_ping_start >= 60000)
    {
        gw_ping_start = now;
        if (Ping.ping(WiFi.gatewayIP(), 1))
        {
            int lat = Ping.averageTime();
            // Log success once an hour
            if ((now - gw_report_start >= 60 * 60 * 1000) || (lat > 100))
            {
                gw_report_start = now;
                ESP_LOGI(TAG, "Gateway %s alive %u ms", WiFi.gatewayIP().toString().c_str(), lat);
            }
        }
        else
        {
            ESP_LOGI(TAG, "No response from Gateway %s", WiFi.gatewayIP().toString().c_str());
        }
    }
#endif

    if (userConfig->getWifiChanged() && wifiConnectActive && (_millis() - wifiConnectStart >= 30000))
    {
        bool connected = (WiFi.status() == WL_CONNECTED);
        ESP_LOGI(TAG, "30 seconds since WiFi settings change, connected to access point: %s", (connected) ? "true" : "false");
        // If not connected, reset to auto.
        if (!connected)
        {
            ESP_LOGI(TAG, "Reset WiFi Power to 20.5 dBm and WiFiPhyMode to: 0");
            userConfig->set(cfg_wifiPower, 20);
            userConfig->set(cfg_wifiPhyMode, 0);
            WiFi.setOutputPower(20.5);
            WiFi.setPhyMode((WiFiPhyMode_t)0);
            ESP8266_SAVE_CONFIG();
            // Now try and reconnect...
            wifiConnectStart = _millis();
            wifiConnectActive = true;
            WiFi.reconnect();
            return;
        }
        else
        {
            ESP_LOGI(TAG, "Connected, test Gatway IP reachable");
            if (!Ping.ping(WiFi.gatewayIP(), 1))
            {
                ESP_LOGI(TAG, "Unable to ping Gateway, reset to DHCP to acquire IP address and reconnect");
                userConfig->set(cfg_staticIP, false);
                ESP8266_SAVE_CONFIG();
                IPAddress ip;
                ip.fromString("0.0.0.0");
                WiFi.config(ip, ip, ip);
                // Now try and reconnect...
                wifiConnectStart = _millis();
                wifiConnectActive = true;
                WiFi.reconnect();
                return;
            }
            else
            {
                ESP_LOGI(TAG, "Gateway %s alive %u ms", WiFi.gatewayIP().toString().c_str(), Ping.averageTime());
            }
        }
        // reset flag
        userConfig->set(cfg_wifiChanged, false);
        ESP8266_SAVE_CONFIG();
    }
}
#endif
