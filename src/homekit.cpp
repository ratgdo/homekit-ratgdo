// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License
#define TAG ("HOMEKIT")

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <esp_log.h>
#include <esp_system.h>

#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>

#include "homekit_decl.h"
#include "ratgdo.h"
#include "comms.h"
#include "log.h"

#define DEVICE_NAME_SIZE 19
#define SERIAL_NAME_SIZE 18

// Bring in the garage door state storage in ratgdo.c
extern struct GarageDoor garage_door;

// Make device_name available
char device_name[DEVICE_NAME_SIZE];

// Make serial_number available
char serial_number[SERIAL_NAME_SIZE];

// Queue to store GDO notification events
static QueueHandle_t gdo_notif_event_q;

enum class HomeKitNotifDest {
    DoorCurrentState,
    DoorTargetState,
    LockCurrentState,
    LockTargetState,
    Obstruction,
    Light,
    Motion,
};

struct GDOEvent {
    HomeKitNotifDest dest;
    union {
        bool b;
        uint8_t u;
    } value;
};

static int gdo_svc_set(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv);
static int light_svc_set(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv);


/********************************** MAIN LOOP CODE *****************************************/

int identify(hap_acc_t *acc) {
    ESP_LOGI(TAG, "identify called");
    return HAP_SUCCESS;
}

void homekit_task_entry(void* ctx) {

    uint8_t mac[8] = {0};
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));

    snprintf(device_name, DEVICE_NAME_SIZE, "Garage Door %02X%02X%02X", mac[2], mac[1], mac[0]);
    snprintf(
        serial_number,
        SERIAL_NAME_SIZE,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );

    hap_acc_t *accessory;
    hap_serv_t *gdo_svc;
    hap_serv_t *motion_svc;
    hap_serv_t *light_svc;

    gdo_notif_event_q = xQueueCreate(5, sizeof(GDOEvent));

    hap_init(HAP_TRANSPORT_WIFI);

    hap_acc_cfg_t config;
    config.name = device_name;
    config.manufacturer = "ratCloud llc";
    config.model = "ratgdo";
    config.serial_num = serial_number;
    config.fw_rev = AUTO_VERSION;
    config.hw_rev = NULL;
    config.identify_routine = identify;
    config.cid = HAP_CID_GARAGE_DOOR_OPENER;

    accessory = hap_acc_create(&config);

    // create garage door opener service with optional lock characteristics
    gdo_svc = hap_serv_garage_door_opener_create(
            HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_OPEN,
            HOMEKIT_CHARACTERISTIC_TARGET_DOOR_STATE_OPEN,
            HOMEKIT_CHARACTERISTIC_OBSTRUCTION_SENSOR_CLEAR);
    hap_serv_add_char(gdo_svc, hap_char_name_create("ratgdo"));
    hap_serv_add_char(gdo_svc, hap_char_lock_current_state_create(0));
    hap_serv_add_char(gdo_svc, hap_char_lock_target_state_create(0));

    hap_serv_set_write_cb(gdo_svc, gdo_svc_set);

    hap_acc_add_serv(accessory, gdo_svc);

    // create the motion sensor service with no optional characteristics (e.g. active)
    motion_svc = hap_serv_motion_sensor_create(false);

    hap_acc_add_serv(accessory, motion_svc);

    // create the light service with no optional characteristics (e.g. brightness)
    light_svc = hap_serv_lightbulb_create(false);

    hap_serv_set_write_cb(light_svc, light_svc_set);

    hap_acc_add_serv(accessory, light_svc);

    hap_add_accessory(accessory);

    // initialize and start homekit
    hap_set_setup_code("251-02-023");  // On Oct 25, 2023, Chamberlain announced they were disabling API
                                       // access for "unauthorized" third parties.
    hap_set_setup_id("RTGO");
    hap_start();

    GDOEvent e;

    hap_char_t* dest = NULL;

    while (true) {
        hap_val_t value;

        if (xQueueReceive(gdo_notif_event_q, &e, portMAX_DELAY)) {
            switch (e.dest) {
                case HomeKitNotifDest::DoorCurrentState:
                    dest = hap_serv_get_char_by_uuid(gdo_svc, HAP_CHAR_UUID_CURRENT_DOOR_STATE);
                    value.u = e.value.u;
                    break;
                case HomeKitNotifDest::DoorTargetState:
                    dest = hap_serv_get_char_by_uuid(gdo_svc, HAP_CHAR_UUID_TARGET_DOOR_STATE);
                    value.u = e.value.u;
                    break;
                case HomeKitNotifDest::LockCurrentState:
                    dest = hap_serv_get_char_by_uuid(gdo_svc, HAP_CHAR_UUID_LOCK_CURRENT_STATE);
                    value.b = e.value.b;
                    break;
                case HomeKitNotifDest::LockTargetState:
                    dest = hap_serv_get_char_by_uuid(gdo_svc, HAP_CHAR_UUID_LOCK_TARGET_STATE);
                    value.b = e.value.b;
                    break;
                case HomeKitNotifDest::Obstruction:
                    dest = hap_serv_get_char_by_uuid(gdo_svc, HAP_CHAR_UUID_OBSTRUCTION_DETECTED);
                    value.b = e.value.b;
                    break;
                case HomeKitNotifDest::Light:
                    dest = hap_serv_get_char_by_uuid(light_svc, HAP_CHAR_UUID_ON);
                    value.b = e.value.b;
                    break;
                case HomeKitNotifDest::Motion:
                    dest = hap_serv_get_char_by_uuid(motion_svc, HAP_CHAR_UUID_MOTION_DETECTED);
                    value.b = e.value.b;
                    break;
            }
            if (dest) {
                ESP_LOGI(TAG, "updating characteristic");
                if (hap_char_update_val(dest, &value) == HAP_FAIL) {
                    ESP_LOGE(TAG, "failed to update characteristic");
                }
            }
        }
    }
}

/******************************** GETTERS AND SETTERS ***************************************/

static int gdo_svc_set(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv) {

    int i, ret = HAP_SUCCESS;
    hap_write_data_t *write;

    for (i = 0; i < count; i++) {
        write = &write_data[i];

        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_TARGET_DOOR_STATE)) {
            ESP_LOGI(TAG, "set door state: %d", write->val.u);
            switch (write->val.u) {
                case TGT_OPEN:
                    open_door();
                    hap_char_update_val(write->hc, &(write->val));
                    *(write->status) = HAP_STATUS_SUCCESS;
                    break;
                case TGT_CLOSED:
                    close_door();
                    hap_char_update_val(write->hc, &(write->val));
                    *(write->status) = HAP_STATUS_SUCCESS;
                    break;
                default:
                    ESP_LOGE(TAG, "invalid target door state set requested: %d", write->val.u);
                    *(write->status) = HAP_STATUS_VAL_INVALID;
                    ret = HAP_FAIL;
                    break;
            }
            hap_char_update_val(write->hc, &(write->val));
            *(write->status) = HAP_STATUS_SUCCESS;

        } else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_LOCK_TARGET_STATE)) {
            ESP_LOGI(TAG, "set lock state: %s", write->val.b ? "Locked" : "Unlocked");
            set_lock(write->val.b);
            hap_char_update_val(write->hc, &(write->val));
            *(write->status) = HAP_STATUS_SUCCESS;

        } else {
            // no other characteristics are settable
            ESP_LOGE(TAG, "invalid characteristic set, requested UUID: %s", hap_char_get_type_uuid(write->hc));
            *(write->status) = HAP_STATUS_RES_ABSENT;
            ret = HAP_FAIL;

        }
    }

    return ret;
}

void notify_homekit_target_door_state_change() {
    ESP_LOGI(TAG, "notifying homekit of target door status %d", garage_door.target_state);
    GDOEvent e;
    e.dest = HomeKitNotifDest::DoorTargetState;
    e.value.u = garage_door.target_state;
    if (!gdo_notif_event_q || xQueueSend(gdo_notif_event_q, &e, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "could not queue homekit notif of door target state %d", garage_door.target_state);
    }
}

void notify_homekit_current_door_state_change() {
    ESP_LOGI(TAG, "notifying homekit of current door status %d", garage_door.current_state);
    GDOEvent e;
    e.dest = HomeKitNotifDest::DoorCurrentState;
    e.value.u = garage_door.current_state;
    if (!gdo_notif_event_q || xQueueSend(gdo_notif_event_q, &e, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "could not queue homekit notif of door current state %d", garage_door.current_state);
    }
}

static int light_svc_set(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv) {

    int i, ret = HAP_SUCCESS;
    hap_write_data_t *write;
    for (i = 0; i < count; i++) {
        write = &write_data[i];

        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ON)) {
            ESP_LOGI(TAG, "set light: %s", write->val.b ? "On" : "Off");
            set_light(write->val.b);
            hap_char_update_val(write->hc, &(write->val));
            *(write->status) = HAP_STATUS_SUCCESS;

        } else {
            // no other characteristics are settable
            ESP_LOGE(TAG, "invalid characteristic set, requested UUID: %s", hap_char_get_type_uuid(write->hc));
            *(write->status) = HAP_STATUS_RES_ABSENT;
            ret = HAP_FAIL;

        }
    }

    return ret;
}

void notify_homekit_obstruction() {
    GDOEvent e;
    e.dest = HomeKitNotifDest::Obstruction;
    e.value.b = garage_door.obstructed;
    if (!gdo_notif_event_q || xQueueSend(gdo_notif_event_q, &e, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "could not queue homekit notif of door obstructed %d", garage_door.obstructed);
    }
}

void notify_homekit_current_lock() {
    GDOEvent e;
    e.dest = HomeKitNotifDest::LockCurrentState;
    e.value.b = garage_door.current_lock;
    if (!gdo_notif_event_q || xQueueSend(gdo_notif_event_q, &e, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "could not queue homekit notif of lock state %d", garage_door.current_lock);
    }
}

void notify_homekit_target_lock() {
    GDOEvent e;
    e.dest = HomeKitNotifDest::LockTargetState;
    e.value.b = garage_door.target_lock;
    if (!gdo_notif_event_q || xQueueSend(gdo_notif_event_q, &e, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "could not queue homekit notif of lock tgt %d", garage_door.target_lock);
    }
}

void notify_homekit_light() {
    ESP_LOGI(TAG, "notifying homekit of light status %s", garage_door.light ? "on" : "off");
    GDOEvent e;
    e.dest = HomeKitNotifDest::Light;
    e.value.b = garage_door.light;
    if (!gdo_notif_event_q || xQueueSend(gdo_notif_event_q, &e, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "could not queue homekit notif of light state %d", garage_door.light);
    }
}

void notify_homekit_motion() {
    GDOEvent e;
    e.dest = HomeKitNotifDest::Motion;
    e.value.b = garage_door.motion;
    if (!gdo_notif_event_q || xQueueSend(gdo_notif_event_q, &e, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "could not queue homekit notif of motion %d", garage_door.motion);
    }
}
