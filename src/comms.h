// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef _COMMS_H
#define _COMMS_H
//Needed to define doorState
#include "Packet.h"

void setup_comms();
void comms_loop();

void open_door();
void close_door();

void set_lock(uint8_t value);
void set_light(bool value);

void save_rolling_code();
void reset_door();

//Adding external declaration so doorState can be used in dryContactLoop() ratgdo.cpp
//Remove if dryContactLoop() is moved
extern DoorState doorState;

#endif // _COMMS_H
