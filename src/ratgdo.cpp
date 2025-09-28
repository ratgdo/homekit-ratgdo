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

// C/C++ language includes

// ESP system includes
#ifdef ESP8266
#include <time.h>
#include <coredecls.h>
#include <user_interface.h>
#include <LittleFS.h>
#else
#include <esp_core_dump.h>
#include <esp_log.h>
#include <ping/ping_sock.h>
#endif

// RATGDO project includes
#include "ratgdo.h"
#include "config.h"
#include "comms.h"
#include "homekit.h"
#include "web.h"
#include "led.h"
#include "provision.h"
#include "softAP.h"
#ifdef ESP8266
#include "wifi_8266.h"
#endif
#ifdef RATGDO32_DISCO
#include "vehicle.h"
#endif
#ifndef USE_GDOLIB
#include "drycontact.h"
#endif

// Logger tag
static const char *TAG = "ratgdo-main";

// Initialize GDO status
GarageDoor garage_door = {
    .pinModeObstructionSensor = false,
    .wallPanelEmulated = false,
    .active = false,
    .current_state = (GarageDoorCurrentState)0xFF,
    .target_state = (GarageDoorTargetState)0xFF,
    .obstructed = false,
    .has_motion_sensor = false,
#ifdef RATGDO32_DISCO
    .has_distance_sensor = false,
#endif
#ifndef USE_GDOLIB
    .motion_timer = 0,
#endif
    .motion = false,
    .light = false,
    .current_lock = (LockCurrentState)0xFF,
    .target_lock = (LockTargetState)0xFF,
    .openingsCount = 0,
    .batteryState = 0,
    .openDuration = 0,
    .closeDuration = 0,
};

// Some initialization is postponed until after we have an IP address
bool wifi_got_ip = false;

// Track our memory usage
uint32_t free_heap = (1024 * 1024);
uint32_t min_heap = (1024 * 1024);
#ifdef MMU_IRAM_HEAP
uint32_t free_iram = (1024 * 1024);
uint32_t min_iram = (1024 * 1024);
#endif // MMU_IRAM_HEAP
_millis_t next_heap_check = 0;
#define MIN_FREE_HEAP (1024 * 4)
#define FREE_HEAP_CHECK_MS 1000

// Forward declare functions
bool suspend_service_loop = false;
void service_timer_loop();

// support for changeing WiFi settings
#define WIFI_CONNECT_TIMEOUT (30 * 1000)
static _millis_t wifiConnectTimeout = 0;

#ifndef ESP8266
// on ESP8266 ping is implemented in wifi.cpp
static bool ping_failure = false;
static bool ping_timed_out = false;
static esp_ping_handle_t ping;
static void ping_start();
static void ping_stop();
#endif

/****************************************************************************
 * Initialize RATGDO
 */
void setup()
{
#ifdef ESP8266
    disable_extra4k_at_link_time();
#else
    esp_core_dump_init();
#ifdef RATGDO32_DISCO
    tone(BEEPER_PIN, 1300, 500);
#endif
#endif // ESP32
    // led on during setup
    led.on();
    ESP_LOGI(TAG, "=== Starting RATGDO Homekit version %s", AUTO_VERSION);
#ifdef ESP8266
    ESP_LOGI(TAG, "%s", ESP.getFullVersion().c_str());
    ESP_LOGI(TAG, "Flash chip size 0x%X", ESP.getFlashChipSize());
    ESP_LOGI(TAG, "Flash chip mode 0x%X", ESP.getFlashChipMode());
    ESP_LOGI(TAG, "Flash chip speed 0x%X (%d MHz)", ESP.getFlashChipSpeed(), ESP.getFlashChipSpeed() / 1000000);
    ESP_LOGI(TAG, "Free heap: %d", ESP.getFreeHeap());
    // Load users saved configuration (or set defaults)
    load_all_config_settings();
    // Now set log level to whatever user has requested
    logLevel = (esp_log_level_t)userConfig->getLogLevel();
    // Initialize crash count... which can persist over reboots
    crashCount = saveCrash.count();
    if (crashCount == 255)
    {
        saveCrash.clear();
        crashCount = 0;
    }
#else
    esp_reset_reason_t r = esp_reset_reason();
    switch (r)
    {
    case ESP_RST_POWERON:
    case ESP_RST_PWR_GLITCH:
        ESP_LOGI(TAG, "System restart after power-on or power glitch: %d", r);
        // RTC memory does not survive power interruption. Initialize values.
        rebootTime = 0;
        crashTime = 0;
        crashCount = 0;
        break;
    default:
        ESP_LOGI(TAG, "System restart reason: %d", r);
        break;
    }

    // If there is a core dump image but no saved crash log, then set count to -1.
    if ((crashCount == 0) && (esp_core_dump_image_check() == ESP_OK))
        crashCount = -1;

    // Set log to info level so logging within load_all_config_settings() will display
    esp_log_level_set("*", ESP_LOG_INFO);
    // We will intercept calls to standard ESP_LOGx so we can route them through our logger
    esp_log_set_vprintf((vprintf_like_t)esp_log_hook);
    // Load users saved configuration (or set defaults)
    load_all_config_settings();
    // Now set log level to whatever user has requested
    esp_log_level_set("*", (esp_log_level_t)userConfig->getLogLevel());
#endif

    if (softAPmode)
    {
        start_soft_ap();
        setup_improv();
        return;
    }

    // We only reach here if not in softAPmode
    if (userConfig->getWifiChanged())
    {
        wifiConnectTimeout = _millis() + WIFI_CONNECT_TIMEOUT;
    }

    // Start by initializing Improv for WiFi provisioning by serial port
#ifdef ESP8266
    setup_improv();
#else
    if (userConfig->getEnableHomeSpanCLI())
        disable_improv();
    else
        setup_improv();
#endif

    // Then initialize communication with the garage door and sensors
    setup_comms();
#ifdef RATGDO32_DISCO
    setup_vehicle();
#endif
#ifndef USE_GDOLIB
    setup_drycontact();
#endif

    // Finally initialize WiFi and HomeKit
#ifdef ESP8266
    // on ESP8266 we setup everything ourselves.
    wifi_connect();
#else
    // on ESP32 the HomeSpan library handles WiFi and has callbacks which we use to setup everything else.
    setup_homekit();
#endif
    ESP_LOGI(TAG, "=== WAITING for IP address before continuing initialization");
    // led to idle mode
    led.idle();
}

/****************************************************************************
 * Main loop
 */
void loop()
{
    static bool setup_after_IP_done = false;

    // Some initialization is postponed until after we have an IP address
    if (!setup_after_IP_done && wifi_got_ip && !softAPmode)
    {
        if (strlen(userConfig->getTimeZone()) == 0)
        {
            // no timeZone set, try and find it automatically
            get_auto_timezone();
            // if successful this will have set the region and city, but not
            // the POSIX time zone code. That will be done by browser.
        }
#ifdef ESP8266
        // On ESP8266 we handle WiFi and HomeKit ourselves.  On ESP32 it is done by HomeSpan
        setup_homekit();
#endif
        // HTTP web server should be started after HomeKit because HomeKit initializes MDNS which we use in web setup
        setup_web();
        setup_after_IP_done = true;
        ESP_LOGI(TAG, "=== Initialization after IP address acquired complete");
#ifdef RATGDO32_DISCO
        // beep on completing startup.
        static bool startupBeeped = false;
        if (!startupBeeped)
        {
            tone(BEEPER_PIN, 2000, 500);
            startupBeeped = true;
        }
#endif
    }

    comms_loop();
#ifndef USE_GDOLIB
    drycontact_loop();
#endif
#ifdef ESP8266
    // On ESP8266 we handle WiFi and HomeKit ourselves
    wifi_loop();
    homekit_loop();
#endif
#ifdef RATGDO32_DISCO
    vehicle_loop();
#endif
    web_loop();
    improv_loop();
    soft_ap_loop();
    service_timer_loop();
}

/****************************************************************************
 * Service loop
 */
void service_timer_loop()
{
    _millis_t current_millis = _millis();
    static time_t lastSNTP = 0;
    static _millis_t lastLEDblink = 0;

    if (current_millis - lastLEDblink >= LED_BLINK_INTERVAL)
    {
        lastLEDblink = current_millis;
        // This gives a reassuring blink to show we're still alive and well.
        led.flash(250);
    }

    if (suspend_service_loop)
        return;

    if ((rebootSeconds != 0) && (rebootSeconds < (uint32_t)(current_millis / 1000)))
    {
        // Reboot the system if we have reached time...
        ESP_LOGI(TAG, "Rebooting system as %lu seconds expired", rebootSeconds);
        sync_and_restart();
        return;
    }

    if (enableNTP && clockSet)
    {
        if (clockSet != lastSNTP)
        {
            lastSNTP = clockSet;
            ESP_LOGI(TAG, "Current System time: %s", timeString());
        }

        if (lastRebootAt == 0)
        {
            time_t timeNow = time(NULL);
            lastRebootAt = timeNow - (current_millis / 1000);
            ESP_LOGI(TAG, "System boot time:    %s", timeString(lastRebootAt));
            // Need to also set when last door open/close was
            if (userConfig->getDoorUpdateAt() != 0)
            {
                lastDoorUpdateAt = (_millis_t)(((time_t)userConfig->getDoorUpdateAt() - timeNow) * 1000LL) + current_millis;
                ESP_LOGI(TAG, "Last door update at: %s", timeString((time_t)userConfig->getDoorUpdateAt()));
            }
            if (userConfig->getDoorOpenAt() != 0)
            {
                lastDoorOpenAt = (_millis_t)(((time_t)userConfig->getDoorOpenAt() - timeNow) * 1000LL) + current_millis;
                ESP_LOGI(TAG, "Last door open at:   %s", timeString((time_t)userConfig->getDoorOpenAt()));
            }
            if (userConfig->getDoorCloseAt() != 0)
            {
                lastDoorCloseAt = (_millis_t)(((time_t)userConfig->getDoorCloseAt() - timeNow) * 1000LL) + current_millis;
                ESP_LOGI(TAG, "Last door close at:  %s", timeString((time_t)userConfig->getDoorCloseAt()));
            }
        }
    }

    // Check heap
    static _millis_t last_heap_check = 0;
    if (current_millis - last_heap_check >= FREE_HEAP_CHECK_MS)
    {
        last_heap_check = current_millis;
        free_heap = ESP.getFreeHeap();
        if (free_heap < min_heap)
        {
            min_heap = free_heap;
            ESP_LOGI(TAG, "Free heap dropped to %d", min_heap);
            if (free_heap < MIN_FREE_HEAP)
            {
                ESP_LOGW(TAG, "Free heap dropped below %d, rebooting to maintain stability", MIN_FREE_HEAP);
                sync_and_restart();
            }
        }

#ifdef MMU_IRAM_HEAP
        // Also track IRAM heap usage
        // IRAM heap is only allocated during initialization, so this should stabilize after setup.
        {
            HeapSelectIram ephemeral;
            free_iram = ESP.getFreeHeap();
            if (free_iram < min_iram)
            {
                min_iram = free_iram;
                ESP_LOGI(TAG, "Free IRAM heap dropped to %d", min_iram);
            }
        }
#endif // MMU_IRAM_HEAP
    }

#ifndef ESP8266
    if ((wifiConnectTimeout > 0) && (current_millis > wifiConnectTimeout))
    {
        bool connected = (WiFi.status() == WL_CONNECTED);
        if (!connected)
        {
            ESP_LOGE(TAG, "30 seconds since WiFi settings change, failed to connect");
            userConfig->set(cfg_wifiPower, WIFI_POWER_MAX);
            userConfig->set(cfg_wifiPhyMode, 0);
            // TODO support WiFi TX Power & PhyMode... set changes immediately here
            // Now try and reconnect...
            wifiConnectTimeout = _millis() + WIFI_CONNECT_TIMEOUT;
            WiFi.reconnect();
        }
        else
        {
            ESP_LOGI(TAG, "30 seconds since WiFi settings change, successfully connected to access point");
            if (userConfig->getStaticIP())
            {
                ESP_LOGI(TAG, "Connected with static IP, test gateway IP reachable");
                ping_start();
            }
            wifiConnectTimeout = 0;
        }
        userConfig->set(cfg_wifiChanged, false);
    }

    if (ping_failure)
    {
        ping_failure = false; // reset, so we only come in here once
        if (userConfig->getStaticIP())
        {
            // We timed out trying to ping gateway set by static IP, revert to DHCP
            ping_stop();
            ESP_LOGI(TAG, "Unable to ping Gateway, reset to DHCP to acquire IP address and reconnect");
            userConfig->set(cfg_staticIP, false);
            IPAddress ip;
            ip.fromString("0.0.0.0");
            WiFi.config(ip, ip, ip, ip);
            // Now try and reconnect...
            wifiConnectTimeout = _millis() + WIFI_CONNECT_TIMEOUT;
            WiFi.reconnect();
        }
    }
#endif
}

#ifndef ESP8266
/****************************************************************************
 * Functions to ping gateway to test network okay
 */
static void ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint32_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    IPAddress ip_addr((uint32_t)target_addr.u_addr.ip4.addr);
    ESP_LOGI(TAG, "Ping: %d bytes from %s icmp_seq=%d ttl=%d time=%dms",
             recv_len, ip_addr.toString().c_str(), seqno, ttl, elapsed_time);
    ping_timed_out = false;
}

static void ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint32_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    IPAddress ip_addr((uint32_t)target_addr.u_addr.ip4.addr);
    ESP_LOGI(TAG, "Ping from %s icmp_seq=%d timeout", ip_addr.toString().c_str(), seqno);
    ping_timed_out = true;
}

static void ping_end(esp_ping_handle_t hdl, void *args)
{
    ping_failure = ping_timed_out;
    ESP_LOGI(TAG, "Ping end: %s", (ping_failure) ? "failed" : "success");
}

static void ping_start()
{
    ip_addr_t addr;
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    WiFi.gatewayIP().to_ip_addr_t(&addr);
    ESP_LOGI(TAG, "Ping to: %s", WiFi.gatewayIP().toString().c_str());
    ping_config.target_addr = addr;
    ping_config.count = 2;

    esp_ping_callbacks_t cbs;
    cbs.on_ping_success = ping_success;
    cbs.on_ping_timeout = ping_timeout;
    cbs.on_ping_end = ping_end;
    cbs.cb_args = NULL;
    esp_ping_new_session(&ping_config, &cbs, &ping);

    ping_failure = false;
    ping_timed_out = false;
    esp_ping_start(ping);
}

static void ping_stop()
{
    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
}
#endif
