// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

/*
 * HomeKit accessory, service, and characteristic declarations and definitions.
 *
 * These are in a C file and included via `extern "C"` from the C++ sources in
 * order to make their declarations a bit easier to write and understand.
 *
 * TODO add a service for the light
 * TODO add a service for the motion detector
 */

#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "homekit_decl.h"

// Called to identify this accessory (HAP section 6.7.6 Identify Routine)
//
// Called when paired successfully or after clicking the "Identify Accessory"
// button in Home app.
void identify(homekit_value_t _value) {
    printf("accessory identify\n");
}

char device_name[19] = "XXXXXX\0";
char serial_number[18] = "XXXXXX\0";

homekit_characteristic_t active_state = HOMEKIT_CHARACTERISTIC_(
        STATUS_ACTIVE, false,
        );


homekit_characteristic_t current_door_state = HOMEKIT_CHARACTERISTIC_(
        CURRENT_DOOR_STATE, HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_CLOSED,
        //.getter=current_door_state_get,
        //.setter=NULL
        );

homekit_characteristic_t target_door_state = HOMEKIT_CHARACTERISTIC_(
        TARGET_DOOR_STATE, HOMEKIT_CHARACTERISTIC_TARGET_DOOR_STATE_CLOSED,
        //.getter=target_door_state_get,
        //.setter=target_door_state_set
        );

homekit_characteristic_t obstruction_detected = HOMEKIT_CHARACTERISTIC_(
        OBSTRUCTION_DETECTED, HOMEKIT_CHARACTERISTIC_TARGET_DOOR_STATE_CLOSED,
        //.getter=obstruction_detected_get,
        //.setter=NULL
        );

homekit_characteristic_t motion_detected = HOMEKIT_CHARACTERISTIC_(
        MOTION_DETECTED, false,
        );

// Declare and define the accessory
homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_garage, .services=(homekit_service_t*[]){
            HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, device_name),
                    HOMEKIT_CHARACTERISTIC(MANUFACTURER, "ratCloud llc"),
                    HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, serial_number),
                    HOMEKIT_CHARACTERISTIC(MODEL, "ratgdo"),
                    HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, AUTO_VERSION),
                    HOMEKIT_CHARACTERISTIC(IDENTIFY, identify),
                    NULL
                    }),
            HOMEKIT_SERVICE(GARAGE_DOOR_OPENER, .primary=true, .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "ratgdo"),
                    &active_state,
                    &current_door_state,
                    &target_door_state,
                    &obstruction_detected,
                    NULL
                    }),
            HOMEKIT_SERVICE(MOTION_SENSOR, .primary=false, .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "ratgdo-sensor"),
                    &motion_detected,
                    NULL
                    }),
            NULL
    }),
    NULL
};

// Overall HomeKit server config
homekit_server_config_t config = {
    .accessories = accessories,
    .password = "251-02-023",  // On Oct 25, 2023, Chamberlain announced they were disabling API
                               // access for "unauthorized" third parties.
    .setupId = "RTGO",
};
