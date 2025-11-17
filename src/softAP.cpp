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
 *
 */

// Arduino system includes
#include <DNSServer.h>

// RATGDO project includes
#include "ratgdo.h"
#include "utilities.h"
#include "config.h"
#include "softAP.h"
#include "web.h"
#include "provision.h"

// Logger tag
static const char *TAG = "ratgdo-softAP";

static const char softAPhttpPreamble[] PROGMEM = "HTTP/1.1 200 OK\nContent-Type: text/html\nCache-Control: no-cache, no-store\n\n<!DOCTYPE html>\n";
// TODO enable advanced mode (AP selection), disabled below by setting display = none
static const char softAPtableHead[] PROGMEM = R"(
<tr style='display:none;'><td></td><td><input id='adv' name='advanced' type='checkbox' onclick='showAdvanced(this.checked)'/><label for='adv'>&nbsp;Advanced</label></td></tr>
<tr><th></th><th>&nbsp;&nbsp;SSID</th><th>RSSI</th><th>Chan</th><th>Hardware BSSID</th></tr>)";
static const char softAPtableRow[] PROGMEM = R"(
<tr %s><td></td><td><input type='radio' id='net%d' name='net' value='%d' %s/><label for='net%d'>&nbsp;%s</label></td><td>%ddBm</td><td>%d</td><td>&nbsp;&nbsp;%02x:%02x:%02x:%02x:%02x:%02x</td></tr>)";
static const char softAPtableLastRow[] PROGMEM = R"(
<tr><td></td><td><input type='radio' id='net%d' name='net' value='%d'/>&nbsp;<label><input type='text' id='userSSID' name='userSSID' placeholder='SSID' value='%s'/></label></td></tr>)";
static const char softAPsuccess[] PROGMEM = R"(<html><head><title>Success</title></head><body>Success</body><script type='text/javascript'>window.location.href = '/';</script></html>\n)";
static const char softAPempty[] PROGMEM = R"(HTTP/1.1 204 No Content\nContent-Length: 0\n\n)";

// forward declare functions
void handle_softAPweb();
void handle_wifinets();

#define MAX_ATTEMPTS_WIFI_CONNECTION 30
#define TXT_BUFFER_SIZE 1024

DNSServer dnsServer;

// support for scaning WiFi networks
bool wifiNetsCmp(const wifiNet_t &a, const wifiNet_t &b)
{
    // Sorts first by SSID and then by RSSI so strongest signal first.
    return (a.ssid < b.ssid) || ((a.ssid == b.ssid) && (a.rssi > b.rssi));
}
std::multiset<wifiNet_t, decltype(&wifiNetsCmp)> wifiNets(&wifiNetsCmp);

static bool softAPinitialized = false;

char *getServiceName(char *service_name, size_t max)
{
    const char *ssid_prefix = "RATGDO_";
#ifdef ESP8266
    snprintf(service_name, max, "%s%06X", ssid_prefix, ESP.getChipId());
#else
    uint8_t mac[6];
    Network.macAddress(mac);
    snprintf(service_name, max, "%s%02X%02X%02X", ssid_prefix, mac[3], mac[4], mac[5]);
#endif
    return service_name;
}

const char *encryptionToString(uint16_t e)
{
    // static char *str[16];
#ifdef ESP32
    switch (e)
    {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA+WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2-EAP";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2+WPA3";
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI";
    default:
        return "UNKNOWN";
    }
#else
    switch (e)
    {
    case ENC_TYPE_WEP:
        return PSTR("WEP");
    case ENC_TYPE_TKIP:
        return PSTR("WPA");
    case ENC_TYPE_CCMP:
        return PSTR("WPA2");
    case ENC_TYPE_NONE:
        return PSTR("OPEN");
    case ENC_TYPE_AUTO:
        return PSTR("AUTO");
    default:
        return PSTR("UNKNOWN");
    }
#endif
}

void wifi_scan()
{
    // scan for networks
    ESP_LOGI(TAG, "Scanning WiFi Networks...");
    wifiNet_t wifiNet;
    wifiNets.clear();
    int nNets = std::min((int)WiFi.scanNetworks(), 127);
    if (nNets == 0)
    {
        ESP_LOGI(TAG, "No networks found!");
    }
    else
    {
        static const char d[] PROGMEM = "----------------------------------------";
        ESP_LOGI(TAG, "%-32.32s  %17.17s  %4.4s  %4.4s  %12.12s", "SSID", "BSSID", "RSSI", "CHAN", "ENCRYPTION");
        ESP_LOGI(TAG, "%-32.32s  %17.17s  %4.4s  %4.4s  %12.12s", d, d, d, d, d);
        for (int i = 0; i < nNets; i++)
        {
            wifiNet.ssid = WiFi.SSID(i);
            wifiNet.channel = WiFi.channel(i);
            wifiNet.rssi = WiFi.RSSI(i);
            memcpy(wifiNet.bssid, WiFi.BSSID(i), sizeof(wifiNet.bssid));
            wifiNet.encryptionType = WiFi.encryptionType(i);
            ESP_LOGI(TAG, "%-32.32s  %17.17s  %4ld  %4ld  %12.12s",
                     wifiNet.ssid.c_str(), WiFi.BSSIDstr(i).c_str(),
                     wifiNet.rssi, wifiNet.channel,
                     encryptionToString(wifiNet.encryptionType));
            wifiNets.insert(wifiNet);
        }
    }
    // delete scan from memory
    WiFi.scanDelete();
}

void start_soft_ap()
{
    softAPmode = true;
    ESP_LOGI(TAG, "Start AP mode for: %s", device_name_rfc952);
    WiFi.persistent(false);
    WiFi.setSleep(WIFI_PS_NONE); // Improves performance, at cost of power consumption
    bool apStarted = WiFi.softAP(device_name_rfc952);
    if (apStarted)
    {
        ESP_LOGI(TAG, "AP started with IP %s", WiFi.softAPIP().toString().c_str());
    }
    else
    {
        ESP_LOGI(TAG, "Error starting AP mode");
    }

    // any dns request will point to us
    dnsServer.start(53, "*", WiFi.softAPIP());

    server.onNotFound(handle_softAPweb);
    server.begin();
    ESP_LOGI(TAG, "Soft AP web server started");
#ifdef ESP32
    // used in Android 11+ (but docs suggest it will fall back to http probe)
    WiFi.AP.enableDhcpCaptivePortal();
#endif
    softAPinitialized = true;
}

void soft_ap_loop()
{
    if (!softAPinitialized)
        return;

    server.handleClient();

    if (!softAPmode)
        return;

#ifdef ESP8266
    dnsServer.processNextRequest();
#endif

    static _millis_t soft_ap_start = 0;
    static bool soft_ap_timer_started = false;

    if (!soft_ap_timer_started)
    {
        soft_ap_start = _millis();
        soft_ap_timer_started = true;
    }

    if (_millis() - soft_ap_start > 10 * 60 * 1000)
    {
        ESP_LOGI(TAG, "In Soft Access Point mode for over 10 minutes, reboot");
        sync_and_restart();
        return;
    }
}

void doRedirect(const char *location = "/captive")
{
    // Redirect to root
    ESP_LOGD(TAG, "Send redirect to %s", location);
    server.sendHeader(F("Location"), location, true);
    server.send_P(302, type_txt, "");
}

void handle_softAPweb()
{
    if ((WiFi.getMode() & WIFI_AP) != WIFI_AP)
        return;

    // If we are in Soft Access Point mode
    HTTPMethod method = server.method();
    String page = server.uri();
    static bool cnaHasLoaded = false;
    static uint32_t requests = 0;

    ESP_LOGD(TAG, "WiFi Soft Access Point mode, requesting: %s", page.c_str());

    // captive portal probes
    // apple (ios)
    if (page.equals("/hotspot-detect.html"))
    {
        ESP_LOGI(TAG, "Captive-Portal (apple) request");
        return doRedirect();
    }
    // android
    else if (page.equals("/generate_204"))
    {
        ESP_LOGI(TAG, "Captive-Portal (android) request %d", requests);
        if (!cnaHasLoaded)
        {
            doRedirect();
            requests++;
        }
        else
        {
            server.sendContent(softAPempty, strlen(softAPempty));
            ESP_LOGD(TAG, "Sent 204 No Content (android)");
            requests = 0;
            cnaHasLoaded = false;
        }
        return;
    }
    else if (page.equals("/captive"))
    {
        server.sendContent(softAPhttpPreamble, strlen(softAPhttpPreamble));
        server.sendContent(softAPsuccess, strlen(softAPsuccess));
        server.sendContent("\n", 1);
        ESP_LOGD(TAG, "Sent 200 OK (with Success and page load)");
        return;
    }

    /*
    // windows (redirect)
    else if (page.equals("/redirect"))
        handle_softAPPortal();
    // windows 10+
    else if (page.equals("/connecttest.txt"))
    {
        // windows 11 workaround
        // server.sendHeader("Location", "http://logout.net", true);
        // server.send_P(302, type_txt, "");

        server.send_P(200, type_txt, "Microsoft Connect Test");
    }
    */

    // our pages
    else if (page.equals("/") || page.equals("/wifiap"))
    {
        return handle_wifiap();
    }
    else if (page.equals("/wifiap.css"))
    {
        return load_page("/wifiap.css");
    }
    else if (page.equals("/wifinets"))
    {
        cnaHasLoaded = true;
        return handle_wifinets();
    }
    else if (page.equals("/setssid") && method == HTTP_POST)
    {
        return handle_setssid();
    }
    else if (page.equals("/reboot") && method == HTTP_POST)
    {
        return handle_reboot();
    }
    else if (page.equals("/rescan") && method == HTTP_POST)
    {
        return handle_rescan();
    }
    else
    {
        return handle_notfound();
    }
}

void handle_rescan()
{
    wifi_scan();
    server.send_P(200, type_txt, "Scan complete.");
}

void handle_wifiap()
{
    return load_page("/wifiap.html");
}

void handle_wifinets()
{
    bool connected = WiFi.isConnected();
    String previousSSID = "";
    bool match = false;
    if (connected)
    {
        previousSSID = WiFi.SSID();
    }
    ESP_LOGI(TAG, "Number of WiFi networks: %d", wifiNets.size());
    String currentSSID = "";
    server.client().setNoDelay(true);
    server.sendContent(softAPhttpPreamble, strlen(softAPhttpPreamble));
    server.sendContent(softAPtableHead, strlen(softAPtableHead));
    int i = 0;
    char *txtBuffer = static_cast<char *>(malloc(TXT_BUFFER_SIZE));
    for (wifiNet_t net : wifiNets)
    {
        bool hide = true;
        bool matchSSID = (previousSSID == net.ssid);
        if (matchSSID)
            match = true;
        if (currentSSID != net.ssid)
        {
            currentSSID = net.ssid;
            hide = false;
        }
        else
        {
            matchSSID = false;
        }
        snprintf(txtBuffer, TXT_BUFFER_SIZE, softAPtableRow, (hide) ? "class='adv'" : "", i, i, (matchSSID) ? "checked='checked'" : "", i,
                 net.ssid.c_str(), net.rssi, net.channel,
                 net.bssid[0], net.bssid[1], net.bssid[2], net.bssid[3], net.bssid[4], net.bssid[5]);
        server.sendContent(txtBuffer, strlen(txtBuffer));
        i++;
    }
    // user entered value
    snprintf(txtBuffer, TXT_BUFFER_SIZE, softAPtableLastRow, i, i, (!match) ? previousSSID.c_str() : "");
    server.sendContent(txtBuffer, strlen(txtBuffer));
    server.sendContent("\n", 1);
    server.client().stop();
    free(txtBuffer);
    return;
}

void handle_setssid()
{
    if (server.args() < 3)
    {
        ESP_LOGI(TAG, "Sending %s, for: %s as invalid number of args", response400invalid, server.uri().c_str());
        server.send_P(400, type_txt, response400invalid);
        return;
    }

    const unsigned int net = atoi(server.arg("net").c_str());
    String ssid = server.arg("userSSID");
    bool advanced = server.arg("advanced").equals("on");
    wifiNet_t wifiNet;

    if (net < wifiNets.size())
    {
        // User selected network from within scanned range
        wifiNet = *std::next(wifiNets.begin(), net);
        ssid = wifiNet.ssid;
    }
    else
    {
        // Outside scanned range, do not allow locking to access point
        advanced = false;
    }

    char *txtBuffer = static_cast<char *>(malloc(TXT_BUFFER_SIZE));
    if (advanced)
    {
        ESP_LOGI(TAG, "Requested WiFi SSID: %s (%d) at AP: %02x:%02x:%02x:%02x:%02x:%02x",
                 ssid.c_str(), net, wifiNet.bssid[0], wifiNet.bssid[1], wifiNet.bssid[2], wifiNet.bssid[3], wifiNet.bssid[4], wifiNet.bssid[5]);
        snprintf_P(txtBuffer, TXT_BUFFER_SIZE, PSTR("Setting SSID to: %s locked to Access Point: %02x:%02x:%02x:%02x:%02x:%02x\nRATGDO rebooting.\nPlease wait 30 seconds and connect to RATGDO on new network."),
                   ssid.c_str(), wifiNet.bssid[0], wifiNet.bssid[1], wifiNet.bssid[2], wifiNet.bssid[3], wifiNet.bssid[4], wifiNet.bssid[5]);
    }
    else
    {
        ESP_LOGI(TAG, "Requested WiFi SSID: %s (%d)", ssid.c_str(), net);
        snprintf_P(txtBuffer, TXT_BUFFER_SIZE, PSTR("Setting SSID to: %s\nRATGDO rebooting.\nPlease wait 30 seconds and connect to RATGDO on new network."), ssid.c_str());
    }
    server.client().setNoDelay(true);
    server.send_P(200, type_txt, txtBuffer);
    delay(500);
    free(txtBuffer);

    set_new_ssid(ssid.c_str(), server.arg("pw").c_str(), (advanced) ? wifiNet.bssid : NULL);
    sync_and_restart();
    return;
}

bool set_new_ssid(const char *ssid, const char *password, const uint8_t *bssid)
{
    const bool connected = WiFi.isConnected();
    String previousSSID;
    String previousPSK;
    String previousBSSID;
    if (connected)
    {
        previousSSID = WiFi.SSID();
        previousPSK = WiFi.psk();
        previousBSSID = WiFi.BSSIDstr();
        ESP_LOGI(TAG, "Current SSID: %s / BSSID:%s", previousSSID.c_str(), previousBSSID.c_str());
        WiFi.disconnect();
    }
    ESP_LOGI(TAG, "Attempt to connect to %s", ssid);
    if (connect_wifi(ssid, password, bssid))
    {
        ESP_LOGI(TAG, "WiFi Successfully connects to SSID: %s", ssid);
#ifdef ESP8266
        WiFi.persistent(true); // Set persist to store wifi credentials
        // Call begin with connect = false because we are allready connected in fn connect_wifi()
        WiFi.begin(ssid, password, 0, bssid);
        WiFi.persistent(false); // clear the persist flag so other settings do not get written to flash
#else
        homeSpan.setWifiCredentials(ssid, password);
#endif
        // We should reset WiFi if changing networks or were not currently connected.
        if (!connected || previousBSSID != ssid)
        {
            userConfig->set(cfg_staticIP, false);
            userConfig->set(cfg_wifiPower, WIFI_POWER_MAX);
            userConfig->set(cfg_wifiPhyMode, 0);
            userConfig->set(cfg_timeZone, "");
        }
        return true;
    }
    else
    {
        ESP_LOGI(TAG, "WiFi Failed to connect to SSID: %s", ssid);
        if (connected)
        {
            ESP_LOGI(TAG, "Resetting WiFi to previous SSID: %s, removing any Access Point BSSID lock", previousSSID.c_str());
            connect_wifi(previousSSID.c_str(), previousPSK.c_str());
        }
        else
        {
            // We were not connected, and we failed to connext to new SSID,
            // so best to reset any wifi settings.
            userConfig->set(cfg_staticIP, false);
            userConfig->set(cfg_wifiPower, WIFI_POWER_MAX);
            userConfig->set(cfg_wifiPhyMode, 0);
            userConfig->set(cfg_timeZone, "");
        }
        return false;
    }
}

bool connect_wifi(const char *ssid, const char *password, const uint8_t *bssid)
{
    uint8_t count = 0;
    _millis_t start_time = _millis();

    WiFi.begin(ssid, password, 0, bssid);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(100); // Reduced delay for better responsiveness
        YIELD();

        // Check both attempt count and total time to prevent watchdog timeout
        if (count > MAX_ATTEMPTS_WIFI_CONNECTION || (_millis() - start_time) > 10000) // 10 second timeout
        {
            ESP_LOGI(TAG, "WiFi connection timeout after %lu ms, %d attempts", (uint32_t)(_millis() - start_time), count);
            WiFi.disconnect();
            return false;
        }

        // Only increment count every 500ms to maintain same retry logic
        if (count * 100 % 500 == 0)
        {
            count++;
        }
    }
    return true;
}
