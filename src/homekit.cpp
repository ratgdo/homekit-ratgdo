// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include <arduino_homekit_server.h>
#include "ratgdo.h"
#include "comms.h"
#include "log.h"

// Bring in config and characteristics defined in homekit_decl.c
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t current_door_state;
extern "C" homekit_characteristic_t target_door_state;
extern "C" homekit_characteristic_t obstruction_detected;
extern "C" homekit_characteristic_t active_state;

// Bring in the garage door state storage in ratgdo.c
extern struct GarageDoor garage_door;

// Forward-declare setters used by characteristics
homekit_value_t current_door_state_get();
homekit_value_t target_door_state_get();
void target_door_state_set(const homekit_value_t new_value);
homekit_value_t obstruction_detected_get();
homekit_value_t active_state_get();

/********************************** MAIN LOOP CODE *****************************************/

void homekit_loop() {
    arduino_homekit_loop();
}

void setup_homekit() {

    current_door_state.getter = current_door_state_get;
    target_door_state.getter = target_door_state_get;
    target_door_state.setter = target_door_state_set;
    obstruction_detected.getter = obstruction_detected_get;
    active_state.getter = active_state_get;

    arduino_homekit_setup(&config);
}

/******************************** GETTERS AND SETTERS ***************************************/

homekit_value_t current_door_state_get() {
    RINFO("get current door state: %d", garage_door.current_state);

    return HOMEKIT_UINT8_CPP(garage_door.current_state);
}

homekit_value_t target_door_state_get() {
    RINFO("get target door state: %d", garage_door.target_state);

    return HOMEKIT_UINT8_CPP(garage_door.target_state);
}

void target_door_state_set(const homekit_value_t value) {
    RINFO("set door state: %d", value.uint8_value);

    switch (value.uint8_value) {
        case TGT_OPEN:
            open_door();
            break;
        case TGT_CLOSED:
            close_door();
            break;
        default:
            ERROR("invalid target door state set requested: %d", value.uint8_value);
            break;
    }

}

homekit_value_t obstruction_detected_get() {
    RINFO("get obstruction: %d", garage_door.obstructed);
    return HOMEKIT_BOOL_CPP(garage_door.obstructed);
}

homekit_value_t active_state_get() {
    RINFO("get active: %d", garage_door.active);
    return HOMEKIT_BOOL_CPP(garage_door.active);
}

void notify_homekit_target_door_state_change() {
    homekit_characteristic_notify(
        &target_door_state,
        HOMEKIT_UINT8_CPP(garage_door.target_state)
    );
}

void notify_homekit_current_door_state_change() {
    homekit_characteristic_notify(
        &current_door_state,
        HOMEKIT_UINT8_CPP(garage_door.current_state)
    );
}

void notify_homekit_active() {
    homekit_characteristic_notify(
        &active_state,
        HOMEKIT_BOOL_CPP(true)
    );
}
