// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

/* WiFi configuration and setup
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
 */

// #if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266Ping.h>
// #elif defined(ESP32)
// #include <WiFi.h>
// #endif
#include "improv.h"
#include <Arduino.h>
#include "ratgdo.h"
#include "log.h"
#include "utilities.h"
#include "wifi.h"

// support for changeing WiFi settings
unsigned long wifiConnectTimeout = 0;
// support for scaning WiFi networks
bool wifiNetsCmp(wifiNet_t a, wifiNet_t b)
{
    // Sorts first by SSID and then by RSSI so strongest signal first.
    return (a.ssid < b.ssid) || ((a.ssid == b.ssid) && (a.rssi > b.rssi));
}
std::multiset<wifiNet_t, decltype(&wifiNetsCmp)> wifiNets(&wifiNetsCmp);
station_config wifiConf;

#define MAX_ATTEMPTS_WIFI_CONNECTION 30
uint8_t x_buffer[128];
uint8_t x_position = 0;

void set_error(improv::Error error);
void send_response(std::vector<uint8_t> &response);
void set_state(improv::State state);
void get_available_wifi_networks();
bool on_command_callback(improv::ImprovCommand cmd);
void on_error_callback(improv::Error err);

WiFiEventHandler connectedHandler;
WiFiEventHandler disconnectedHandler;
WiFiEventHandler gotIPHandler;
WiFiEventHandler dhcpTimeoutHandler;

void onConnected(const WiFiEventStationModeConnected &evt)
{
    RINFO("WiFi connected SSID: %s, Channel: %d", evt.ssid.c_str(), evt.channel);
}

void onDisconnected(const WiFiEventStationModeDisconnected &evt)
{
    RINFO("WiFi disconnected SSID: %s, BSSID: %02x:%02x:%02x:%02x:%02x:%02x, Reason: %d", evt.ssid.c_str(),
          evt.bssid[0], evt.bssid[1], evt.bssid[2], evt.bssid[3], evt.bssid[4], evt.bssid[5], evt.reason);
}

void onGotIP(const WiFiEventStationModeGotIP &evt)
{
    strlcpy(userConfig->IPaddress, evt.ip.toString().c_str(), sizeof(userConfig->IPaddress));
    strlcpy(userConfig->IPnetmask, evt.mask.toString().c_str(), sizeof(userConfig->IPnetmask));
    strlcpy(userConfig->IPgateway, evt.gw.toString().c_str(), sizeof(userConfig->IPgateway));
    strlcpy(userConfig->IPnameserver, (WiFi.dnsIP().isSet()) ? WiFi.dnsIP().toString().c_str() : evt.gw.toString().c_str(), sizeof(userConfig->IPnameserver));
    write_config_to_file();
    RINFO("WiFi Got IP: %s, Mask: %s, Gateway: %s, DNS: %s", userConfig->IPaddress, userConfig->IPnetmask, userConfig->IPgateway, userConfig->IPnameserver);
}

void onDHCPTimeout()
{
    RINFO("WiFi DHCP Timeout");
}

void wifi_scan()
{
    // scan for networks
    RINFO("Scanning for networks...");
    wifiNet_t wifiNet;
    wifiNets.clear();
    int nNets = std::min((int)WiFi.scanNetworks(), 127);
    RINFO("Found %d networks", nNets);
    for (int i = 0; i < nNets; i++)
    {
        wifiNet.ssid = WiFi.SSID(i);
        wifiNet.channel = WiFi.channel(i);
        wifiNet.rssi = WiFi.RSSI(i);
        memcpy(wifiNet.bssid, WiFi.BSSID(i), sizeof(wifiNet.bssid));
        RINFO("Network: %s (Ch:%d, %ddBm) AP: %s", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.BSSIDstr(i).c_str());
        wifiNets.insert(wifiNet);
    }
    // delete scan from memory
    WiFi.scanDelete();
}

void wifi_connect()
{
    RINFO("=== Initialize WiFi %s", (softAPmode) ? "Soft Access Point" : "Station");
    IRAM_START
    // IRAM heap is used only for allocating globals, to leave as much regular heap
    // available during operations.  We need to carefully monitor useage so as not
    // to exceed available IRAM.  We can adjust the LOG_BUFFER_SIZE (in log.h) if we
    // need to make more space available for initialization.
    WiFi.persistent(false);
    if (softAPmode)
    {
        RINFO("Start AP mode for: %s", device_name_rfc952);
        bool apStarted = WiFi.softAP(device_name_rfc952);
        if (apStarted)
        {
            RINFO("AP started with IP %s", WiFi.softAPIP().toString().c_str());
        }
        else
        {
            RINFO("Error starting AP mode");
        }
        userConfig->wifiSettingsChanged = false;
        wifi_scan();
    }
    else
    {
        if (userConfig->wifiSettingsChanged)
        {
            RINFO("WARNING: WiFi settings changed. Will check for connection after 30 seconds.");
        }
        switch (userConfig->wifiPhyMode)
        {
        case WIFI_PHY_MODE_11B:
            RINFO("Setting WiFi preference to 802.11b (Wi-Fi 1)");
            break;
        case WIFI_PHY_MODE_11G:
            RINFO("Setting WiFi preference to 802.11g (Wi-Fi 3)");
            break;
        case WIFI_PHY_MODE_11N:
            RINFO("Setting WiFi preference to 802.11n (Wi-Fi 4)");
            break;
        default:
            RINFO("Setting WiFi version preference to automatic");
        }
        WiFi.mode(WIFI_STA);
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        WiFi.setPhyMode((WiFiPhyMode_t)userConfig->wifiPhyMode);
        if (userConfig->wifiPower < 20)
        {
            // Only set WiFi power if set to less than the maximum
            RINFO("Setting WiFi power to %d", userConfig->wifiPower);
            WiFi.setOutputPower((float)userConfig->wifiPower);
        }
        WiFi.setAutoReconnect(true); // don't require explicit attempts to reconnect in the main loop

        RINFO("Set WiFi Host Name: %s", device_name_rfc952);
        WiFi.hostname((const char *)device_name_rfc952);

        if (userConfig->staticIP)
        {
            IPAddress ip;
            IPAddress gw;
            IPAddress nm;
            IPAddress dns;
            if (ip.fromString(userConfig->IPaddress) &&
                gw.fromString(userConfig->IPgateway) &&
                nm.fromString(userConfig->IPnetmask) &&
                dns.fromString(userConfig->IPnameserver))
            {
                WiFi.config(ip, gw, nm, dns);
            }
            else
            {
                RINFO("Failed to set static IP address, error parsing addresses");
            }
        }
    }
    // Set callbacks so we can monitor connection status
    connectedHandler = WiFi.onStationModeConnected(&onConnected);
    disconnectedHandler = WiFi.onStationModeDisconnected(&onDisconnected);
    gotIPHandler = WiFi.onStationModeGotIP(&onGotIP);
    dhcpTimeoutHandler = WiFi.onStationModeDHCPTimeout(&onDHCPTimeout);

    wifi_station_get_config_default(&wifiConf);
    if (wifiConf.bssid_set)
    {
        RINFO("Connecting to SSID: %s locked to Access Point: %02x:%02x:%02x:%02x:%02x:%02x, ", wifiConf.ssid,
              wifiConf.bssid[0], wifiConf.bssid[1], wifiConf.bssid[2], wifiConf.bssid[3], wifiConf.bssid[4], wifiConf.bssid[5]);
    }
    else
    {
        RINFO("Connecting to SSID: %s", wifiConf.ssid);
    }
    RINFO("Starting WiFi connecting in background");
    wifiConnectTimeout = millis() + 30000;
    WiFi.begin(); // use credentials stored in flash
    IRAM_END("Wifi initialized");
}

void improv_loop()
{
    loop_id = LOOP_IMPROV;
    // Once a minute ping the Gateway and log
    unsigned long now = millis();
    if (now % 60000 == 0) {
        if (Ping.ping(WiFi.gatewayIP(), 1)) {
            int lat = Ping.averageTime();
            // Log success once an hour
            if (now % (60000 * 60) == 0 || lat > 100)
                RINFO("Gateway %s alive %u ms", WiFi.gatewayIP().toString().c_str(), lat);
        }
        else {
            RINFO("No response from Gateway %s", WiFi.gatewayIP().toString().c_str());
        }
    }
    if (Serial.available() > 0)
    {
        uint8_t b = Serial.read();

        if (parse_improv_serial_byte(x_position, b, x_buffer, on_command_callback, on_error_callback))
        {
            x_buffer[x_position++] = b;
        }
        else
        {
            x_position = 0;
        }
    }

    if (softAPmode && (millis() > 10 * 60 * 1000))
    {
        RINFO("In Soft Access Point mode for over 10 minutes, reboot");
        sync_and_restart();
        return;
    }

    if (userConfig->wifiSettingsChanged && (millis() > wifiConnectTimeout))
    {
        bool connected = (WiFi.status() == WL_CONNECTED);
        RINFO("30 seconds since WiFi settings change, connected to access point: %s", (connected) ? "true" : "false");
        // If not connected, reset to auto.
        if (!connected)
        {
            RINFO("Reset WiFi Power to 20.5 dBm and WiFiPhyMode to: 0");
            userConfig->wifiPower = 20;
            userConfig->wifiPhyMode = 0;
            WiFi.setOutputPower(20.5);
            WiFi.setPhyMode((WiFiPhyMode_t)0);
            write_config_to_file();
            // Now try and reconnect...
            wifiConnectTimeout = millis() + 30000;
            WiFi.reconnect();
            return;
        }
        else
        {
            RINFO("Connected, test Gatway IP reachable");
            IPAddress ip;
            if (!Ping.ping(WiFi.gatewayIP(), 1))
            {
                RINFO("Unable to ping Gateway, reset to DHCP to acquire IP address and reconnect");
                userConfig->staticIP = false;
                write_config_to_file();
                IPAddress ip;
                ip.fromString("0.0.0.0");
                WiFi.config(ip, ip, ip);
                // Now try and reconnect...
                wifiConnectTimeout = millis() + 30000;
                WiFi.reconnect();
                return;
            } else {
                RINFO("Gateway %s alive %u ms", WiFi.gatewayIP().toString().c_str(), Ping.averageTime());
            }
        }
        // reset flag
        userConfig->wifiSettingsChanged = false;
        write_config_to_file();
    }
}

bool connect_wifi(const std::string &ssid, const std::string &password)
{
    return connect_wifi(ssid, password, NULL);
}

bool connect_wifi(const std::string &ssid, const std::string &password, const uint8_t *bssid)
{
    uint8_t count = 0;

    WiFi.persistent(true); // Set persist to store wifi credentials
    WiFi.begin(ssid.c_str(), password.c_str(), 0, bssid);
    WiFi.persistent(false); // clear the persist flag so other settings do not get written to flash

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        yield();
        if (count > MAX_ATTEMPTS_WIFI_CONNECTION)
        {
            WiFi.disconnect();
            return false;
        }
        count++;
    }
    return true;
}

std::vector<std::string> get_local_url()
{
    return {
        // TODO
        // URL where user can finish onboarding or use device
        // Recommended to use website hosted by device
        String("http://" + WiFi.localIP().toString()).c_str()};
}

void on_error_callback(improv::Error err)
{
    RERROR("improv error: %02X", err);
}

bool on_command_callback(improv::ImprovCommand cmd)
{

    switch (cmd.command)
    {
    case improv::Command::GET_CURRENT_STATE:
    {
        if ((WiFi.status() == WL_CONNECTED))
        {
            set_state(improv::State::STATE_PROVISIONED);
            std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_CURRENT_STATE, get_local_url(), false);
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

        if (connect_wifi(cmd.ssid, cmd.password))
        {
            set_state(improv::STATE_PROVISIONED);
            std::vector<uint8_t> data = improv::build_rpc_response(improv::WIFI_SETTINGS, get_local_url(), false);
            send_response(data);
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
            DEVICE_NAME,
            AUTO_VERSION,
            CHIP_FAMILY,
            MODEL_NAME};
        std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_DEVICE_INFO, infos, false);
        send_response(data);
        break;
    }

    case improv::Command::GET_WIFI_NETWORKS:
    {
        get_available_wifi_networks();
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

void get_available_wifi_networks()
{
    int networkNum = WiFi.scanNetworks();

    int sortedIndicies[networkNum];
    for (int i = 0; i < networkNum; i++)
    {
        sortedIndicies[i] = i;
    }

    // sort networks by RSSI, strongest to weakest
    for (int i = 0; i < networkNum; i++)
    {
        for (int j = i + 1; j < networkNum; j++)
        {
            if (WiFi.RSSI(sortedIndicies[j]) > WiFi.RSSI(sortedIndicies[i]))
            {
                std::swap(sortedIndicies[i], sortedIndicies[j]);
            }
        }
    }

    // find duplicates (must be RSSI sorted)
    String temp_ssid;
    for (int i = 0; i < networkNum; i++)
    {
        if (sortedIndicies[i] == -1)
            continue; // skip duplicate
        temp_ssid = WiFi.SSID(sortedIndicies[i]);
        for (int j = i + 1; j < networkNum; j++)
        {
            if (temp_ssid == WiFi.SSID(sortedIndicies[j]))
            {
                sortedIndicies[j] = -1; // set dupes to -1 to skip later
            }
        }
    }

    for (int id = 0; id < networkNum; ++id)
    {
        if (sortedIndicies[id] == -1)
            continue; // skip duplicate
        std::vector<uint8_t> data = improv::build_rpc_response(
            improv::GET_WIFI_NETWORKS, {WiFi.SSID(sortedIndicies[id]), String(WiFi.RSSI(sortedIndicies[id])), (WiFi.encryptionType(sortedIndicies[id]) == ENC_TYPE_NONE ? "NO" : "YES")}, false);
        send_response(data);
        delay(1);
    }
    // final response
    std::vector<uint8_t> data =
        improv::build_rpc_response(improv::GET_WIFI_NETWORKS, std::vector<std::string>{}, false);
    send_response(data);

    // delete scan from memory
    WiFi.scanDelete();
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
