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

// Logger tag
static const char *TAG = "ratgdo-serialCLI";

void serialCLI(char cmd)
{

    switch (cmd)
    {
    case '?':
    {
        _millis_t upTime = _millis();
        Serial.printf_P(PSTR("\nServer uptime:         %llums (%s)\n"), (int64_t)upTime, toHHMMSSmmm(upTime));
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
        Serial.printf_P(PSTR("Valid commands:\n"));
        Serial.printf_P(PSTR(" F - factory reset RATGDO and reboot\n"));
        Serial.printf_P(PSTR(" l - print RATGDO buffered message log\n"));
        Serial.printf_P(PSTR(" L - print RATGDO saved reboot log\n"));
        Serial.printf_P(PSTR(" r - %s log to serial port\n"), suppressSerialLog ? "enable" : "disable");
        Serial.printf_P(PSTR(" R - reboot RATGDO\n"));
        Serial.printf_P(PSTR(" S - reboot into Soft AP mode (access at 192.168.4.1)\n"));
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
        Serial.printf_P(PSTR(" t - print FreeRTOS task info\n"));
#endif
        Serial.printf_P(PSTR(" T - time-to-close test (flash without closing)\n"));
        Serial.printf_P(PSTR(" u - %s force recovery with multiple button press\n"), !force_recover.enable ? "enable" : "disable");
        // Serial.printf_P(PSTR(" w - scan and select WiFi network\n"));
        Serial.printf_P(PSTR(" 0..5 - set ESP log level 0(none), 1(error), 2(warn), 3(info), 4(debug), 5(verbose)\n\n"));
        break;
    }

    case 'F':
    {
        Serial.setTimeout(5000);
        while (Serial.available())
            Serial.read();
        Serial.printf_P(PSTR("Factory reset reqested, are you sure Y/N: "));
        String confirm = Serial.readStringUntil('\n');
        while (Serial.available())
            Serial.read();
        Serial.println(confirm);
        if (confirm == "Y" || confirm == "y")
        {
            factoryReset();
        }
        else
        {
            Serial.println("Factory reset request aborted");
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

    case 'r':
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

    case 'S':
    {
        userConfig->set(cfg_softAPmode, true);
        ESP8266_SAVE_CONFIG();
        sync_and_restart();
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
        delayFnCall(5 * 1000, []() {
            ESP_LOGD(TAG, "TTC test call back");
            // This will send a light press / release / release without checking whether necessary or not.
            if (doorControlType == 1)
            {
                sec1_light_press(250);
                sec1_light_release(2,250);
            }
        });
        break;
    }

    case 'u':
    {
        force_recover.enable = !force_recover.enable;
        ESP_LOGI(TAG, "Forced recovery (enter Soft AP mode) by pressing wall panel buttons %s", !force_recover.enable ? "disabled" : "enabled");
        break;
    }

    case 'w':
    {
        String currentSSID = "";
        // wifiNet_t net;
        int32_t count = 0;
        bool saved = suppressSerialLog;
        suppressSerialLog = true;
        Serial.printf_P(PSTR("Scanning for networks...\n"));
        wifi_scan();
        Serial.printf_P(PSTR("Found %d networks, Strongest access points:\n"), wifiNets.size());
        for (wifiNet_t net : wifiNets)
        {
            // wifiNets may have multiple entries for a SSID sorted by RSSI,
            // we use the first (strongest signal) in the list.
            if (currentSSID != net.ssid)
            {
                currentSSID = net.ssid;
                count++;
                Serial.printf_P(PSTR(" %2d: %-12s (Ch:%2d, %ddBm) AP: %02x:%02x:%02x:%02x:%02x:%02x\n"),
                                count, net.ssid.c_str(), net.channel, net.rssi,
                                net.bssid[0], net.bssid[1], net.bssid[2], net.bssid[3], net.bssid[4], net.bssid[5]);
            }
        }
        Serial.printf_P(PSTR("Found %d unique SSID\n"), count);
        suppressSerialLog = saved;
        /* Work in progress
        Serial.setTimeout(5000);
        Serial.printf_P(PSTR("Select Network: "));
        int32_t num = Serial.parseInt();
        while (Serial.available())
            Serial.read();
        suppressSerialLog = saved;
        if (num < 1 || num > count)
        {
            Serial.printf_P(PSTR("%d\nInvalid network selection\n"), num);
            break;
        }
        currentSSID = "";
        count = 0;
        String ssid = "";
        for (wifiNet_t net : wifiNets)
        {
            // wifiNets may have multiple entries for a SSID sorted by RSSI,
            // we use the first (strongest signal) in the list.
            if (currentSSID != net.ssid)
            {
                currentSSID = net.ssid;
                if (++count == num)
                {
                    ssid = net.ssid;
                }
            }
        }
        if (ssid == "")
        {
            Serial.printf_P(PSTR("\nInvalid network selection: %d\n"), num);
            break;
        }
        Serial.printf_P(PSTR("%d: %s\n"), num, ssid.c_str());
        Serial.printf_P(PSTR("Password: "));
        String password = Serial.readStringUntil('\n');
        while (Serial.available())
            Serial.read();
        if (password == "")
        {
            Serial.printf_P(PSTR("\nNo password provided\n"));
            break;
        }
        Serial.print(password);
        Serial.println();
        */
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