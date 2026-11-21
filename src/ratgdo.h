/****************************************************************************
 * RATGDO HomeKit
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-26 David A Kerr... https://github.com/dkerr64/
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 * Contributions acknowledged from
 * Brandon Matthews... https://github.com/thenewwazoo
 * Jonathan Stroud...  https://github.com/jgstroud
 *
 */
#pragma once

#if defined(RATGDO_ENCODER) && defined(USE_GDOLIB) && (defined(RATGDO_ENCODER) == defined(USE_GDOLIB))
#error "Only one of RATGDO_ENCODER or USE_GDOLIB may be defined."
#endif

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

// Default device information for ESP8266 and ESP32 boards.
#ifdef ESP8266
#define DEVICE_NAME "homekit-ratgdo"
#define MANUF_NAME "ratCloud llc"
#define SERIAL_NUMBER "0P3ND00R"
#define MODEL_NAME "ratgdo_v2.5"
#define CHIP_FAMILY "ESP8266"
#else // not ESP8266
#ifndef DEVICE_NAME
#define DEVICE_NAME "homekit-ratgdo"
#endif
#ifndef MANUF_NAME
#define MANUF_NAME "ratCloud llc"
#endif
#ifndef SERIAL_NUMBER
#define SERIAL_NUMBER "0P3ND00R"
#endif
#ifndef MODEL_NAME
#define MODEL_NAME "ratgdo_32"
#endif
#ifndef CHIP_FAMILY
#define CHIP_FAMILY "ESP32"
#endif
#endif // ESP8266

// GPIO definitions for ESP8266 and ESP32 boards.
// For ESP8266 these are hardcoded
// For ESP32 they can be configured with defaults below
#ifdef ESP8266
typedef int8_t gpio_num_t;

#define UART_TX_GPIO D1 // red control terminal / GarageDoorOpener (UART1 TX)
#define UART_RX_GPIO D2 // red control terminal / GarageDoorOpener (UART1 RX)

#define INPUT_OBST_GPIO D7        // black obstruction sensor terminal
#define STATUS_OBST_GPIO D8       // output for obstruction status, HIGH for obstructed, LOW for clear
#define STATUS_DOOR_GPIO D0       // output door status, HIGH for open, LOW for closed
#define DRY_CONTACT_OPEN_GPIO D5  // dry contact for open door limit switch
#define DRY_CONTACT_CLOSE_GPIO D6 // dry contact for close door limit switch
#define DRY_CONTACT_LIGHT_GPIO D3 // dry contact for light toggle switch
#define DISCRETE_OPEN_GPIO D0     // alternative (or in addition) to toggle, can use discrete open control
#define DISCRETE_CLOSE_GPIO D8    // alternative (or in addition) to toggle, can use discrete close control
#define LED_GPIO LED_BUILTIN      // built-in LED for ESP8266 boards
#ifndef LED_ON_STATE
#define LED_ON_STATE LOW
#endif

#else // not ESP8266, i.e., ESP32

#ifndef UART_TX_GPIO
#define UART_TX_GPIO GPIO_NUM_17
#endif
#ifndef UART_RX_GPIO
#define UART_RX_GPIO GPIO_NUM_21
#endif

#ifndef INPUT_OBST_GPIO
#define INPUT_OBST_GPIO GPIO_NUM_4
#endif
#ifndef LED_GPIO
#define LED_GPIO GPIO_NUM_2
#endif
#ifndef LED_ON_STATE
#define LED_ON_STATE HIGH
#endif
#ifndef DRY_CONTACT_OPEN_GPIO
#define DRY_CONTACT_OPEN_GPIO GPIO_NUM_13
#endif
#ifndef DRY_CONTACT_CLOSE_GPIO
#define DRY_CONTACT_CLOSE_GPIO GPIO_NUM_14
#endif
#ifndef DRY_CONTACT_LIGHT_GPIO
#define DRY_CONTACT_LIGHT_GPIO GPIO_NUM_27
#endif
#ifndef STATUS_DOOR_GPIO
#define STATUS_DOOR_GPIO GPIO_NUM_26
#endif
#ifndef STATUS_OBST_GPIO
#define STATUS_OBST_GPIO GPIO_NUM_25
#endif
#ifndef DISCRETE_OPEN_GPIO
#define DISCRETE_OPEN_GPIO STATUS_DOOR_GPIO
#endif
#ifndef DISCRETE_CLOSE_GPIO
#define DISCRETE_CLOSE_GPIO STATUS_OBST_GPIO
#endif

#ifdef RATGDO32_DISCO
// RATGDO32 Disco board specific GPIO definitions
#ifndef BEEPER_GPIO
#define BEEPER_GPIO GPIO_NUM_33
#endif
#ifndef LASER_GPIO
#define LASER_GPIO GPIO_NUM_23
#endif
#ifndef SENSOR_SDA_GPIO
#define SENSOR_SDA_GPIO GPIO_NUM_19
#endif
#ifndef SENSOR_SCL_GPIO
#define SENSOR_SCL_GPIO GPIO_NUM_18
#endif
#ifndef SENSOR_SHUTDOWN_GPIO
#define SENSOR_SHUTDOWN_GPIO GPIO_NUM_32
#endif
#endif // RATGDO32_DISCO

#endif // ESP8266

const gpio_num_t UART_TX_PIN = (gpio_num_t)UART_TX_GPIO;
const gpio_num_t UART_RX_PIN = (gpio_num_t)UART_RX_GPIO;
const gpio_num_t INPUT_OBST_PIN = (gpio_num_t)INPUT_OBST_GPIO;
const gpio_num_t LED_PIN = (gpio_num_t)LED_GPIO;
const gpio_num_t DRY_CONTACT_OPEN_PIN = (gpio_num_t)DRY_CONTACT_OPEN_GPIO;   // open door
const gpio_num_t DRY_CONTACT_CLOSE_PIN = (gpio_num_t)DRY_CONTACT_CLOSE_GPIO; // close door
const gpio_num_t DRY_CONTACT_LIGHT_PIN = (gpio_num_t)DRY_CONTACT_LIGHT_GPIO; // toggle light
const gpio_num_t STATUS_DOOR_PIN = (gpio_num_t)STATUS_DOOR_GPIO;
const gpio_num_t STATUS_OBST_PIN = (gpio_num_t)STATUS_OBST_GPIO;
const gpio_num_t DISCRETE_OPEN_PIN = (gpio_num_t)DISCRETE_OPEN_GPIO;   // alternative (or in addition) to toggle, can use discrete open control
const gpio_num_t DISCRETE_CLOSE_PIN = (gpio_num_t)DISCRETE_CLOSE_GPIO; // alternative (or in addition) to toggle, can use discrete close control
#ifdef RATGDO32_DISCO
const gpio_num_t BEEPER_PIN = (gpio_num_t)BEEPER_GPIO;
const gpio_num_t LASER_PIN = (gpio_num_t)LASER_GPIO;
const gpio_num_t SENSOR_SDA_PIN = (gpio_num_t)SENSOR_SDA_GPIO;
const gpio_num_t SENSOR_SCL_PIN = (gpio_num_t)SENSOR_SCL_GPIO;
const gpio_num_t SENSOR_SHUTDOWN_PIN = (gpio_num_t)SENSOR_SHUTDOWN_GPIO;
#endif // RATGDO32_DISCO

#ifdef ESP8266
// ESP8266 specific door status definitions
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
// ESP32 specific door status definitions
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
#endif // ESP8266

// Helper macros to convert state enums to human-readable strings
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

// Global variables related to system state
extern bool suspend_service_loop;
extern bool wifi_got_ip;
extern "C" uint32_t free_heap;
extern "C" uint32_t min_heap;
#ifndef ESP8266
extern "C" uint32_t max_alloc_heap;
#endif
extern "C" uint32_t free_heap_at_boot;
extern "C" uint32_t free_iram_at_boot;

#define MOTION_TIMER_DURATION 5000  // how long to keep HomeKit motion sensor active for
#define LED_BLINK_INTERVAL 5 * 1000 // time between each "alive and working" LED blink

// Garage door stautus struct
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
#ifdef RATGDO_ENCODER
    bool manuallyOperated;
    GarageDoorCurrentState protocol_door_state;
    GarageDoorCurrentState encoder_door_state;
    uint32_t encoder_door_position; // 0 = closed, 100 = fully open, anything else implies unknown.
#endif
};
extern GarageDoor garage_door;
extern GarageDoor last_reported_garage_door;

// JSON response caching
#ifdef ESP8266
#define STATUS_JSON_BUFFER_SIZE (256 * 8)
#else
#define STATUS_JSON_BUFFER_SIZE (256 * 12)
#endif
extern char *status_json;
