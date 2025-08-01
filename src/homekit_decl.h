// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef _HOMEKIT_DECL_H
#define _HOMEKIT_DECL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#define SERIAL_NAME_SIZE 18
#define DEVICE_NAME_SIZE 32

extern char device_name[DEVICE_NAME_SIZE];
extern char device_name_rfc952[DEVICE_NAME_SIZE];
extern char serial_number[SERIAL_NAME_SIZE];

// Bring in config and characteristics defined in homekit_decl.c
extern homekit_server_config_t config;
extern homekit_characteristic_t current_door_state;
extern homekit_characteristic_t target_door_state;
extern homekit_characteristic_t obstruction_detected;
extern homekit_characteristic_t active_state;
extern homekit_characteristic_t current_lock_state;
extern homekit_characteristic_t target_lock_state;
extern homekit_characteristic_t light_state;
extern homekit_characteristic_t motion_detected;

#ifdef __cplusplus
}
#endif
#endif // _HOMEKIT_DECL_H
