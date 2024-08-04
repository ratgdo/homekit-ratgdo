// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef _WEB_H
#define _WEB_H

void setup_web();
void web_loop();

typedef struct
{
    uint8_t motion : 1;
    uint8_t obstruction : 1;
    uint8_t lightKey : 1;
    uint8_t dooorKey : 1;
    uint8_t lockKey : 1;
    uint8_t undef : 3;
} motionTriggerBitset;
typedef union
{
    motionTriggerBitset bit;
    uint8_t asInt;
} motionTriggersUnion;
extern "C" motionTriggersUnion motionTriggers;
extern "C" const char motionTriggersFile[];

enum BroadcastType : uint8_t {
    RATGDO_STATUS = 1,
    LOG_MESSAGE = 2,
};
void SSEBroadcastState(const char *data, BroadcastType type = RATGDO_STATUS);

extern "C" int crashCount; // pull in number of times crashed.

#endif // _WEB_H
