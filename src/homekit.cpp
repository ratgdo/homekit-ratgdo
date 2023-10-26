#include <arduino_homekit_server.h>
#include "ratgdo.h"
#include "comms.h"

// Bring in config and characteristics defined in homekit_decl.c
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t current_door_state;
extern "C" homekit_characteristic_t target_door_state;
extern "C" homekit_characteristic_t obstruction_detected;

// Bring in the garage door state storage in ratgdo.c
extern struct GarageDoor garage_door;

// Forward-declare setters used by characteristics
homekit_value_t current_door_state_get();
homekit_value_t target_door_state_get();
void target_door_state_set(const homekit_value_t new_value);
homekit_value_t obstruction_detected_get();

/********************************** MAIN LOOP CODE *****************************************/

void homekit_loop() {
    arduino_homekit_loop();
}

void setup_homekit() {

    current_door_state.getter = current_door_state_get;
    target_door_state.getter = target_door_state_get;
    target_door_state.setter = target_door_state_set;
    obstruction_detected.getter = obstruction_detected_get;

    arduino_homekit_setup(&config);
}

/******************************** GETTERS AND SETTERS ***************************************/

homekit_value_t current_door_state_get() {
    return HOMEKIT_UINT8_CPP(garage_door.current_state);
}

homekit_value_t target_door_state_get() {
    return HOMEKIT_UINT8_CPP(garage_door.target_state);
}

void target_door_state_set(const homekit_value_t value) {
    switch (value.uint8_value) {
        case TGT_OPEN:
            open_door();
            break;
        case TGT_CLOSED:
            close_door();
            break;
        default:
            // ?
            break;
    }
}

homekit_value_t obstruction_detected_get() {
    return HOMEKIT_BOOL_CPP(garage_door.obstructed);
}
