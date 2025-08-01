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

// C/C++ language includes
// none

// ESP system includes
#ifdef ESP32
#define PROGMEM // so it is no-op in webcontent.h
#include <WebServer.h>
extern WebServer server;
#else
#include <ESP8266WebServer.h>
extern ESP8266WebServer server;
#endif

// RATGDO project includes
// none

extern void setup_web();
extern void web_loop();

extern void handle_notfound();
extern void handle_reboot();

extern void load_page(const char *page);

extern const char response400invalid[];
extern const char type_txt[];

extern _millis_t lastDoorUpdateAt;

enum BroadcastType : uint8_t
{
    RATGDO_STATUS = 1,
    LOG_MESSAGE = 2,
};
void SSEBroadcastState(const char *data, BroadcastType type = RATGDO_STATUS);
