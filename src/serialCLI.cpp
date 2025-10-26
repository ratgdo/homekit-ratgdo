/****************************************************************************
 * RATGDO HomeKit
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-25 David A Kerr... https://github.com/dkerr64/
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 */

// C/C++ language includes
// none

// Arduino includes
// none

// RATGDO project includes
#include "ratgdo.h"
#include "config.h"
#include "softAP.h"
#include "web.h"
#include "comms.h"
#include "provision.h"

// Logger tag
static const char *TAG = "ratgdo-serialCLI";

// forward declarations
uint32_t scanWifi(bool uniqueSSID);
bool areYouSure(const char *txt);

// Serial Command Line Interface
void serialCLI(char cmd)
{

    switch (cmd)
    {
    case '?':
    {
        _millis_t upTime = _millis();
        Serial.printf_P(PSTR("\n----------> RATGDO <----------\n"));
        Serial.printf_P(PSTR("Hostname:              http://%s.local\n"), device_name_rfc952);
        Serial.printf_P(PSTR("IP Address:            %s\n"), userConfig->getLocalIP());
        Serial.printf_P(PSTR("Server uptime:         %llums (%s)\n"), (int64_t)upTime, toHHMMSSmmm(upTime));
        if (enableNTP && clockSet)
        {
            Serial.printf_P(PSTR("Current System time:   %s\n"), timeString());
            Serial.printf_P(PSTR("System boot time:      %s\n"), timeString(lastRebootAt));
        }
        Serial.printf_P(PSTR("Firmware version:      %s\n"), AUTO_VERSION);
        Serial.printf_P(PSTR("Free heap:             %d\n"), free_heap);
        Serial.printf_P(PSTR("Minimum heap:          %d\n"), min_heap);
        Serial.printf_P(PSTR("Log level:             %d\n"), userConfig->getLogLevel());
        Serial.printf_P(PSTR("Log to Serial console: %s\n\n"), suppressSerialLog ? "Disabled" : "Enabled");
        if (softAPmode)
        {
            Serial.printf_P(PSTR("*** Running in Access Point Mode @ 192.168.4.1 ***\n\n"));
        }
        Serial.printf_P(PSTR("Commands:\n"));
        Serial.printf_P(PSTR(" A - reboot into Access Point mode (192.168.4.1)\n"));
        Serial.printf_P(PSTR(" R - restart RATGDO\n"));
        Serial.printf_P(PSTR(" r - reset door values (ID & rolling code, open/close history)\n"));
        Serial.printf_P(PSTR(" F - factory reset RATGDO and reboot\n"));
        Serial.printf_P(PSTR(" W - configure WiFi Credentials\n"));
        Serial.printf_P(PSTR(" Z - scan for available WiFi networks\n"));
#ifdef USE_HOMESPAN
        if (!softAPmode)
        {
            Serial.printf_P(PSTR(" C - switch to HomeSpan CLI (and disable Improv WiFi provisioning)\n"));
        }
#endif
        Serial.println();
        Serial.printf_P(PSTR(" l - print RATGDO buffered message log\n"));
        Serial.printf_P(PSTR(" L - print RATGDO saved reboot log\n"));
        Serial.printf_P(PSTR(" P - print RATGDO crash log\n"));
        Serial.printf_P(PSTR(" S - print RATGDO status JSON\n"));
        Serial.printf_P(PSTR(" s - %s log to serial port\n"), suppressSerialLog ? "enable" : "disable");
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
        Serial.printf_P(PSTR(" t - print FreeRTOS task info\n"));
#endif
        Serial.printf_P(PSTR(" T - time-to-close test (flash without closing)\n"));
        Serial.printf_P(PSTR(" u - %s force recovery with multiple button press\n"), !force_recover.enable ? "enable" : "disable");
        Serial.println();
        Serial.printf_P(PSTR(" 0..5 - set log level 0(none), 1(error), 2(warn), 3(info), 4(debug), 5(verbose)\n\n"));
        break;
    }

#ifdef USE_HOMESPAN
    case 'C':
    {
        userConfig->set(cfg_homespanCLI, true);
        disable_improv();
        break;
    }
#endif

    case 'F':
    {
        if (areYouSure(PSTR("Factory reset reqested? Are you sure Y/N: ")))
        {
            factoryReset();
        }
        else
        {
            Serial.printf_P(PSTR("Factory reset request aborted\n"));
        }
        break;
    }

    case 'l':
    {
        // Print current log
        bool saved = suppressSerialLog;
        suppressSerialLog = true;
        ratgdoLogger->printMessageLog(Serial);
        suppressSerialLog = saved;
        break;
    }

    case 'L':
    {
        // Print log at last reboot
        bool saved = suppressSerialLog;
        suppressSerialLog = true;
#ifdef ESP8266
        File file = LittleFS.open(REBOOT_LOG_MSG_FILE, "r");
        ratgdoLogger->printSavedLog(file, Serial);
        file.close();
#else
        ratgdoLogger->printSavedLog(Serial);
#endif
        suppressSerialLog = saved;
        break;
    }

    case 'P':
    {
        // Print last crash log
        bool saved = suppressSerialLog;
        suppressSerialLog = true;
        ratgdoLogger->printCrashLog(Serial);
        suppressSerialLog = saved;
        break;
    }

    case 'r':
    {
        if (areYouSure(PSTR("Reset door ID, rolling code, motion and open/close history? Are you sure Y/N: ")))
        {
            reset_door();
        }
        else
        {
            Serial.printf_P(PSTR("Door reset request aborted\n"));
        }
        break;
    }

    case 's':
    {
        // switch logging to serial console (in case Improv suppressed it)
        suppressSerialLog = !suppressSerialLog;
        ESP_LOGI(TAG, "logging to serial port %s", suppressSerialLog ? "disabled" : "enabled");
        break;
    }

    case 'R':
    {
        // Reboot device
        sync_and_restart();
        break;
    }

    case 'A':
    {
        // Reboot into soft AP mode
        userConfig->set(cfg_softAPmode, true);
        ESP8266_SAVE_CONFIG();
        sync_and_restart();
        break;
    }

    case 'S':
    {
        static char *json = status_json;
        build_status_json(json);
        Serial.println(json);
        Serial.printf_P(PSTR("JSON length: %d, max: %d, used: %d%%\n"), strlen(json), STATUS_JSON_BUFFER_SIZE, strlen(json) * 100 / STATUS_JSON_BUFFER_SIZE);
        break;
    }

#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
    case 't':
    {
        printTaskInfo();
        break;
    }
#endif

    case 'T':
    {
        ESP_LOGI(TAG, "Test time-to-close delay with light flash for 5 seconds");
        delayFnCall(5 * 1000, []()
                    {
            ESP_LOGD(TAG, "Time-to-close test callback function");
            // This will send a light press / release / release without checking whether necessary or not.
            set_light(false,false);
            // This will force us to send current state to browser, so it reports correct state.
            last_reported_garage_door.current_state = (GarageDoorCurrentState)0xFF; });
        break;
    }

    case 'u':
    {
        force_recover.enable = !force_recover.enable;
        ESP_LOGI(TAG, "Forced recovery (enter Soft AP mode) by pressing wall panel buttons %s", !force_recover.enable ? "disabled" : "enabled");
        break;
    }

    case 'Z':
    {
        uint32_t count = scanWifi(false);
        if (count == 0)
        {
            Serial.printf_P(PSTR("No networks found!\n"));
            break;
        }

        Serial.printf_P(PSTR("Found %d networks...\n"), count);
        break;
    }

    case 'W':
    {
        uint32_t count = scanWifi(true);
        if (count == 0)
        {
            Serial.printf_P(PSTR("No networks found!\n"));
            break;
        }

        Serial.printf_P(PSTR("Found %d networks...\n"), count);

        // User selects network by number...
        Serial.setTimeout(5000);
        Serial.printf_P(PSTR("Select Network: "));
        uint32_t num = Serial.parseInt();
        while (Serial.available())
            Serial.read();
        // bool saved = suppressSerialLog;
        // suppressSerialLog = saved;
        if (num < 1 || num > count)
        {
            Serial.printf_P(PSTR("%d\nInvalid network selection\n"), num);
            break;
        }

        String currentSSID = "";
        String ssid = "";
        String password = "";
        bool encrypted = false;
        count = 0;
        for (wifiNet_t net : wifiNets)
        {

            if (currentSSID != net.ssid)
            {
                currentSSID = net.ssid;
                if (++count == num)
                {
                    ssid = net.ssid;
#ifdef ESP32
                    encrypted = net.encryptionType != wifi_auth_mode_t::WIFI_AUTH_OPEN;
#else
                    encrypted = net.encryptionType != wl_enc_type::ENC_TYPE_NONE;
#endif
                }
            }
        }
        if (encrypted)
        {
            Serial.printf_P(PSTR("\nPassword for network %s: "), ssid.c_str());
            password = Serial.readStringUntil('\n');
            while (Serial.available())
                Serial.read();
            password.trim(); // remove whitespace
            if (password.length() == 0)
            {
                Serial.printf_P(PSTR("\nNo password provided\n"));
                break;
            }
            // Serial.print(password);
        }

        Serial.printf_P(PSTR("\nConnect to WiFi network %s and restart? "), ssid.c_str());
        if (!areYouSure("Are you sure Y/N: "))
            break;

        // attempt to connect
        Serial.printf_P(PSTR("\nConnecting...\n"));
        if (set_new_ssid(ssid.c_str(), password.c_str()))
        {
            Serial.printf_P(PSTR("\nSuccess, you are now connected to WiFi network %s\n"), ssid.c_str());
        }
        else
        {
            Serial.printf_P(PSTR("\nFailed to connect to WiFi network %s, connected to previous network\n"), ssid.c_str());
        }

        break;
    }

    case 0:
    {
        // echo CR/LF to terminal... reassurance to user.
        Serial.printf_P(PSTR("\r\n"));
        break;
    }

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    {
        // Set log level... take effect immediately
        Serial.printf_P(PSTR("Set log level to %c\n"), cmd);
        userConfig->set(cfg_logLevel, (int)(cmd - '0'));
#ifdef ESP32
        esp_log_level_set("*", (esp_log_level_t)userConfig->getLogLevel());
#else
        logLevel = (esp_log_level_t)userConfig->getLogLevel();
#endif
        break;
    }
    } // End of switch
}

bool areYouSure(const char *txt)
{
    Serial.setTimeout(5000);
    while (Serial.available())
        Serial.read();
    Serial.printf_P(txt);
    String confirm = Serial.readStringUntil('\n');
    while (Serial.available())
        Serial.read();
    confirm.trim(); // remove whitespace
    Serial.println(confirm);
    return (confirm.equalsIgnoreCase("Y") || confirm.equalsIgnoreCase("YES"));
}

uint32_t scanWifi(bool uniqueSSID)
{
    String currentSSID = "";
    int32_t count = 0;
    bool saved = suppressSerialLog;
    suppressSerialLog = true;
    Serial.printf_P(PSTR("Scanning WiFi Networks...\n"));
    wifi_scan();
    if (wifiNets.size() == 0)
    {
        Serial.printf_P(PSTR("No networks found!\n"));
        return 0;
    }
    else
    {
        // Serial.printf_P(PSTR("Found %d networks...\n"), wifiNets.size());
        static const char d[] PROGMEM = "----------------------------------------";
        Serial.printf_P(PSTR("     %-32.32s  %17.17s  %4.4s  %4.4s  %12.12s\n"), "SSID", "BSSID", "RSSI", "CHAN", "ENCRYPTION");
        Serial.printf_P(PSTR("     %-32.32s  %17.17s  %4.4s  %4.4s  %12.12s\n"), d, d, d, d, d);

        for (wifiNet_t net : wifiNets)
        {
            // wifiNets may have multiple entries for a SSID sorted by RSSI,
            // we use the first (strongest signal) in the list.
            if (!uniqueSSID || currentSSID != net.ssid)
            {
                currentSSID = net.ssid;
                count++;
                Serial.printf_P(PSTR("%3d: %-32.32s  %02x:%02x:%02x:%02x:%02x:%02x  %4ld  %4ld  %12.12s\n"),
                                count, net.ssid.c_str(),
                                net.bssid[0], net.bssid[1], net.bssid[2], net.bssid[3], net.bssid[4], net.bssid[5],
                                net.rssi, net.channel, encryptionToString(net.encryptionType));
            }
        }
        Serial.println();
    }
    suppressSerialLog = saved;
    return count;
}
