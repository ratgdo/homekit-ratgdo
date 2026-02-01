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

#ifdef ESP32
#define USE_HOMESPAN
#endif

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
#define DRY_CONTACT_LIGHT_PIN D3 // dry contact for light toggle switch

#ifndef LED_BUILTIN_ON_STATE
#define LED_BUILTIN_ON_STATE LOW
#endif

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

// RATGDO32 GPIO
#ifndef UART_TX_GPIO
#define UART_TX_GPIO GPIO_NUM_17
#endif
const gpio_num_t UART_TX_PIN = (gpio_num_t)UART_TX_GPIO;
#ifndef UART_RX_GPIO
#define UART_RX_GPIO GPIO_NUM_21
#endif
const gpio_num_t UART_RX_PIN = (gpio_num_t)UART_RX_GPIO;
#ifndef INPUT_OBST_GPIO
#define INPUT_OBST_GPIO GPIO_NUM_4
#endif
const gpio_num_t INPUT_OBST_PIN = (gpio_num_t)INPUT_OBST_GPIO;
#ifndef LED_BUILTIN_GPIO
#define LED_BUILTIN_GPIO GPIO_NUM_2
#endif
#ifndef LED_BUILTIN_ON_STATE
#define LED_BUILTIN_ON_STATE HIGH
#endif
const gpio_num_t LED_BUILTIN = (gpio_num_t)LED_BUILTIN_GPIO;
#ifndef DRY_CONTACT_OPEN_GPIO
#define DRY_CONTACT_OPEN_GPIO GPIO_NUM_13
#endif
const gpio_num_t DRY_CONTACT_OPEN_PIN = (gpio_num_t)DRY_CONTACT_OPEN_GPIO; // open door
#ifndef DRY_CONTACT_CLOSE_GPIO
#define DRY_CONTACT_CLOSE_GPIO GPIO_NUM_14
#endif
const gpio_num_t DRY_CONTACT_CLOSE_PIN = (gpio_num_t)DRY_CONTACT_CLOSE_GPIO; // close door
#ifndef DRY_CONTACT_LIGHT_GPIO
#define DRY_CONTACT_LIGHT_GPIO GPIO_NUM_27
#endif
const gpio_num_t DRY_CONTACT_LIGHT_PIN = (gpio_num_t)DRY_CONTACT_LIGHT_GPIO; // toggle light
#ifndef STATUS_DOOR_GPIO
#define STATUS_DOOR_GPIO GPIO_NUM_26
#endif
const gpio_num_t STATUS_DOOR_PIN = (gpio_num_t)STATUS_DOOR_GPIO;
#ifndef STATUS_OBST_GPIO
#define STATUS_OBST_GPIO GPIO_NUM_25
#endif
const gpio_num_t STATUS_OBST_PIN = (gpio_num_t)STATUS_OBST_GPIO;

// !!! this is conflicting with above status pins
#ifdef USE_GDOLIB
const gpio_num_t DISCRETE_OPEN_PIN = GPIO_NUM_26;  // alternative (or in addition) to toggle, can use discrete open control
const gpio_num_t DISCRETE_CLOSE_PIN = GPIO_NUM_25; // alternative (or in addition) to toggle, can use discrete close control
#endif

#ifdef RATGDO32_DISCO
const gpio_num_t BEEPER_PIN = GPIO_NUM_33;
const gpio_num_t LASER_PIN = GPIO_NUM_23;
const gpio_num_t SENSOR_SDA_PIN = GPIO_NUM_19;
const gpio_num_t SENSOR_SCL_PIN = GPIO_NUM_18;
const gpio_num_t SENSOR_SHUTDOWN_PIN = GPIO_NUM_32;
#endif

enum GarageDoorCurrentState : uint8_t
{
    CURR_OPEN = Characteristic::CurrentDoorState::OPEN,
    CURR_CLOSED = Characteristic::CurrentDoorState::CLOSED,
    CURR_OPENING = Characteristic::CurrentDoorState::OPENING,
    CURR_CLOSING = Characteristic::CurrentDoorState::CLOSING,
    CURR_STOPPED = Characteristic::CurrentDoorState::STOPPED,
    // UNKNOWN = 0xFF, // Not a valid HomeKit value, so should not have in our enum.
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

#define DOOR_STATE(s) (s == 0) ? "Open" : (s == 1) ? "Closed"  \
                                      : (s == 2)   ? "Opening" \
                                      : (s == 3)   ? "Closing" \
                                      : (s == 4)   ? "Stopped" \
                                                   : "Unknown"
#define LOCK_STATE(s) (s == 0) ? "Unsecured" : (s == 1) ? "Secured" \
                                           : (s == 2)   ? "Jammed"  \
                                                        : "Unknown"
// Caution, do not change Enabled / Disabled text without changing functions.js to match
#define REMOTES_STATE(s) (s == 0) ? "Enabled" : (s == 1) ? "Disabled" \
                                            : (s == 2)   ? "Jammed"   \
                                                         : "Unknown"

extern bool suspend_service_loop;
extern bool wifi_got_ip;
extern "C" uint32_t free_heap;
extern "C" uint32_t min_heap;
extern "C" uint32_t free_heap_at_boot;
extern "C" uint32_t free_iram_at_boot;

#define MOTION_TIMER_DURATION 5000  // how long to keep HomeKit motion sensor active for
#define LED_BLINK_INTERVAL 5 * 1000 // time between each "alive and working" LED blink

struct __attribute__((aligned(4))) GarageDoor
{
    bool pinModeObstructionSensor;
    bool wallPanelEmulated;
    bool active;
    GarageDoorCurrentState current_state;
    GarageDoorTargetState target_state;
    bool obstructed;
    bool has_motion_sensor;
#ifdef RATGDO32_DISCO
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
    uint32_t ttcActive;
    uint32_t builtInTTC;
    uint32_t builtInTTCremaining;
    bool builtInTTChold;
#ifndef ESP8266
    // Feature not available on ESP8266
    _millis_t room_occupancy_timeout;
    bool room_occupied;
#endif
};
extern GarageDoor garage_door;
extern GarageDoor last_reported_garage_door;

// JSON response caching
#ifdef ESP8266
#define STATUS_JSON_BUFFER_SIZE (256 * 8)
#else
#define STATUS_JSON_BUFFER_SIZE (256 * 10)
#endif
extern char *status_json;
