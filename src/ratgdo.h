// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License
#pragma once

#include <driver/gpio.h>

#include "homekit_decl.h"

#define DEVICE_NAME "homekit-ratgdo"
#define MANUF_NAME "ratCloud llc"
#define SERIAL_NUMBER "0P3ND00R"
#define MODEL_NAME "ratgdo_v2.5"
#define CHIP_FAMILY "ESP8266"

/********************************** PIN DEFINITIONS *****************************************/

const gpio_num_t UART_TX_PIN = GPIO_NUM_5;
const gpio_num_t UART_RX_PIN = GPIO_NUM_4;
const gpio_num_t LED_BUILTIN = GPIO_NUM_2;

// TODO obstruction refactor
// const gpio_num_t INPUT_OBST_PIN = GPIO_NUM_13; // black obstruction sensor terminal

/*
 * TODO add support for dry contact switches
#define STATUS_DOOR_PIN         D0  // output door status, HIGH for open, LOW for closed
*/
// TODO obstruction refactor
// const gpio_num_t STATUS_DOOR_PIN = GPIO_NUM_16;  // output for obstruction status, HIGH for obstructed, LOW for clear
/*
#define DRY_CONTACT_OPEN_PIN    D5  // dry contact for opening door
#define DRY_CONTACT_CLOSE_PIN   D6  // dry contact for closing door
#define DRY_CONTACT_LIGHT_PIN   D3  // dry contact for triggering light (no discrete light commands, so toggle only)
 */

/********************************** MODEL *****************************************/

enum GarageDoorCurrentState : uint8_t {
    CURR_OPEN = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_OPEN,
    CURR_CLOSED = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_CLOSED,
    CURR_OPENING = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_OPENING,
    CURR_CLOSING = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_CLOSING,
    CURR_STOPPED = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_STOPPED,
};

enum GarageDoorTargetState : uint8_t {
    TGT_OPEN = HOMEKIT_CHARACTERISTIC_TARGET_DOOR_STATE_OPEN,
    TGT_CLOSED = HOMEKIT_CHARACTERISTIC_TARGET_DOOR_STATE_CLOSED,
};

enum LockCurrentState : uint8_t {
    CURR_UNLOCKED = HOMEKIT_CHARACTERISTIC_CURRENT_LOCK_STATE_UNSECURED,
    CURR_LOCKED = HOMEKIT_CHARACTERISTIC_CURRENT_LOCK_STATE_SECURED,
    CURR_JAMMED = HOMEKIT_CHARACTERISTIC_CURRENT_LOCK_STATE_JAMMED,
    CURR_UNKNOWN = HOMEKIT_CHARACTERISTIC_CURRENT_LOCK_STATE_UNKNOWN,
};

enum LockTargetState : uint8_t {
    TGT_UNLOCKED = HOMEKIT_CHARACTERISTIC_TARGET_LOCK_STATE_UNSECURED,
    TGT_LOCKED = HOMEKIT_CHARACTERISTIC_TARGET_LOCK_STATE_SECURED,
};

struct GarageDoor {
    bool active;
    GarageDoorCurrentState current_state;
    GarageDoorTargetState target_state;
    bool obstructed;
    bool motion;
    bool light;
    LockCurrentState current_lock;
    LockTargetState target_lock;
};
