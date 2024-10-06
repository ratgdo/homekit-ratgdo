// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef _WEB_H
#define _WEB_H

void setup_web();
void web_loop();

enum BroadcastType : uint8_t
{
    RATGDO_STATUS = 1,
    LOG_MESSAGE = 2,
};
void SSEBroadcastState(const char *data, BroadcastType type = RATGDO_STATUS);

extern "C" int crashCount; // pull in number of times crashed.

#endif // _WEB_H
