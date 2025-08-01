// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include <arduino_homekit_server.h>
#include <ESP8266WiFi.h>

#include "ratgdo.h"
#include "config.h"
#include "comms.h"
#include "web.h"

// Logger tag
static const char *TAG = "ratgdo-homekit";

bool homekit_setup_done = false;

char qrPayload[21];

// Forward-declare setters used by characteristics
homekit_value_t current_door_state_get();
homekit_value_t target_door_state_get();
void target_door_state_set(const homekit_value_t new_value);
homekit_value_t obstruction_detected_get();
homekit_value_t active_state_get();
homekit_value_t current_lock_state_get();
homekit_value_t target_lock_state_get();
void target_lock_state_set(const homekit_value_t new_value);
homekit_value_t light_state_get();
void light_state_set(const homekit_value_t value);

/****************************************************************************
 * Convert a decimal number to base62 (so can use A-Za-z0-9 to represent it.
 */
char *toBase62(char *base62, size_t len, uint32_t base10)
{
    static const char base62Chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    size_t i = 0;
    // Will pad with zeros until base62 buffer filled (to len)
    while ((base10 > 0) || (i < len - 1))
    {
        base62[i++] = base62Chars[base10 % 62];
        base10 /= 62;
    }
    // null terminate
    base62[i] = 0;
    // Now reverse order of the string;
    char *str = base62;
    char *end = str + strlen(str) - 1;
    while (str < end)
    {
        *str ^= *end;
        *end ^= *str;
        *str ^= *end;
        str++;
        end--;
    }
    return base62;
}

/********************************** MAIN LOOP CODE *****************************************/

void homekit_loop()
{
    if (!homekit_setup_done && !comms_status_done)
        return;

    arduino_homekit_loop();
}

void setup_homekit()
{
    if (homekit_setup_done || softAPmode)
        return;

    ESP_LOGI(TAG, "=== Starting HomeKit Server");
    String macAddress = WiFi.macAddress();
    snprintf(serial_number, SERIAL_NAME_SIZE, "%s", macAddress.c_str());

    current_door_state.getter = current_door_state_get;
    target_door_state.getter = target_door_state_get;
    target_door_state.setter = target_door_state_set;
    obstruction_detected.getter = obstruction_detected_get;
    active_state.getter = active_state_get;
    current_lock_state.getter = current_lock_state_get;
    target_lock_state.getter = target_lock_state_get;
    target_lock_state.setter = target_lock_state_set;
    light_state.getter = light_state_get;
    light_state.setter = light_state_set;

    // Generate a QR Code ID from our MAC address, which should create unique pairing QR codes
    // for each of multiple devices on a network... although we do have to clip to 4 characters,
    // so we loose ~2 most significant bits.
    uint8_t mac[WL_MAC_ADDR_LENGTH];
    WiFi.macAddress(mac);
    uint32_t uid = (mac[3] << 16) + (mac[4] << 8) + mac[5];
    static char HKpassword[] = "251-02-023"; // On Oct 25, 2023, Chamberlain announced they were disabling API
                                             // access for "unauthorized" third parties.
    static char setupID[6];
    toBase62(setupID, sizeof(setupID), uid); // always includes leading zeros
    // setupID will be string "0ABCD" plus null terminator.  We throw away the first char.
    ESP_LOGI(TAG, "HomeKit pairing QR Code ID: %s", &setupID[1]);
    // X-HM://0042WZMX3 + setupID... string is constant, precalculated from 25102023
    // and Category::GarageDoorOpeners in the HomeSpan version of setup code.
    strlcpy(qrPayload, "X-HM://0042WZMX3", sizeof(qrPayload));
    sprintf(&qrPayload[16], "%-4.4s", &setupID[1]);
    ESP_LOGI(TAG, "HomeKit QR setup payload: %s", qrPayload);
    config.password = HKpassword;
    config.setupId = &setupID[1];

    garage_door.has_motion_sensor = (bool)read_int_from_file(nvram_has_motion);
    if (!garage_door.has_motion_sensor && (userConfig->getMotionTriggers() == 0))
    {
        ESP_LOGI(TAG, "Motion Sensor not detected.  Disabling Service");
        config.accessories[0]->services[3] = NULL;
    }
    if (userConfig->getGDOSecurityType() == 3)
    {
        ESP_LOGI(TAG, "Dry contact does not support light control.  Disabling Service");
        config.accessories[0]->services[2] = NULL;
    }

    // We can set current lock state to unknown as HomeKit has value for that.
    // But we can't do the same for door state as HomeKit has no value for that.
    garage_door.current_lock = CURR_UNKNOWN;
    arduino_homekit_setup(&config);
    homekit_setup_done = true;
}

/******************************** GETTERS AND SETTERS ***************************************/

homekit_value_t current_door_state_get()
{
    ESP_LOGI(TAG, "get current door state: %d", garage_door.current_state);

    return HOMEKIT_UINT8_CPP(garage_door.current_state);
}

homekit_value_t target_door_state_get()
{
    ESP_LOGI(TAG, "get target door state: %d", garage_door.target_state);

    return HOMEKIT_UINT8_CPP(garage_door.target_state);
}

void target_door_state_set(const homekit_value_t value)
{
    ESP_LOGI(TAG, "set door state: %d", value.uint8_value);

    switch (value.uint8_value)
    {
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

homekit_value_t obstruction_detected_get()
{
    ESP_LOGI(TAG, "get obstruction: %d", garage_door.obstructed);
    return HOMEKIT_BOOL_CPP(garage_door.obstructed);
}

homekit_value_t active_state_get()
{
    ESP_LOGI(TAG, "get active: %d", garage_door.active);
    return HOMEKIT_BOOL_CPP(garage_door.active);
}

homekit_value_t current_lock_state_get()
{
    ESP_LOGI(TAG, "get current lock state: %d", garage_door.current_lock);

    return HOMEKIT_UINT8_CPP(garage_door.current_lock);
}

homekit_value_t target_lock_state_get()
{
    ESP_LOGI(TAG, "get target lock state: %d", garage_door.target_lock);

    return HOMEKIT_UINT8_CPP(garage_door.target_lock);
}

void target_lock_state_set(const homekit_value_t value)
{
    ESP_LOGI(TAG, "set lock state: %d", value.uint8_value);

    set_lock(value.uint8_value);
}

/****************************************************************************
 * Garage Door Service Handler
 */
void notify_homekit_target_door_state_change(GarageDoorTargetState state)
{
    garage_door.target_state = state;
#ifdef ESP32
    if (!isPaired)
        return;

    GDOEvent e;
    e.c = door->target;
    e.value.u = (uint8_t)garage_door.target_state;
    queueSendHelper(door->event_q, e, "target door");
#else
    if (!arduino_homekit_get_running_server())
        return;

    homekit_characteristic_notify(&target_door_state, HOMEKIT_UINT8_CPP(garage_door.target_state));
#endif
}

void notify_homekit_current_door_state_change(GarageDoorCurrentState state)
{
    garage_door.current_state = state;
#ifdef ESP32
    if (!isPaired)
        return;

    GDOEvent e;
    e.c = door->current;
    e.value.u = (uint8_t)garage_door.current_state;
    queueSendHelper(door->event_q, e, "current door");
#else
    if (!arduino_homekit_get_running_server())
        return;

    homekit_characteristic_notify(&current_door_state, HOMEKIT_UINT8_CPP(garage_door.current_state));

#define doorOpening() // Noop on ESP8266
#define doorClosing() // Noop on ESP8266
#endif
    // Set target door state to match.
    switch (state)
    {
    case CURR_OPENING:
        // #ifdef ESP32
        doorOpening(); // Fall through...
                       // #endif
    case CURR_OPEN:
        notify_homekit_target_door_state_change(TGT_OPEN);
        break;

    case CURR_CLOSING:
        // #ifdef ESP32
        doorClosing(); // Fall through...
                       // #endif
    case CURR_CLOSED:
        notify_homekit_target_door_state_change(TGT_CLOSED);
        break;

    default:
        // Ignore other states.
        break;
    }
}

#ifndef ESP32
void notify_homekit_active()
{
    if (!arduino_homekit_get_running_server())
        return;

    homekit_characteristic_notify(&active_state, HOMEKIT_BOOL_CPP(true));
}

homekit_value_t light_state_get()
{
    ESP_LOGI(TAG, "get light state: %s", garage_door.light ? "On" : "Off");

    return HOMEKIT_BOOL_CPP(garage_door.light);
}

void light_state_set(const homekit_value_t value)
{
    ESP_LOGI(TAG, "set light: %s", value.bool_value ? "On" : "Off");

    set_light(value.bool_value);
}
#endif

void notify_homekit_target_lock(LockTargetState state)
{
    garage_door.target_lock = state;
#ifdef ESP32
    if (!isPaired)
        return;

    GDOEvent e;
    e.c = door->lockTarget;
    e.value.u = (uint8_t)garage_door.target_lock;
    queueSendHelper(door->event_q, e, "target lock");
#else
    if (!arduino_homekit_get_running_server())
        return;

    homekit_characteristic_notify(&target_lock_state, HOMEKIT_UINT8_CPP(garage_door.target_lock));
#endif
}

void notify_homekit_current_lock(LockCurrentState state)
{
    garage_door.current_lock = state;
#ifdef ESP32
    if (!isPaired)
        return;

    GDOEvent e;
    e.c = door->lockCurrent;
    e.value.u = (uint8_t)garage_door.current_lock;
    queueSendHelper(door->event_q, e, "current lock");
#else
    if (!arduino_homekit_get_running_server())
        return;

    homekit_characteristic_notify(&current_lock_state, HOMEKIT_UINT8_CPP(garage_door.current_lock));
#endif
}

void notify_homekit_obstruction(bool state)
{
    garage_door.obstructed = state;
#ifdef ESP32
    if (!isPaired)
        return;

    GDOEvent e;
    e.c = door->obstruction;
    e.value.b = garage_door.obstructed;
    queueSendHelper(door->event_q, e, "obstruction");
#else
    if (!arduino_homekit_get_running_server())
        return;

    homekit_characteristic_notify(&obstruction_detected, HOMEKIT_BOOL_CPP(garage_door.obstructed));
#endif
}

/****************************************************************************
 * Light Service Handler
 */
void notify_homekit_light(bool state)
{
    garage_door.light = state;
#ifdef ESP32
    if (!isPaired || !light)
        return;

    GDOEvent e;
    e.value.b = garage_door.light;
    queueSendHelper(light->event_q, e, "light");
#else
    if (!arduino_homekit_get_running_server())
        return;

    homekit_characteristic_notify(&light_state, HOMEKIT_BOOL_CPP(garage_door.light));
#endif
}

/****************************************************************************
 * Motion Service Handler
 */
void enable_service_homekit_motion(bool reboot)
{
#ifdef ESP32
    // only create if not already created
    if (!garage_door.has_motion_sensor)
    {
        nvRam->write(nvram_has_motion, 1);
        garage_door.has_motion_sensor = true;
        createMotionAccessories();
        if (reboot)
        {
            sync_and_restart();
        }
    }
#else
    write_int_to_file(nvram_has_motion, 1);
    if (reboot)
    {
        sync_and_restart();
    }
#endif
}

void notify_homekit_motion(bool state)
{
    garage_door.motion = state;
    garage_door.motion_timer = (!state) ? 0 : _millis() + MOTION_TIMER_DURATION;
#ifdef ESP32
    if (!isPaired || !motion)
        return;

    GDOEvent e;
    e.value.b = garage_door.motion;
    queueSendHelper(motion->event_q, e, "motion");
#else
    if (!arduino_homekit_get_running_server())
        return;

    homekit_characteristic_notify(&motion_detected, HOMEKIT_BOOL_CPP(garage_door.motion));
#endif
}
