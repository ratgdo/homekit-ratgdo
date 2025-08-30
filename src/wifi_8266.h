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
#pragma once

#ifdef ESP8266
// This whole file only applies for ESP8266.
// On ESP32, WiFi is handled by the HomeSpan library
#include <ESP8266WiFi.h>

void wifi_loop();
void wifi_connect();
extern station_config wifiConf;
extern bool wifi_got_ip;
#endif
