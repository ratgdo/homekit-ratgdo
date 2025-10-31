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
extern void shutdown_comms();
extern void comms_loop();

extern GarageDoorCurrentState open_door();
extern GarageDoorCurrentState close_door();
extern void delayFnCall(uint32_t ms, void (*callback)());
#ifndef USE_GDOLIB
extern void send_get_status();
extern void send_get_openings();
extern void send_cancel_ttc();
extern void send_set_ttc(uint16_t seconds);
#endif
extern bool set_lock(bool value, bool verify = true);
extern bool set_light(bool value, bool verify = true);
extern void toggle_light();
extern void sec1_light_press(uint32_t delay = 0);
extern void sec1_light_release(uint8_t howManyReleases = 2, uint32_t delay = 0);

extern void save_rolling_code();
extern void reset_door();

extern uint32_t is_ttc_active();

extern uint32_t doorControlType;
extern GarageDoorCurrentState doorState;
extern bool comms_setup_done;
extern bool comms_status_done;

struct __attribute__((aligned(4))) ForceRecover
{
    uint32_t push_count;
    _millis_t timeout;
    bool enable;
};
extern ForceRecover force_recover;

// For door open/close duration
constexpr uint32_t DOOR_MAX_HISTORY = 6;            // Number of door operations to average across
constexpr uint32_t DOOR_MAX_DURATION = (45 * 1000); // Maximum time it should take to open/close a door
constexpr uint32_t DOOR_MIN_DURATION = (3 * 1000);  // Minimum time it should take to open/close a door

struct DoorHistory
{
    uint32_t max = DOOR_MAX_HISTORY;
    uint32_t count = 0;
    uint32_t duration[DOOR_MAX_HISTORY] = {0};
};

extern struct DoorHistory openHistory;
extern struct DoorHistory closeHistory;

#define openHistory(n) (openHistory.duration[(openHistory.count + DOOR_MAX_HISTORY - (n)) % DOOR_MAX_HISTORY])
#define closeHistory(n) (closeHistory.duration[(closeHistory.count + DOOR_MAX_HISTORY - (n)) % DOOR_MAX_HISTORY])
