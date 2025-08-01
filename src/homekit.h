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

// RATGDO project includes
#include "ratgdo.h"

void setup_homekit();

extern void notify_homekit_target_door_state_change(GarageDoorTargetState state);
extern void notify_homekit_current_door_state_change(GarageDoorCurrentState state);
extern void notify_homekit_target_lock(LockTargetState state);
extern void notify_homekit_current_lock(LockCurrentState state);
extern void notify_homekit_obstruction(bool state);
extern void notify_homekit_light(bool state);
extern void enable_service_homekit_motion(bool reboot);
extern void notify_homekit_motion(bool state);

extern char qrPayload[];

#ifdef ESP8266
// On ESP8266 we have our own HomeKit module
void homekit_loop();
extern void notify_homekit_active();
extern bool homekit_setup_done;

#else
// One ESP32 we use HomeSpan module.
// Accessory IDs
#define HOMEKIT_AID_BRIDGE 1
#define HOMEKIT_AID_GARAGE_DOOR 2
#define HOMEKIT_AID_LIGHT_BULB 3
#define HOMEKIT_AID_MOTION 4
#define HOMEKIT_AID_ARRIVING 5
#define HOMEKIT_AID_DEPARTING 6
#define HOMEKIT_AID_VEHICLE 7
#define HOMEKIT_AID_LASER 8
#define HOMEKIT_AID_ROOM_OCCUPANCY 9

enum Light_t : uint8_t
{
    GDO_LIGHT = 1,
    ASSIST_LASER = 2,
};

extern void notify_homekit_vehicle_occupancy(bool vehicleDetected);
extern void notify_homekit_vehicle_arriving(bool vehicleArriving);
extern void notify_homekit_vehicle_departing(bool vehicleDeparting);
extern void notify_homekit_laser(bool on);
extern void enable_service_homekit_vehicle(bool enable);
extern bool enable_service_homekit_laser(bool enable);
extern bool enable_service_homekit_room_occupancy(bool enable);
extern void notify_homekit_room_occupancy(bool occupied);

extern void homekit_unpair();
extern bool homekit_is_paired();

extern char ipv6_addresses[];

struct GDOEvent
{
    SpanCharacteristic *c;
    union
    {
        bool b;
        uint8_t u;
    } value;
};

struct DEV_GarageDoor : Service::GarageDoorOpener
{
    Characteristic::CurrentDoorState *current;
    Characteristic::TargetDoorState *target;
    Characteristic::ObstructionDetected *obstruction;
    Characteristic::LockCurrentState *lockCurrent;
    Characteristic::LockTargetState *lockTarget;

    QueueHandle_t event_q;

    DEV_GarageDoor();
    boolean update();
    void loop();
};

struct DEV_Info : Service::AccessoryInformation
{
    DEV_Info(const char *name);
    boolean update();
};

struct DEV_Light : Service::LightBulb
{
    Characteristic::On *on;

    QueueHandle_t event_q;
    Light_t type;

    DEV_Light(Light_t type = Light_t::GDO_LIGHT);
    boolean update();
    void loop();
};

struct DEV_Motion : Service::MotionSensor
{
    Characteristic::MotionDetected *motion;

    QueueHandle_t event_q;
    char name[16];

    DEV_Motion(const char *name);
    void loop();
};

struct DEV_Occupancy : Service::OccupancySensor
{
    Characteristic::OccupancyDetected *occupied;

    QueueHandle_t event_q;

    DEV_Occupancy();
    void loop();
};
#endif
