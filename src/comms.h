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
#include <stdint.h>

extern void setup_comms();
extern void comms_loop();

extern GarageDoorCurrentState open_door();
extern GarageDoorCurrentState close_door();

extern bool set_lock(bool value, bool verify = true);
extern bool set_light(bool value, bool verify = true);

extern void save_rolling_code();
extern void reset_door();

extern uint32_t doorControlType;
extern GarageDoorCurrentState doorState;
extern bool comms_status_done;
