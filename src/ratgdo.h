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

// ESP system includes
#ifndef ESP8266
#include <driver/gpio.h>
#endif

// Arduino includes
#include <Arduino.h>

// RATGDO project includes
#ifdef ESP8266
#include "homekit_decl.h"
#else
#include "HomeSpan.h"
#endif
#include "utilities.h"
#include "../lib/ratgdo/log.h"

#define DEVICE_NAME "homekit-ratgdo"
#define MANUF_NAME "ratCloud llc"
#define SERIAL_NUMBER "0P3ND00R"
#ifdef ESP8266
#define MODEL_NAME "ratgdo_v2.5"
#define CHIP_FAMILY "ESP8266"
#else
#define MODEL_NAME "ratgdo_32"
#define CHIP_FAMILY "ESP32"
#endif

#ifdef ESP8266
#define UART_TX_PIN D1 // red control terminal / GarageDoorOpener (UART1 TX)
#define UART_RX_PIN D2 // red control terminal / GarageDoorOpener (UART1 RX)

#define INPUT_OBST_PIN D7        // black obstruction sensor terminal
#define STATUS_OBST_PIN D8       // output for obstruction status, HIGH for obstructed, LOW for clear
#define STATUS_DOOR_PIN D0       // output door status, HIGH for open, LOW for closed
#define DRY_CONTACT_OPEN_PIN D5  // dry contact for open door limit switch
#define DRY_CONTACT_CLOSE_PIN D6 // dry contact for close door limit switch

enum GarageDoorCurrentState : uint8_t
{
    CURR_OPEN = 0,
    CURR_CLOSED = 1,
    CURR_OPENING = 2,
    CURR_CLOSING = 3,
    CURR_STOPPED = 4,
    UNKNOWN = 0xFF,
};

enum GarageDoorTargetState : uint8_t
{
    TGT_OPEN = 0,
    TGT_CLOSED = 1,
};

enum LockCurrentState : uint8_t
{
    CURR_UNLOCKED = 0,
    CURR_LOCKED = 1,
    CURR_JAMMED = 2,
    CURR_UNKNOWN = 3,
};

enum LockTargetState : uint8_t
{
    TGT_UNLOCKED = 0,
    TGT_LOCKED = 1,
};
#else
#ifdef GRGDO1_V2
const gpio_num_t UART_TX_PIN = GPIO_NUM_22;
#else
const gpio_num_t UART_TX_PIN = GPIO_NUM_17;
#endif
const gpio_num_t UART_RX_PIN = GPIO_NUM_21;
const gpio_num_t LED_BUILTIN = GPIO_NUM_2;
const gpio_num_t INPUT_OBST_PIN = GPIO_NUM_4;
const gpio_num_t DRY_CONTACT_OPEN_PIN = GPIO_NUM_13;  // open door contact sensor
const gpio_num_t DRY_CONTACT_CLOSE_PIN = GPIO_NUM_14; // closed door contact sensor
const gpio_num_t LIGHT_PIN = GPIO_NUM_27;             // control a light
const gpio_num_t DISCRETE_OPEN_PIN = GPIO_NUM_26;     // alternative (or in addition) to toggle, can use discrete open control
const gpio_num_t DISCRETE_CLOSE_PIN = GPIO_NUM_25;    // alternative (or in addition) to toggle, can use discrete close control

const gpio_num_t STATUS_DOOR_PIN = GPIO_NUM_26;
const gpio_num_t STATUS_OBST_PIN = GPIO_NUM_25;

const gpio_num_t BEEPER_PIN = GPIO_NUM_33;
const gpio_num_t LASER_PIN = GPIO_NUM_23;
const gpio_num_t SENSOR_PIN = GPIO_NUM_34;

const gpio_num_t SHUTDOWN_PIN = GPIO_NUM_32;

enum GarageDoorCurrentState : uint8_t
{
    CURR_OPEN = Characteristic::CurrentDoorState::OPEN,
    CURR_CLOSED = Characteristic::CurrentDoorState::CLOSED,
    CURR_OPENING = Characteristic::CurrentDoorState::OPENING,
    CURR_CLOSING = Characteristic::CurrentDoorState::CLOSING,
    CURR_STOPPED = Characteristic::CurrentDoorState::STOPPED,
    UNKNOWN = 0xFF,
};

enum GarageDoorTargetState : uint8_t
{
    TGT_OPEN = Characteristic::TargetDoorState::OPEN,
    TGT_CLOSED = Characteristic::TargetDoorState::CLOSED,
};

enum LockCurrentState : uint8_t
{
    CURR_UNLOCKED = Characteristic::LockCurrentState::UNLOCKED,
    CURR_LOCKED = Characteristic::LockCurrentState::LOCKED,
    CURR_JAMMED = Characteristic::LockCurrentState::JAMMED,
    CURR_UNKNOWN = Characteristic::LockCurrentState::UNKNOWN,
};

enum LockTargetState : uint8_t
{
    TGT_UNLOCKED = Characteristic::LockTargetState::UNLOCK,
    TGT_LOCKED = Characteristic::LockTargetState::LOCK,
};
#endif

extern "C" uint32_t free_heap;
extern "C" uint32_t min_heap;

#define MOTION_TIMER_DURATION 5000 // how long to keep HomeKit motion sensor active for

struct __attribute__((aligned(4))) GarageDoor
{
    bool wallPanelEmulated;
    bool active;
    GarageDoorCurrentState current_state;
    GarageDoorTargetState target_state;
    bool obstructed;
    bool has_motion_sensor;
#ifndef ESP8266
    // Feature not available on ESP8266
    bool has_distance_sensor;
#endif
    _millis_t motion_timer;
    bool motion;
    bool light;
    LockCurrentState current_lock;
    LockTargetState target_lock;
    uint32_t openingsCount;
    uint32_t batteryState;
    uint32_t openDuration;
    uint32_t closeDuration;
#ifndef ESP8266
    // Feature not available on ESP8266
    // TODO implement this
    _millis_t room_occupancy_timeout;
    bool room_occupied;
#endif
};
extern GarageDoor garage_door;

struct __attribute__((aligned(4))) ForceRecover
{
    uint32_t push_count;
    _millis_t timeout;
};
