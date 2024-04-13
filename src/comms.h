// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef _COMMS_H
#define _COMMS_H

void setup_comms();
void comms_loop();

void open_door();
void close_door();

void set_lock(uint8_t value);
void set_light(bool value);

void save_rolling_code();
#endif // _COMMS_H
