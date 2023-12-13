#ifndef _RATGDO_H
#define _RATGDO_H

#include "homekit_decl.h"

#define DEVICE_NAME "homekit-ratgdo"
#define MANUF_NAME "ratCloud llc"
#define SERIAL_NUMBER "0P3ND00R"
#define MODEL_NAME "ratgdo_v2.5"
#define CHIP_FAMILY "ESP8266"

/********************************** PIN DEFINITIONS *****************************************/

#define UART_TX_PIN             D1  // red control terminal / GarageDoorOpener (UART1 TX)
#define UART_RX_PIN             D2  // red control terminal / GarageDoorOpener (UART1 RX)

/*
 * TODO add support for pin-based obstruction detection
#define INPUT_OBST_PIN          D7  // black obstruction sensor terminal
 */

/*
 * TODO add support for dry contact switches
#define STATUS_DOOR_PIN         D0  // output door status, HIGH for open, LOW for closed
#define STATUS_OBSTRUCTION_PIN  D8  // output for obstruction status, HIGH for obstructed, LOW for clear
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

struct GarageDoor {
    bool active;
    GarageDoorCurrentState current_state;
    GarageDoorTargetState target_state;
    bool obstructed;
    unsigned long motion_timer;
    bool motion;
    bool light;
};


#endif // _RATGDO_H
