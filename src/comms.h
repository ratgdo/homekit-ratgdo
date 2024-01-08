// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License
#pragma once

void comms_task_entry(void* ctx);

void open_door();
void close_door();

void set_lock(uint8_t value);
void set_light(bool value);
