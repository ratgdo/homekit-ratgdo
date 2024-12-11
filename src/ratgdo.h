#ifndef _RATGDO_H
#define _RATGDO_H

#include <Arduino.h>
#include <OneButton.h>
#include "homekit_decl.h"

#define DEVICE_NAME "homekit-ratgdo"
#define MANUF_NAME "ratCloud llc"
#define SERIAL_NUMBER "0P3ND00R"
#define MODEL_NAME "ratgdo_v2.5"
#define CHIP_FAMILY "ESP8266"

/********************************** PIN DEFINITIONS *****************************************/

#define UART_TX_PIN D1 // red control terminal / GarageDoorOpener (UART1 TX)
#define UART_RX_PIN D2 // red control terminal / GarageDoorOpener (UART1 RX)

#define INPUT_OBST_PIN D7 // black obstruction sensor terminal
#define STATUS_OBST_PIN D8 // output for obstruction status, HIGH for obstructed, LOW for clear
#define STATUS_DOOR_PIN         D0  // output door status, HIGH for open, LOW for closed
#define DRY_CONTACT_OPEN_PIN    D5  // dry contact for open door limit switch
#define DRY_CONTACT_CLOSE_PIN   D6  // dry contact for close door limit switch

/********************************** MODEL *****************************************/

enum GarageDoorCurrentState : uint8_t
{
    CURR_OPEN = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_OPEN,
    CURR_CLOSED = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_CLOSED,
    CURR_OPENING = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_OPENING,
    CURR_CLOSING = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_CLOSING,
    CURR_STOPPED = HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_STOPPED,
};

enum GarageDoorTargetState : uint8_t
{
    TGT_OPEN = HOMEKIT_CHARACTERISTIC_TARGET_DOOR_STATE_OPEN,
    TGT_CLOSED = HOMEKIT_CHARACTERISTIC_TARGET_DOOR_STATE_CLOSED,
};

enum LockCurrentState : uint8_t
{
    CURR_UNLOCKED = HOMEKIT_CHARACTERISTIC_CURRENT_LOCK_STATE_UNSECURED,
    CURR_LOCKED = HOMEKIT_CHARACTERISTIC_CURRENT_LOCK_STATE_SECURED,
    CURR_JAMMED = HOMEKIT_CHARACTERISTIC_CURRENT_LOCK_STATE_JAMMED,
    CURR_UNKNOWN = HOMEKIT_CHARACTERISTIC_CURRENT_LOCK_STATE_UNKNOWN,
};

enum LockTargetState : uint8_t
{
    TGT_UNLOCKED = HOMEKIT_CHARACTERISTIC_TARGET_LOCK_STATE_UNSECURED,
    TGT_LOCKED = HOMEKIT_CHARACTERISTIC_TARGET_LOCK_STATE_SECURED,
};

struct GarageDoor
{
    bool active;
    GarageDoorCurrentState current_state;
    GarageDoorTargetState target_state;
    bool obstructed;
    bool has_motion_sensor;
    unsigned long motion_timer;
    bool motion;
    bool light;
    LockCurrentState current_lock;
    LockTargetState target_lock;
};

struct ForceRecover
{
    uint8_t push_count;
    unsigned long timeout;
};

class LED
{
private:
    uint8_t activeState = 0;
    uint8_t idleState = 1;       // opposite of active
    unsigned long resetTime = 0; // Stores time when LED should return to idle state
    bool initialized = false;
    bool enabled = true;

public:
    LED();

    void on();
    void off();
    void idle();
    void flash(unsigned long ms = 0);
    void setIdleState(uint8_t state);
    uint8_t getIdleState() { return (idleState == activeState) ? 2 : idleState; };
};

extern LED led;

#define LOOP_SYSTEM 0
#define LOOP_IMPROV 1
#define LOOP_COMMS 2
#define LOOP_HK 3
#define LOOP_TIMER 4
#define LOOP_WEB 5
extern uint8_t loop_id;

#define FLASH_MS 50

#endif // _RATGDO_H
