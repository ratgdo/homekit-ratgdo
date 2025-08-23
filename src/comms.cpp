/****************************************************************************
 * RATGDO HomeKit
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-25 David A Kerr... https://github.com/dkerr64/
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 * Contributions acknowledged from
 * Thomas Hagan...     https://github.com/tlhagan
 * Brandon Matthews... https://github.com/thenewwazoo
 * Jonathan Stroud...  https://github.com/jgstroud
 * Mitchell Solomon... https://github.com/mitchjs
 *
 */

// Arduino includes
#include <Ticker.h>

// RATGDO project includes
#include "ratgdo.h"
#include "homekit.h"
#include "config.h"
#include "comms.h"
#include "led.h"

#ifdef ESP32
#ifdef USE_GDOLIB
#include "gdo.h"
#else // USE_GDOLIB
#include "SoftwareSerial.h"
#include "../lib/ratgdo/Packet.h"
#include "../lib/ratgdo/Reader.h"
#include "../lib/ratgdo/secplus2.h"
#include "drycontact.h"
#endif // USE_GDOLIB
#else  // ESP32
#include "SoftwareSerial.h"
#include "Reader.h"
#include "secplus2.h"
#include "Packet.h"
#include "cQueue.h"
#endif // ESP32

static const char *TAG = "ratgdo-comms";

static bool comms_setup_done = false;

/********************************** LOCAL STORAGE *****************************************/
#ifndef USE_GDOLIB
struct __attribute__((aligned(4))) PacketAction
{
    Packet pkt;
    bool inc_counter;
    uint32_t delay;
};

#define COMMAND_QUEUE_SIZE 16
#ifdef ESP32
QueueHandle_t pkt_q;
#else
Queue_t pkt_q;
#endif // ESP32
SoftwareSerial sw_serial;
#endif // not USE_GDOLIB

#define SECPLUS1_DIGITAL_WALLPLATE_TIMEOUT 15000
#define SECPLUS1_RX_MESSAGE_TIMEOUT 10
#define SECPLUS1_TX_WINDOW 20
#define SECPLUS1_TX_MINIMUM_DELAY 30

#define COMMS_STATUS_TIMEOUT 2000
bool comms_status_done = false;
_millis_t comms_status_start = 0;

uint32_t doorControlType = 0;

// For Time-to-close control
Ticker TTCtimer = Ticker();
Ticker callbackDelay = Ticker();
bool TTCwasLightOn = false;

struct ForceRecover force_recover;
#define force_recover_delay 3

#ifdef USE_GDOLIB
static gdo_status_t gdo_status;

std::map<gdo_door_state_t, GarageDoorCurrentState> gdo_to_homekit_door_current_state = {
    {GDO_DOOR_STATE_UNKNOWN, (GarageDoorCurrentState)0xFF},
    {GDO_DOOR_STATE_OPEN, GarageDoorCurrentState::CURR_OPEN},
    {GDO_DOOR_STATE_CLOSED, GarageDoorCurrentState::CURR_CLOSED},
    {GDO_DOOR_STATE_STOPPED, GarageDoorCurrentState::CURR_STOPPED},
    {GDO_DOOR_STATE_OPENING, GarageDoorCurrentState::CURR_OPENING},
    {GDO_DOOR_STATE_CLOSING, GarageDoorCurrentState::CURR_CLOSING},
    {GDO_DOOR_STATE_MAX, (GarageDoorCurrentState)0xFF},
};

std::map<gdo_door_state_t, GarageDoorTargetState> gdo_to_homekit_door_target_state = {
    {GDO_DOOR_STATE_UNKNOWN, (GarageDoorTargetState)0xFF},
    {GDO_DOOR_STATE_OPEN, GarageDoorTargetState::TGT_OPEN},
    {GDO_DOOR_STATE_CLOSED, GarageDoorTargetState::TGT_CLOSED},
    {GDO_DOOR_STATE_STOPPED, (GarageDoorTargetState)0xFF},
    {GDO_DOOR_STATE_OPENING, (GarageDoorTargetState)0xFF},
    {GDO_DOOR_STATE_CLOSING, (GarageDoorTargetState)0xFF},
    {GDO_DOOR_STATE_MAX, (GarageDoorTargetState)0xFF},
};

std::map<gdo_lock_state_t, LockCurrentState> gdo_to_homekit_lock_current_state = {
    {GDO_LOCK_STATE_UNLOCKED, LockCurrentState::CURR_UNLOCKED},
    {GDO_LOCK_STATE_LOCKED, LockCurrentState::CURR_LOCKED},
    {GDO_LOCK_STATE_MAX, (LockCurrentState)0xFF},
};

std::map<gdo_lock_state_t, LockTargetState> gdo_to_homekit_lock_target_state = {
    {GDO_LOCK_STATE_UNLOCKED, LockTargetState::TGT_UNLOCKED},
    {GDO_LOCK_STATE_LOCKED, LockTargetState::TGT_LOCKED},
    {GDO_LOCK_STATE_MAX, (LockTargetState)0xFF},
};

#else // not USE_GDOLIB
/******************************* OBSTRUCTION SENSOR *********************************/

// Track if we've detected a working obstruction sensor
static bool obstruction_sensor_detected = false;
static bool get_obstruction_from_status = false;

struct obstruction_sensor_t
{
    uint32_t low_count = 0;        // count obstruction low pulses
    _millis_t last_asleep = 0;     // count time between high pulses from the obst ISR
    bool pin_ever_changed = false; // track if pin has ever changed from initial stat
} obstruction_sensor;

void IRAM_ATTR isr_obstruction()
{
    obstruction_sensor.low_count++;
}

#define MAX_COMMS_RETRY 10

/******************************* SECURITY 2.0 *********************************/
SecPlus2Reader reader;
uint32_t id_code = 0;
uint32_t rolling_code = 0;
#endif // USE_GDOLIB

uint32_t last_saved_code = 0;
static bool rolling_code_operation_in_progress = false;
#define MAX_CODES_WITHOUT_FLASH_WRITE 10

/******************************* SECURITY 1.0 *********************************/
#ifndef USE_GDOLIB
static const uint8_t RX_LENGTH = 2;
typedef uint8_t RxPacket[RX_LENGTH * 4];
// time stamping
_millis_t last_tx;
_millis_t msg_start;
_millis_t cts_signal;
bool clearToSend = false;
// wall panel management
bool wallPanelBooting = false;
bool wallPanelDetected = false;
// states
GarageDoorCurrentState doorState = GarageDoorCurrentState::UNKNOWN;
uint8_t lightState;
uint8_t lockState;

// keep this here incase at somepoint its needed
// it is used for emulation of wall panel
// byte secplus1States[19] = {0x35,0x35,0x35,0x35,0x33,0x33,0x53,0x53,0x38,0x3A,0x3A,0x3A,0x39,0x38,0x3A, 0x38,0x3A,0x39,0x3A};
// this is what MY 889LM exhibited when powered up (release of all buttons, and then polls)
byte secplus1States[] = {0x35, 0x35, 0x33, 0x33, 0x38, 0x3A, 0x39};

// values for SECURITY+1.0 communication
enum secplus1Codes : uint8_t
{
    DoorButtonPress = 0x30,
    DoorButtonRelease = 0x31,
    LightButtonPress = 0x32,
    LightButtonRelease = 0x33,
    LockButtonPress = 0x34,
    LockButtonRelease = 0x35,

    Unkown_0x36 = 0x36,
    Unknown_0x37 = 0x37,

    DoorStatus = 0x38,
    ObstructionStatus = 0x39, // this is not proven
    LightLockStatus = 0x3A,
    Unknown = 0xFF
};

void sync();
bool process_PacketAction(PacketAction &pkt_ac);
void door_command(DoorAction action);
void send_get_status();
void send_get_openings();
bool transmitSec1(byte toSend);
bool transmitSec2(PacketAction &pkt_ac);
void obstruction_timer();
void sec1_light_press();
void sec1_light_release(uint8_t howManyReleases = 2);
#endif // not USE_GDOLIB

void manual_recovery();

#ifdef USE_GDOLIB
/****************************************************************************
 * Callback for GDOLIB status
 */
static void gdo_event_handler(const gdo_status_t *status, gdo_cb_event_t event, void *arg)
{
    switch (event)
    {
    case GDO_CB_EVENT_SYNCED:
        ESP_LOGI(TAG, "GDO event: synced: %s, protocol: %s", status->synced ? "true" : "false", gdo_protocol_type_to_string(status->protocol));
        if (status->protocol == GDO_PROTOCOL_SEC_PLUS_V2)
        {
            ESP_LOGI(TAG, "Client ID: %" PRIu32 ", Rolling code: %" PRIu32, status->client_id, status->rolling_code);
        }

        if (!status->synced)
        {
            if (gdo_set_rolling_code(status->rolling_code + 100) != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set rolling code");
            }
            else
            {
                ESP_LOGI(TAG, "Rolling code set to %" PRIu32 ", retrying sync", status->rolling_code);
                gdo_sync();
            }
        }
        break;
    case GDO_CB_EVENT_LIGHT:
        ESP_LOGI(TAG, "GDO event: light: %s", gdo_light_state_to_string(status->light));
        garage_door.light = status->light == gdo_light_state_t::GDO_LIGHT_STATE_ON;
        notify_homekit_light(garage_door.light);
        break;
    case GDO_CB_EVENT_LOCK:
        ESP_LOGI(TAG, "GDO event: lock: %s", gdo_lock_state_to_string(status->lock));
        garage_door.target_lock = gdo_to_homekit_lock_target_state[status->lock];
        garage_door.current_lock = gdo_to_homekit_lock_current_state[status->lock];
        notify_homekit_target_lock(garage_door.target_lock);
        notify_homekit_current_lock(garage_door.current_lock);
        break;
    case GDO_CB_EVENT_DOOR_POSITION:
    {
        GarageDoorCurrentState current_state = garage_door.current_state;
        ESP_LOGI(TAG, "GDO event: door: %s, %3d, target: %3d", gdo_door_state_to_string(status->door),
                 status->door_position / 100, (status->door_target >= 0) ? status->door_target / 100 : -1);
        garage_door.active = true;
        garage_door.current_state = gdo_to_homekit_door_current_state[status->door];
        if ((current_state != garage_door.current_state) && (status->door != GDO_DOOR_STATE_UNKNOWN))
        {
            // Notifying HomeKit of current state will also set target state as required.
            notify_homekit_current_door_state_change(garage_door.current_state);

            // If we are using Sec+2.0 built-in time-to-close then reset the TTC to zero when door is closed
            if (status->door == GDO_DOOR_STATE_CLOSED && doorControlType == 2 && userConfig->getBuiltInTTC())
                gdo_set_time_to_close(0);
        }
        break;
    }
    case GDO_CB_EVENT_LEARN:
        ESP_LOGI(TAG, "GDO event: learn: %s", gdo_learn_state_to_string(status->learn));
        break;
    case GDO_CB_EVENT_OBSTRUCTION:
        ESP_LOGI(TAG, "GDO event: obstruction: %s", gdo_obstruction_state_to_string(status->obstruction));
        garage_door.obstructed = status->obstruction == gdo_obstruction_state_t::GDO_OBSTRUCTION_STATE_OBSTRUCTED;
        notify_homekit_obstruction(garage_door.obstructed);
        if (motionTriggers.bit.obstruction && garage_door.obstructed)
        {
            notify_homekit_motion(true);
            notify_homekit_room_occupancy(true);
        }
        break;
    case GDO_CB_EVENT_MOTION:
        ESP_LOGI(TAG, "GDO event: motion: %s", gdo_motion_state_to_string(status->motion));
        // We got a motion message, so we know we have a motion sensor
        // If it's not yet enabled, add the service
        if (!garage_door.has_motion_sensor)
        {
            ESP_LOGI(TAG, "Detected new Motion Sensor. Enabling Service");
            motionTriggers.bit.motion = 1;
            userConfig->set(cfg_motionTriggers, motionTriggers.asInt);
            enable_service_homekit_motion(false); // ESP32 with HomeSpan can do this without reboot
        }
        notify_homekit_motion(status->motion == gdo_motion_state_t::GDO_MOTION_STATE_DETECTED);
        if (garage_door.motion)
            notify_homekit_room_occupancy(true);
        break;
    case GDO_CB_EVENT_BATTERY:
        ESP_LOGI(TAG, "GDO event: battery: %s", gdo_battery_state_to_string(status->battery));
        garage_door.batteryState = status->battery;
        break;
    case GDO_CB_EVENT_BUTTON:
        ESP_LOGI(TAG, "GDO event: button: %s", gdo_button_state_to_string(status->button));
        if (status->button == GDO_BUTTON_STATE_PRESSED)
            manual_recovery();
        break;
    case GDO_CB_EVENT_MOTOR:
        ESP_LOGI(TAG, "GDO event: motor: %s", gdo_motor_state_to_string(status->motor));
        break;
    case GDO_CB_EVENT_OPENINGS:
        ESP_LOGI(TAG, "GDO event: openings: %d", status->openings);
        garage_door.openingsCount = status->openings;
        break;
    case GDO_CB_EVENT_SET_TTC:
        ESP_LOGI(TAG, "GDO event: set TTC: %d", status->ttc_seconds);
        break;
    case GDO_CB_EVENT_UPDATE_TTC:
        ESP_LOGI(TAG, "GDO event: update TTC: %d", status->ttc_seconds);
        break;
    case GDO_CB_EVENT_CANCEL_TTC:
        ESP_LOGI(TAG, "GDO event: cancel TTC");
        if (doorControlType == 2 && userConfig->getBuiltInTTC())
            gdo_set_time_to_close(0);
        break;
    case GDO_CB_EVENT_PAIRED_DEVICES:
        ESP_LOGI(TAG, "GDO event: paired devices, %d remotes, %d keypads, %d wall controls, %d accessories, %d total",
                 status->paired_devices.total_remotes, status->paired_devices.total_keypads,
                 status->paired_devices.total_wall_controls, status->paired_devices.total_accessories,
                 status->paired_devices.total_all);
        break;
    case GDO_CB_EVENT_OPEN_DURATION_MEASUREMENT:
        garage_door.openDuration = (status->open_ms + 500) / 1000; // round up/down to closest second
        ESP_LOGI(TAG, "GDO event: open duration: %d seconds", garage_door.openDuration);
        break;
    case GDO_CB_EVENT_CLOSE_DURATION_MEASUREMENT:
        garage_door.closeDuration = (status->close_ms + 500) / 1000; // round up/down to closest second
        ESP_LOGI(TAG, "GDO event: close duration: %d seconds", garage_door.closeDuration);
        break;
    default:
        ESP_LOGI(TAG, "GDO event: unknown: %d", event);
        break;
    }

    // Save rolling code if we have exceeded max limit.
    gdo_status.rolling_code = status->rolling_code;
    // ESP_LOGI(TAG, "Rolling code: %lu", gdo_status.rolling_code);
    if (gdo_status.rolling_code >= (last_saved_code + MAX_CODES_WITHOUT_FLASH_WRITE))
    {
        save_rolling_code();
    }
}
#endif

/****************************************************************************
 * Initialize communications with garage door.
 */
void setup_comms()
{
    if (comms_setup_done)
        return;

    if (doorControlType == 0)
        doorControlType = userConfig->getGDOSecurityType();

#if defined(ESP8266) || !defined(USE_GDOLIB)
    IRAM_START(TAG);
    // IRAM heap is used only for allocating globals, to leave as much regular heap
    // available during operations.  We need to carefully monitor useage so as not
    // to exceed available IRAM.  We can adjust the LOG_BUFFER_SIZE (in log.h) if we
    // need to make more space available for initialization.
#ifdef ESP32
    pkt_q = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(PacketAction));
#else
    q_init(&pkt_q, sizeof(PacketAction), COMMAND_QUEUE_SIZE, FIFO, false);
#endif

    if (doorControlType == 1)
    {
        ESP_LOGI(TAG, "=== Setting up comms for SECURITY+1.0 protocol");

        sw_serial.begin(1200, SWSERIAL_8E1, UART_RX_PIN, UART_TX_PIN, true, 32);
        wallPanelDetected = false;
        wallPanelBooting = false;
        doorState = GarageDoorCurrentState::UNKNOWN;
        lightState = 2;
        lockState = 2;
    }
    else if (doorControlType == 2)
    {
        ESP_LOGI(TAG, "=== Setting up comms for SECURITY+2.0 protocol");

        sw_serial.begin(9600, SWSERIAL_8N1, UART_RX_PIN, UART_TX_PIN, true, 32);
        sw_serial.enableIntTx(false);
        sw_serial.enableAutoBaud(true); // found in ratgdo/espsoftwareserial branch autobaud

#ifdef ESP32
        id_code = nvRam->read(nvram_id_code);
#else
        id_code = read_int_from_file(nvram_id_code);
#endif
        if (!id_code)
        {
            ESP_LOGI(TAG, "id code not found");
            id_code = (random(0x1, 0xFFF) << 12) | 0x539;
#ifdef ESP32
            nvRam->write(nvram_id_code, id_code);
#else
            write_int_to_file(nvram_id_code, id_code);
#endif
        }
        ESP_LOGI(TAG, "id code %lu (0x%02lX)", id_code, id_code);

        // read from flash, default of 0 if file not exist
#ifdef ESP32
        rolling_code = nvRam->read(nvram_rolling);
#else
        rolling_code = read_int_from_file(nvram_rolling, 0);
#endif
        // last saved rolling code may be behind what the GDO thinks, so bump it up so that it will
        // always be ahead of what the GDO thinks it should be, and save it.
        rolling_code = (rolling_code != 0) ? rolling_code + MAX_CODES_WITHOUT_FLASH_WRITE : 0;
        save_rolling_code();
        ESP_LOGI(TAG, "rolling code %lu (0x%02X)", rolling_code, rolling_code);
        sync();

        // Get the initial state of the door
        if (!digitalRead(UART_RX_PIN))
        {
            send_get_status();
        }
        force_recover.push_count = 0;
    }
    else
    {
        ESP_LOGI(TAG, "=== Setting up comms for dry contact protocol");
        pinMode(UART_TX_PIN, OUTPUT);
    }
    IRAM_END(TAG);
#else // !USE_GDOLIB
    esp_err_t err = ESP_OK;

    if ((doorControlType == 1) || (doorControlType == 2))
    {
        gdo_config_t gdo_conf = {
            .uart_num = UART_NUM_1,
            .obst_from_status = userConfig->getObstFromStatus(),
            .invert_uart = true,
            .uart_tx_pin = UART_TX_PIN,
            .uart_rx_pin = UART_RX_PIN,
            .obst_in_pin = INPUT_OBST_PIN,
            .obst_tp_pin = GPIO_NUM_0,  // only used for testing obstruction sensor
            .dc_open_pin = GPIO_NUM_0,  // disable dry-contact
            .dc_close_pin = GPIO_NUM_0, // disable dry-contact
            .dc_discrete_open_pin = GPIO_NUM_0,
            .dc_discrete_close_pin = GPIO_NUM_0,
            .use_sw_serial = userConfig->getUseSWserial(),
        };
        if (userConfig->getDCOpenClose())
        {
            // Enable dry-contact (to trigger door open/close)
            gdo_conf.dc_open_pin = DRY_CONTACT_OPEN_PIN;
            gdo_conf.dc_close_pin = DRY_CONTACT_CLOSE_PIN;
        }
        if ((err = gdo_init(&gdo_conf)) != ESP_OK)
        {
            ESP_LOGE(TAG, "gdo_init failed with error: %d", err);
            return;
        }
        // read from flash, default of 0 if file not exist
        uint32_t id_code = nvRam->read(nvram_id_code);
        uint32_t rolling_code = nvRam->read(nvram_rolling);
        if (!id_code || !rolling_code)
        {
            ESP_LOGI(TAG, "generate new id code");
            id_code = (random(0x1, 0xFFF) << 12) | 0x539;
            nvRam->write(nvram_id_code, id_code);
        }
        ESP_LOGI(TAG, "id code %lu (0x%02lX)", id_code, id_code);

        // last saved rolling code may be behind what the GDO thinks, so bump it up so that it will
        // always be ahead of what the GDO thinks it should be, and save it.
        rolling_code = (rolling_code != 0) ? rolling_code + MAX_CODES_WITHOUT_FLASH_WRITE : 0;
        ESP_LOGI(TAG, "rolling code %lu (0x%02X)", rolling_code, rolling_code);
        if (doorControlType == 2)
        {
            if ((err = gdo_set_protocol(GDO_PROTOCOL_SEC_PLUS_V2)) != ESP_OK)
            {
                ESP_LOGE(TAG, "gdo_set_protocol failed with error: %d", err);
                return;
            }
            gdo_set_client_id(id_code);
            gdo_set_rolling_code(rolling_code);
            save_rolling_code();
        }
        else
        {
            if ((err = gdo_set_protocol(GDO_PROTOCOL_SEC_PLUS_V1)) != ESP_OK)
            {
                ESP_LOGE(TAG, "gdo_set_protocol failed with error: %d", err);
                return;
            }
        }
        if ((err = gdo_start(gdo_event_handler, NULL)) != ESP_OK)
        {
            ESP_LOGE(TAG, "gdo_start failed with error: %d", err);
            return;
        }
        gdo_get_status(&gdo_status);
        force_recover.push_count = 0;
    }
    else
    {
        // door control is dry contact
        gdo_config_t gdo_conf = {
            .uart_num = UART_NUM_1, // not used for dry contact
            .obst_from_status = false,
            .invert_uart = true, // not used for dry contact
            .uart_tx_pin = UART_TX_PIN,
            .uart_rx_pin = UART_RX_PIN, // not used for dry contact
            .obst_in_pin = INPUT_OBST_PIN,
            .obst_tp_pin = GPIO_NUM_0, // only used for testing obstruction sensor
            .dc_open_pin = DRY_CONTACT_OPEN_PIN,
            .dc_close_pin = DRY_CONTACT_CLOSE_PIN,
            .dc_discrete_open_pin = DISCRETE_OPEN_PIN,
            .dc_discrete_close_pin = DISCRETE_CLOSE_PIN,
            .dc_debounce_ms = (uint32_t)userConfig->getDCDebounceDuration(),
        };
        if ((err = gdo_set_protocol(GDO_PROTOCOL_DRY_CONTACT)) != ESP_OK)
        {
            ESP_LOGE(TAG, "gdo_set_protocol failed with error: %d", err);
            return;
        }
        // gdo_set_obst_test_pulse_timer(10000, true); // only used for testing obstruction sensor
        if ((err = gdo_init(&gdo_conf)) != ESP_OK)
        {
            ESP_LOGE(TAG, "gdo_init failed with error: %d", err);
            return;
        }
        if ((err = gdo_start(gdo_event_handler, NULL)) != ESP_OK)
        {
            ESP_LOGE(TAG, "gdo_start failed with error: %d", err);
            return;
        }
        gdo_get_status(&gdo_status);
        force_recover.push_count = 0;
    }
#endif

#ifndef USE_GDOLIB
    if (!(get_obstruction_from_status = userConfig->getObstFromStatus()))
    {
        // pin-based obstruction detection attempted only if user not requested to get from status
        ESP_LOGI(TAG, "Initialize for pin-based obstruction detection");
        pinMode(INPUT_OBST_PIN, INPUT);
        pinMode(STATUS_OBST_PIN, OUTPUT);
        // FALLING from https://github.com/ratgdo/esphome-ratgdo/blob/e248c705c5342e99201de272cb3e6dc0607a0f84/components/ratgdo/ratgdo.cpp#L54C14-L54C14
        attachInterrupt(INPUT_OBST_PIN, isr_obstruction, FALLING);
    }
    else
    {
        ESP_LOGI(TAG, "Use status messages for obstruction detection");
    }
#endif
    comms_setup_done = true;
    comms_status_start = _millis();
}

/****************************************************************************
 * Helper functions for GDO communications.
 */
void save_rolling_code()
{
    if (doorControlType != 2)
        return;
    // Prevent concurrent rolling code operations
    if (rolling_code_operation_in_progress)
        return;
    rolling_code_operation_in_progress = true;

#ifdef USE_GDOLIB
    if (gdo_status.rolling_code != 0)
        gdo_get_status(&gdo_status); // get most recent rolling code if we are not resetting it.
    ESP_LOGI(TAG, "Save rolling code: %d", gdo_status.rolling_code);
    nvRam->write(nvram_rolling, gdo_status.rolling_code);
    last_saved_code = gdo_status.rolling_code;
#else // !USE_GDOLIB
#ifdef ESP32
    nvRam->write(nvram_rolling, rolling_code);
#else
    write_int_to_file(nvram_rolling, rolling_code);
#endif
    last_saved_code = rolling_code;
#endif // !USE_GDOLIB
    rolling_code_operation_in_progress = false;
}

void reset_door()
{
#ifdef USE_GDOLIB
    gdo_status.rolling_code = 0; // because sync_and_reboot writes this.
#else
    rolling_code = 0; // because sync_and_reboot writes this.
#endif
#ifdef ESP32
    nvRam->erase(nvram_rolling);
    nvRam->erase(nvram_id_code);
    nvRam->erase(nvram_has_motion);
#else
    delete_file(nvram_rolling);
    delete_file(nvram_id_code);
    delete_file(nvram_has_motion);
#endif
}
#ifndef USE_GDOLIB
/****************************************************************************
 * Sec+ 1.0 loop functions.
 */
void wallPlate_Emulation()
{
    if (wallPanelDetected)
        return;

    _millis_t currentMillis = _millis();
    static _millis_t lastRequestMillis = 0;
    static _millis_t startMillis = currentMillis;
    static bool emulateWallPanel = false;
    static uint8_t stateIndex = 0;

    // wait up to 15 seconds to look for an existing wallplate or it could be booting, so need to wait
    if (currentMillis - startMillis < SECPLUS1_DIGITAL_WALLPLATE_TIMEOUT || wallPanelBooting == true)
    {
        if (currentMillis - lastRequestMillis > 1000)
        {
            ESP_LOGI(TAG, "Looking for security+ 1.0 DIGITAL wall panel...");
            lastRequestMillis = currentMillis;
        }

        if (!wallPanelDetected && (doorState != GarageDoorCurrentState::UNKNOWN || lightState != 2))
        {
            wallPanelDetected = true;
            wallPanelBooting = false;
            ESP_LOGI(TAG, "DIGITAL Wall panel detected.");
            return;
        }
    }
    else
    {
        if (!emulateWallPanel && !wallPanelDetected)
        {
            emulateWallPanel = true;
            ESP_LOGI(TAG, "No DIGITAL wall panel detected. Switching to emulation mode.");
        }

        // transmit every 250ms
        if (emulateWallPanel && (currentMillis - lastRequestMillis) > 250)
        {
            lastRequestMillis = currentMillis;

            byte secplus1ToSend = byte(secplus1States[stateIndex]);

            // send through queue
            PacketData data;
            data.type = PacketDataType::Status;
            data.value.cmd = secplus1ToSend;
            Packet pkt = Packet(PacketCommand::GetStatus, data, id_code);
            PacketAction pkt_ac = {pkt, true};
#ifdef ESP32
            if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#else
            if (!q_push(&pkt_q, &pkt_ac))
#endif
            {
                ESP_LOGE(TAG, "packet queue full, dropping panel emulation status pkt");
            }

            stateIndex++;
            if (stateIndex == sizeof(secplus1States))
            {
                stateIndex = sizeof(secplus1States) - 3;
            }
        }
    }
}

void update_door_state(GarageDoorCurrentState current_state)
{
    static _millis_t start_opening = 0;
    static _millis_t start_closing = 0;
    static _millis_t open_average = 0;
    static int32_t open_counter = 0;
    static _millis_t close_average = 0;
    static int32_t close_counter = 0;
#define FACTOR 5 // Number of door operations to average across.
    GarageDoorTargetState target_state = garage_door.target_state;

    // Determine target state
    switch (current_state)
    {
    case GarageDoorCurrentState::CURR_OPEN:
        target_state = TGT_OPEN;
        break;
    case GarageDoorCurrentState::CURR_CLOSED:
        target_state = TGT_CLOSED;
        break;
    case GarageDoorCurrentState::CURR_STOPPED:
        target_state = TGT_OPEN;
        break;
    case GarageDoorCurrentState::CURR_OPENING:
        target_state = TGT_OPEN;
        break;
    case GarageDoorCurrentState::CURR_CLOSING:
        target_state = TGT_CLOSED;
        break;
    case GarageDoorCurrentState::UNKNOWN:
        ESP_LOGE(TAG, "Got door state unknown");
        break;
    }

    // Calculate door open/close duration
    if (current_state == CURR_OPENING && garage_door.current_state == CURR_CLOSED)
    {
        start_opening = _millis();
        ESP_LOGD(TAG, "Record start time of door opening: %llums", (uint64_t)start_opening);
    }
    else if (current_state == CURR_OPEN && garage_door.current_state == CURR_OPENING && start_opening > 0)
    {
        _millis_t open_duration = _millis() - start_opening;
        open_counter++;
        open_average += ((int32_t)open_duration - (int32_t)open_average) / std::min(open_counter, FACTOR);
        garage_door.openDuration = (open_average + 500) / 1000; // round up/down to closest second
        ESP_LOGI(TAG, "Door open duration: %lums, average: %lums", (uint32_t)open_duration, (uint32_t)open_average);
    }
    else if (current_state == CURR_CLOSING && garage_door.current_state == CURR_OPEN)
    {
        start_closing = _millis();
        ESP_LOGD(TAG, "Record start time of door closing: %llums", (uint64_t)start_closing);
    }
    else if (current_state == CURR_CLOSED && garage_door.current_state == CURR_CLOSING && start_closing > 0)
    {
        _millis_t close_duration = _millis() - start_opening;
        close_counter++;
        close_average += ((int32_t)close_duration - (int32_t)close_average) / std::min(close_counter, FACTOR);
        garage_door.closeDuration = (close_average + 500) / 1000; // round up/down to closest second
        ESP_LOGI(TAG, "Door close duration: %lums, average: %lums", (uint32_t)close_duration, (uint32_t)close_average);
    }
    else if (current_state == CURR_STOPPED)
    {
        // If door is stopped (neither fully open or fully closed) then abort measuring duration
        start_opening = 0;
        start_closing = 0;
    }

    // If we are in a time-to-close delay timeout, cancel the timeout
    if ((current_state == CURR_CLOSING) && (TTCtimer.active()))
    {
        ESP_LOGI(TAG, "Canceling TTC delay timer");
        TTCtimer.detach();
    }

    // First time initialization
    if (!garage_door.active)
    {
        garage_door.active = true;
        if (current_state == CURR_OPENING || current_state == CURR_OPEN)
        {
            target_state = TGT_OPEN;
        }
        else
        {
            target_state = TGT_CLOSED;
        }
        // retrieve number of door open/close cycles.
        send_get_openings();
    }
    else if (current_state == CURR_CLOSED && (current_state != garage_door.current_state))
    {
        // door activated, retrieve number of door open/close cycles.
        send_get_openings();
    }

    // Inform HomeKit if there is a change in door state.
    if ((target_state != garage_door.target_state) ||
        (current_state != garage_door.current_state))
    {
        ESP_LOGI(TAG, "Door target: %d, current: %d", target_state, current_state);
        notify_homekit_current_door_state_change(current_state);
        notify_homekit_target_door_state_change(target_state);
    }

    // Update the global
    doorState = current_state;
}

void sec1_process_message(uint8_t key, uint8_t value)
{
    static _millis_t lastTime = 0;

    _millis_t now = _millis();
    // time between messages
    _millis_t since = _millis() - lastTime;

#ifdef SEC1_EXTENDED_COMMS_DEBUG
    ESP_LOGD(TAG, "SEC1 RX IDLE:%lums - MSG: 0x%02X:0x%02X", since, key, value);
#endif

    lastTime = now;

    switch (key)
    {
    // door button press
    case secplus1Codes::DoorButtonPress:
    {
        ESP_LOGI(TAG, "SEC1 RX 0x30 (door press)");

        manual_recovery();

        if (motionTriggers.bit.doorKey)
        {
            notify_homekit_motion(true);
        }

        break;
    }

    // door button release
    case secplus1Codes::DoorButtonRelease:
    {
        // wall panel is sending out 0x31 (Door Button Release) when it starts up
        // but also on release of door button

        ESP_LOGI(TAG, "SEC1 RX 0x31 (door release)");

        // Possible power up of 889LM
        if (doorState == GarageDoorCurrentState::UNKNOWN)
        {
            wallPanelBooting = true;
        }

        break;
    }

    // light button press
    case secplus1Codes::LightButtonPress:
    {
        ESP_LOGI(TAG, "SEC1 RX 0x32 (light press)");

        manual_recovery();

        break;
    }

    // light button release
    case secplus1Codes::LightButtonRelease:
    {
        ESP_LOGI(TAG, "SEC1 RX 0x33 (light release)");

        break;
    }

    // door status
    case secplus1Codes::DoorStatus:
    {
        // 0x5X = stopped
        // 0x0X = moving
        // best attempt to trap invalid values (due to collisions)
        if (((value & 0xF0) != 0x00) && ((value & 0xF0) != 0x50) && ((value & 0xF0) != 0xB0))
        {
            ESP_LOGI(TAG, "SEC1 RX DoorStatus(0x38) \"value\" upper nibble not 0x0 or 0x5 or 0xB, received: 0x%02X", value);
            break;
        }

        // sec+1 doors sometimes report wrong door status
        // back to origional code, MJS 8/14/2025 confirmed logging
        // it could report a valid byte but its not really valid
        // ie: opening when its already open
        static uint8_t prevDoor = 0xFF; // Initialize to invalid value
        if (prevDoor != value)
        {
            prevDoor = value;
            break;
        }

        // mask off door status bits
        value = (value & 0x7);
        // 000 0x0 stopped
        // 001 0x1 opening
        // 010 0x2 open
        // 100 0x4 closing
        // 101 0x5 closed
        // 110 0x6 stopped

        GarageDoorCurrentState current_state = garage_door.current_state;
        switch (value)
        {
        case 0x00:
            if (garage_door.current_state == CURR_CLOSED || garage_door.current_state == CURR_OPEN)
            {
                ESP_LOGI(TAG, "Ignoring invalid door state change from %s to STOPPED (0x00)", (garage_door.current_state == CURR_CLOSED) ? "CLOSED" : "OPEN");
                break;
            }
            current_state = GarageDoorCurrentState::CURR_STOPPED;
            break;
        case 0x01:
            if (garage_door.current_state == CURR_OPEN)
            {
                ESP_LOGI(TAG, "Ignoring invalid door state change from OPEN to OPENING");
                break;
            }
            current_state = GarageDoorCurrentState::CURR_OPENING;
            break;
        case 0x02:
            current_state = GarageDoorCurrentState::CURR_OPEN;
            break;
        // no 0x03 known
        case 0x04:
            if (garage_door.current_state == CURR_CLOSED)
            {
                ESP_LOGI(TAG, "Ignoring invalid door state change from CLOSED to CLOSING");
                break;
            }
            current_state = GarageDoorCurrentState::CURR_CLOSING;
            break;
        case 0x05:
            current_state = GarageDoorCurrentState::CURR_CLOSED;
            break;
        case 0x06:
            if (garage_door.current_state == CURR_CLOSED || garage_door.current_state == CURR_OPEN)
            {
                ESP_LOGI(TAG, "Ignoring invalid door state change from %s to STOPPED (0x06)", (garage_door.current_state == CURR_CLOSED) ? "CLOSED" : "OPEN");
                break;
            }
            current_state = GarageDoorCurrentState::CURR_STOPPED;
            break;
        default:
            ESP_LOGE(TAG, "SEC1 RX Got unknown \"value\" for door state");
            current_state = GarageDoorCurrentState::UNKNOWN;
            break;
        }
        update_door_state(current_state);
        break;
    }

    // obstruction states (not confirmed)
    case secplus1Codes::ObstructionStatus:
    {
        // 0x00         No obstruction
        // 0x00 -> 0x04 Obstruction beam broken, implies motion
        // 0x04 -> 0x01 Stable obstruction
        // 0x01 -> 0x04 Obstruction removed, implies motion
        // 0x04 -> 0x00 No obstruction

#ifdef SEC1_EXTENDED_COMMS_DEBUG
        if (value > 0)
            ESP_LOGD(TAG, "SEC1 TX MSG 0x39: value: 0x%02X", value);
#endif

        // Handle obstruction from status packet if pin-based detection not used
        static uint8_t prevObstruction = 0xFF; // Initialize to invalid value
        if (!obstruction_sensor_detected && value != prevObstruction)
        {
            // Reported value has changed
            bool status_obstructed = bitRead(value, 0);
            bool status_motion = bitRead(value, 2);
            if (garage_door.obstructed != status_obstructed)
            {
                // Obstruction state changed
                ESP_LOGI(TAG, "Obstruction %s (Status packet)", status_obstructed ? "Detected" : "Clear");
                notify_homekit_obstruction(status_obstructed);
                digitalWrite(STATUS_OBST_PIN, !status_obstructed);
            }
            if (motionTriggers.bit.obstruction && status_motion)
            {
                // User want to trigger motion sensor based on obstruction beam
                notify_homekit_motion(true);
            }
            prevObstruction = value;
        }
        break;
    }

    // light & lock
    case secplus1Codes::LightLockStatus:
    {
        // only use for real sec1 commm debugging, its just too chatty
        // ESP_LOGI(TAG, "SEC1 RX 0x3A value: 0x%02X", value);

        // upper nibble must be 0x5 or 0x1
        if (((value & 0xF0) != 0x50) && ((value & 0xF0) != 0x10))
        {
            ESP_LOGI(TAG, "SEC1 RX LightLockStatus(0x3A) \"value\" upper nibble not 0x5 or 0x1, received: 0x%02X", value);
            break;
        }

        // make sure 2 same in a row
        // MJS 8/14/2025 during logging observed this situation
        static uint8_t prevLightLock = 0xFF; // Initialize to invalid value
        if (value != prevLightLock)
        {
            prevLightLock = value;
            break;
        }

        lightState = bitRead(value, 2);
        lockState = !bitRead(value, 3);

        // light status
        static uint8_t lastLightState = 0xff;
        // light state change?
        if (lightState != lastLightState)
        {
            ESP_LOGI(TAG, "status LIGHT: %s", lightState ? "On" : "Off");
            lastLightState = lightState;
            notify_homekit_light((bool)lightState);
            if (motionTriggers.bit.lightKey)
            {
                notify_homekit_motion(true);
            }
        }

        // lock status
        static uint8_t lastLockState = 0xff;
        // lock state change?
        if (lockState != lastLockState)
        {
            ESP_LOGI(TAG, "status LOCK: %s", lockState ? "Secured" : "Unsecured");
            lastLockState = lockState;

            if (lockState)
            {
                garage_door.current_lock = CURR_LOCKED;
                garage_door.target_lock = TGT_LOCKED;
            }
            else
            {
                garage_door.current_lock = CURR_UNLOCKED;
                garage_door.target_lock = TGT_UNLOCKED;
            }
            notify_homekit_target_lock(garage_door.target_lock);
            notify_homekit_current_lock(garage_door.current_lock);
            if (motionTriggers.bit.lockKey)
            {
                notify_homekit_motion(true);
            }
        }

        break;
    }
    }
}

void comms_loop_sec1()
{
    static bool reading_msg = false;
    static uint32_t byte_count = 0;
    static RxPacket rx_packet;
    bool gotMessage = false;

    // CTS timer
    // when wall panel present, need 20ms elasped after last complete message arrives.
    // if one arrives before that (ie multiple in rx buffers, the cts_signal is reset)
    if (!clearToSend)
    {
        if ((_millis() - cts_signal) > SECPLUS1_TX_WINDOW)
        {
            clearToSend = true;
        }
    }

    // get all the rxed bytes processed now
    // any rx bytes will reset clearToSend
    while (sw_serial.available())
    {
        uint8_t ser_byte = sw_serial.read();

        clearToSend = false;

        if (reading_msg == false)
        {
            if (ser_byte == 0xFF)
            {
#ifdef SEC1_EXTENDED_COMMS_DEBUG
                ESP_LOGD(TAG, "SEC1 RX GDO sync byte(0xFF) received");
#endif
                // count them, if say 10, start emulator? (future idea)
                byte_count++;
                if (byte_count == 10)
                {
                    byte_count = 0;

                    ESP_LOGD(TAG, "SEC1 RX GDO sync bytes(0xFF)");
                }
            }
            // valid?
            else if (ser_byte >= 0x30 && ser_byte <= 0x3A)
            {
                byte_count = 1;
                rx_packet[0] = ser_byte;

                reading_msg = true;

                // timestamp beinging of message
                msg_start = _millis();

                // is it single byte command? (PRESS/RELEASE FROM WALL PLATE)
                if (ser_byte >= 0x30 && ser_byte <= 0x37)
                {
                    reading_msg = false;
                    gotMessage = true;
                    sec1_process_message(rx_packet[0], rx_packet[1]);
                }
            }
            else
            {
                ESP_LOGD(TAG, "SEC1 RX invalid cmd byte 0x%02X", ser_byte);
            }
        }
        else
        {
            // we only allow 2 bytes max, and the reading_msg controls that

            // this is the value to response of the GDO query
            byte_count = 2;
            rx_packet[1] = ser_byte;

            gotMessage = true;
            sec1_process_message(rx_packet[0], rx_packet[1]);

            // reset cts signal, after 10ms ok to send
            cts_signal = _millis();
        }

        if (gotMessage == true)
        {
            gotMessage = false;

            // reset start of message
            reading_msg = false;
            byte_count = 0;
        }
    }

    // incomplete message timeout?
    if (reading_msg == true && gotMessage == false && ((_millis() - msg_start) > SECPLUS1_RX_MESSAGE_TIMEOUT))
    {
        ESP_LOGE(TAG, "SEC1 RX message timeout, 1 byte of 2 byte message received [rx_packet[0]=0x%02X]", rx_packet[0]);

        // if we have a partial packet and it's been over 15ms since last byte was read,
        // the rest is not coming (a full packet should be received in ~10ms),
        // discard it so we can read the following packet correctly
        // so how did we lose it
        //
        // ⁡⁢⁣⁡⁢⁣⁢MJS-8/12/2025 - measued time 9-10ms for 2byte messages⁡
        reading_msg = false;
        byte_count = 0;
    }

    // if still reading the message in, no need to process further
    // as its not a good time to TX, and next byte is expected within 10ms
    // check if a byte became available, exit to process (on next comm_loop())
    if (reading_msg == true || sw_serial.available())
    {
        return;
    }

    // PROCESS TRANSMIT QUEUE
    //
    PacketAction pkt_ac;
    // static uint32_t cmdDelay = 0;
    _millis_t now;
    static uint32_t retryCount = 0;
    bool okToSend = false;
    uint32_t msgs;

#ifdef ESP32
    if ((msgs = uxQueueMessagesWaiting(pkt_q)) > 0)
#else
    if ((msgs = (uint32_t)q_getCount(&pkt_q)) > 0)
#endif
    {
        okToSend = false;
        if ((_millis() - last_tx) > SECPLUS1_TX_MINIMUM_DELAY)
        {
            okToSend = true;
        }

        // if there is a wall panel, need to make sure the clear to send timing is met
        if (wallPanelDetected)
        {
            okToSend &= clearToSend;
        }

        // meets our timing requirements
        if (okToSend)
        {
#ifdef ESP8266
            if (q_peek(&pkt_q, &pkt_ac))
            {
#else
            xQueueReceive(pkt_q, &pkt_ac, 0); // ignore errors
#endif
                if (process_PacketAction(pkt_ac))
                {

                    // get next delay "between" transmits (currently not using)
                    // cmdDelay = pkt_ac.delay;
#ifdef ESP8266
                    q_drop(&pkt_q);
#endif
                }
                else
                {
                    // cmdDelay = 0;
                    if (retryCount++ < MAX_COMMS_RETRY)
                    {
                        ESP_LOGD(TAG, "SEC1 TX send failed, will retry");
#ifdef ESP32
                        xQueueSendToFront(pkt_q, &pkt_ac, 0); // ignore errors
#endif
                        // get out now
                        return;
                    }
                    else
                    {
                        ESP_LOGE(TAG, "SEC1 TX send failed, exceeded max retry");
                        retryCount = 0;
#ifdef ESP8266
                        q_drop(&pkt_q);
#endif
                    }
                }
#ifdef ESP8266
            }
#endif
        }
    }
    // check for wall panel and provide emulator
    wallPlate_Emulation();
}

/****************************************************************************
 * Sec+ 2.0 loop functions.
 */
void comms_loop_sec2()
{
    static uint32_t retryCount = 0;

    if (!sw_serial.available())
    {
        // no incoming data, check if we have command queued
        PacketAction pkt_ac;
        uint32_t msgs;

#ifdef ESP8266
        while ((msgs = (uint32_t)q_peek(&pkt_q, &pkt_ac)) > 0)
#else
        while ((msgs = uxQueueMessagesWaiting(pkt_q)) > 0)
#endif
        {
            // Two packets in the queue is normal (e.g. set light followed by get status)
            // but more than that may indicate a problem
            if (msgs > 2)
                ESP_LOGW(TAG, "WARNING: message packets in queue is > 2 (%lu)", msgs);

#ifdef ESP32
            xQueueReceive(pkt_q, &pkt_ac, 0); // ignore errors
#endif
            if (!process_PacketAction(pkt_ac))
            {
                if (retryCount++ < MAX_COMMS_RETRY)
                {
                    ESP_LOGD(TAG, "Transmit failed, will retry");
#ifdef ESP32
                    xQueueSendToFront(pkt_q, &pkt_ac, 0); // ignore errors
#endif
                }
                else
                {
                    ESP_LOGE(TAG, "Transmit failed, exceeded max retry");
                    retryCount = 0;
#ifdef ESP8266
                    q_drop(&pkt_q);
#endif
                }
            }
#ifdef ESP8266
            else
            {
                q_drop(&pkt_q);
            }
#endif
            // If we are looping over multiple packets, yield on each loop
            if (msgs > 1)
                YIELD();
        }
    }
    else
    {
        // spin on receiving data until the whole packet has arrived
        uint8_t ser_data = sw_serial.read();
        if (reader.push_byte(ser_data))
        {
            Packet pkt = Packet(reader.fetch_buf());
            pkt.print();

            switch (pkt.m_pkt_cmd)
            {
            case PacketCommand::Status:
            {
                GarageDoorCurrentState current_state = garage_door.current_state;
                switch (pkt.m_data.value.status.door)
                {
                case DoorState::Open:
                    current_state = GarageDoorCurrentState::CURR_OPEN;
                    break;
                case DoorState::Closed:
                    current_state = GarageDoorCurrentState::CURR_CLOSED;
                    break;
                case DoorState::Stopped:
                    current_state = GarageDoorCurrentState::CURR_STOPPED;
                    break;
                case DoorState::Opening:
                    current_state = GarageDoorCurrentState::CURR_OPENING;
                    break;
                case DoorState::Closing:
                    current_state = GarageDoorCurrentState::CURR_CLOSING;
                    break;
                default:
                    ESP_LOGE(TAG, "Got unknown door state");
                    current_state = GarageDoorCurrentState::UNKNOWN;
                    break;
                }
                update_door_state(current_state);

                if (pkt.m_data.value.status.light != garage_door.light)
                {
                    ESP_LOGI(TAG, "Light Status %s", pkt.m_data.value.status.light ? "On" : "Off");
                    notify_homekit_light(pkt.m_data.value.status.light);
                }

                LockCurrentState current_lock;
                LockTargetState target_lock;
                if (pkt.m_data.value.status.lock)
                {
                    current_lock = CURR_LOCKED;
                    target_lock = TGT_LOCKED;
                }
                else
                {
                    current_lock = CURR_UNLOCKED;
                    target_lock = TGT_UNLOCKED;
                }
                if (current_lock != garage_door.current_lock)
                {
                    notify_homekit_target_lock(target_lock);
                    notify_homekit_current_lock(current_lock);
                }

                // Handle obstruction from status packet if pin-based detection not available
                if (!obstruction_sensor_detected)
                {
                    // Status packet obstruction field is inverted: 1=clear, 0=obstructed
                    bool status_obstructed = !pkt.m_data.value.status.obstruction;
                    if (garage_door.obstructed != status_obstructed)
                    {
                        ESP_LOGI(TAG, "Obstruction %s (Status packet)", status_obstructed ? "Detected" : "Clear");
                        notify_homekit_obstruction(status_obstructed);
                        digitalWrite(STATUS_OBST_PIN, !status_obstructed);
                        if (motionTriggers.bit.obstruction)
                        {
                            notify_homekit_motion(status_obstructed);
                        }
                    }
                }

                comms_status_done = true;
                break;
            }

            case PacketCommand::Lock:
            {
                LockTargetState lock = garage_door.target_lock;
                switch (pkt.m_data.value.lock.lock)
                {
                case LockState::Off:
                    lock = TGT_UNLOCKED;
                    break;
                case LockState::On:
                    lock = TGT_LOCKED;
                    break;
                case LockState::Toggle:
                    if (lock == TGT_LOCKED)
                    {
                        lock = TGT_UNLOCKED;
                    }
                    else
                    {
                        lock = TGT_LOCKED;
                    }
                    break;
                }
                if (lock != garage_door.target_lock)
                {
                    ESP_LOGI(TAG, "Lock Cmd %d", lock);
                    notify_homekit_target_lock(lock);
                    if (motionTriggers.bit.lockKey)
                    {
                        notify_homekit_motion(true);
                    }
                }
                // Send a get status to make sure we are in sync
                send_get_status();
                break;
            }

            case PacketCommand::Light:
            {
                bool l = garage_door.light;
                manual_recovery();
                switch (pkt.m_data.value.light.light)
                {
                case LightState::Off:
                    l = false;
                    break;
                case LightState::On:
                    l = true;
                    break;
                case LightState::Toggle:
                case LightState::Toggle2:
                    l = !garage_door.light;
                    break;
                }
                if (l != garage_door.light)
                {
                    ESP_LOGI(TAG, "Light Cmd %s", l ? "On" : "Off");
                    notify_homekit_light(l);
                    if (motionTriggers.bit.lightKey)
                    {
                        notify_homekit_motion(true);
                    }
                }
                // Send a get status to make sure we are in sync
                // Should really only need to do this on a toggle,
                // But safer to do it always
                send_get_status();
                break;
            }

            case PacketCommand::Motion:
            {
                ESP_LOGI(TAG, "Motion Detected");
                // We got a motion message, so we know we have a motion sensor
                // If it's not yet enabled, add the service
                if (!garage_door.has_motion_sensor)
                {
                    ESP_LOGI(TAG, "Detected new Motion Sensor. Enabling Service");
                    garage_door.has_motion_sensor = true;
                    motionTriggers.bit.motion = 1;
                    userConfig->set(cfg_motionTriggers, motionTriggers.asInt);
                    ESP8266_SAVE_CONFIG();
#ifdef ESP8266
                    enable_service_homekit_motion(true);
#else
                    enable_service_homekit_motion(false); // ESP32 with HomeSpan can do this without reboot
#endif
                }

                // When we get the motion detect message, notify HomeKit. Motion sensor
                if (!garage_door.motion)
                {
                    notify_homekit_motion(true);
                }
                // Update status because things like light may have changed states
                send_get_status();
                break;
            }

            case PacketCommand::DoorAction:
            {
                ESP_LOGI(TAG, "Door Action");
                if (pkt.m_data.value.door_action.pressed)
                {
                    manual_recovery();
                }
                if (pkt.m_data.value.door_action.pressed && motionTriggers.bit.doorKey)
                {
                    notify_homekit_motion(true);
                }
                break;
            }

            case PacketCommand::Battery:
            {
                garage_door.batteryState = (uint8_t)pkt.m_data.value.battery.state;
                break;
            }

            case PacketCommand::Openings:
            {
                if (pkt.m_data.value.openings.flags == 0)
                {
                    // Apparently flags must be zero... to indicate a reply to our request
                    garage_door.openingsCount = pkt.m_data.value.openings.count;
                }
                break;
            }

            case PacketCommand::GetStatus:
            case PacketCommand::GetOpenings:
            case PacketCommand::Unknown:
            {
                // Silently ignore, because we see lots of these and they have no data, and Packet.h logged them.
                break;
            }
            case PacketCommand::Pair3Resp:
            {
                // Only use Pair3Resp for obstruction detection if no sensor detected
                if (!obstruction_sensor_detected)
                {
                    // Use Pair3Resp packets for obstruction detection via parity
                    // Parity 3 = clear, Parity 4 = obstructed
                    uint8_t parity = pkt.m_data.value.no_data.parity;
                    bool currently_obstructed = (parity == 4);

                    // Only update if obstruction state has changed
                    if (garage_door.obstructed != currently_obstructed)
                    {
                        ESP_LOGI(TAG, "Obstruction %s (Pair3Resp parity %d)", currently_obstructed ? "Detected" : "Clear", parity);
                        // Notify HomeKit of the state change
                        notify_homekit_obstruction(currently_obstructed);
                        digitalWrite(STATUS_OBST_PIN, !currently_obstructed);
                        // Trigger motion detection if enabled
                        if (motionTriggers.bit.obstruction)
                        {
                            notify_homekit_motion(currently_obstructed);
                        }
                    }
                }
                break;
            }

            default:
                // Log if we get a command that we do not recognize.
                ESP_LOGI(TAG, "Support for %s packet unimplemented. Ignoring.", PacketCommand::to_string(pkt.m_pkt_cmd));
                break;
            }
        }
    }

    // Only check if no rolling code operation is in progress to prevent race conditions
    if (!rolling_code_operation_in_progress && rolling_code >= (last_saved_code + MAX_CODES_WITHOUT_FLASH_WRITE))

    {
        save_rolling_code();
    }
}

void comms_loop_drycontact()
{
    static GarageDoorCurrentState previousDoorState = GarageDoorCurrentState::UNKNOWN;

    // Notify HomeKit when the door state changes
    if (doorState != previousDoorState)
    {
        switch (doorState)
        {
        case GarageDoorCurrentState::CURR_OPEN:
            garage_door.current_state = GarageDoorCurrentState::CURR_OPEN;
            garage_door.target_state = GarageDoorTargetState::TGT_OPEN;
            break;
        case GarageDoorCurrentState::CURR_CLOSED:
            garage_door.current_state = GarageDoorCurrentState::CURR_CLOSED;
            garage_door.target_state = GarageDoorTargetState::TGT_CLOSED;
            break;
        case GarageDoorCurrentState::CURR_OPENING:
            garage_door.current_state = GarageDoorCurrentState::CURR_OPENING;
            garage_door.target_state = GarageDoorTargetState::TGT_OPEN;
            break;
        case GarageDoorCurrentState::CURR_CLOSING:
            garage_door.current_state = GarageDoorCurrentState::CURR_CLOSING;
            garage_door.target_state = GarageDoorTargetState::TGT_CLOSED;
            break;
        default:
            garage_door.current_state = GarageDoorCurrentState::CURR_STOPPED;
            break;
        }

        notify_homekit_current_door_state_change(garage_door.current_state);
        notify_homekit_target_door_state_change(garage_door.target_state);

        previousDoorState = doorState;

        // Log the state change for debugging
        ESP_LOGI(TAG, "Door state updated: Current: %d, Target: %d", garage_door.current_state, garage_door.target_state);
    }
}
#endif
void comms_loop()
{
    if (!comms_setup_done)
        return;

    // wait for a status command to be processes to properly set the initial state of
    // all homekit characteristics.  Also timeout if we don't receive a status in
    // a reasonable amount of time.  This prevents unintentional state changes if
    // a home hub reads the state before we initialize everything
    // Note, secplus1 doesnt have a status command so it will just timeout
    if (!comms_status_done && (_millis() - comms_status_start > COMMS_STATUS_TIMEOUT))
    {
        ESP_LOGI(TAG, "Comms initial status timeout");
        comms_status_done = true;
    }

#ifdef USE_GDOLIB
    // Room Occupancy Clear Timer
    if (garage_door.room_occupied && (_millis() > garage_door.room_occupancy_timeout))
    {
        notify_homekit_room_occupancy(false);
        ESP_LOGI(TAG, "Room occupancy cleared after %d minutes", userConfig->getOccupancyDuration() / 60);
    }

    // Motion Clear Timer
    if (garage_door.motion && (_millis() > garage_door.motion_timer))
    {
        notify_homekit_motion(false);
        ESP_LOGI(TAG, "Motion Cleared after %d seconds", MOTION_TIMER_DURATION / 1000);
    }
#else  // not USE_GDOLIB
    if (doorControlType == 1)
        comms_loop_sec1();
    else if (doorControlType == 2)
        comms_loop_sec2();
    else
        comms_loop_drycontact();

    // Motion Clear Timer
    if (garage_door.motion && garage_door.motion_timer > 0 && (int32_t)(_millis() - garage_door.motion_timer) >= 0)
    {
        ESP_LOGI(TAG, "Motion Cleared");
        notify_homekit_motion(false);
    }
    // Service the Obstruction Timer
    obstruction_timer();
#endif // USE_GDOLIB
}

#ifndef USE_GDOLIB
/**************************** CONTROLLER CODE *******************************
 * SECURITY+1.0
 */
bool transmitSec1(byte toSend)
{
    bool noSend = false;

    // safety
    if (sw_serial.available())
    {
        ESP_LOGD(TAG, "SEC1 TX incoming data detected, cannot send right now");
        noSend = true;
    }

    if (digitalRead(UART_RX_PIN))
    {
        ESP_LOGD(TAG, "SEC1 TX UART_RX_PIN HIGH detected, cannot send right now");
        noSend = true;
    }

    if (noSend == true)
    {
        clearToSend = false;
        return false;
    }

    // sending a poll?
    bool poll_cmd = (toSend == 0x38) || (toSend == 0x39) || (toSend == 0x3A);
    // if not a poll command (and polls only with wall panel emulation),
    // disable disable rx (allows for cleaner tx, and no echo)
    if (!poll_cmd)
    {
        // Use LED to signal activity
        led.flash(FLASH_MS);
        // disable rx
        sw_serial.enableRx(false);
    }

    // write byte *takes aprox 10ms
    sw_serial.write(toSend);
    sw_serial.flush(); // wait till sent

#ifdef SEC1_EXTENDED_COMMS_DEBUG
    ESP_LOGD(TAG, "SEC1 TX sent: 0x%02X", toSend);
#endif

    // timestamp tx
    last_tx = _millis();

    // re-enable rx
    if (!poll_cmd)
    {
        // enable rx
        sw_serial.enableRx(true);
    }

    return true;
}

/**************************** CONTROLLER CODE *******************************
 * SECURITY+2.0
 */
bool transmitSec2(PacketAction &pkt_ac)
{
    // inverted logic, so this pulls the bus low to assert it
    digitalWrite(UART_TX_PIN, HIGH);
    delayMicroseconds(1300);
    digitalWrite(UART_TX_PIN, LOW);
    delayMicroseconds(130);

    // check to see if anyone else is continuing to assert the bus after we have released it
    if (digitalRead(UART_RX_PIN))
    {
        ESP_LOGI(TAG, "Collision detected, waiting to send packet");
        return false;
    }
    else
    {
        uint8_t buf[SECPLUS2_CODE_LEN];
        if (pkt_ac.pkt.encode(rolling_code, buf) != 0)
        {
            ESP_LOGE(TAG, "Could not encode packet");
            pkt_ac.pkt.print();
        }
        else
        {
            // Use LED to signal activity
            led.flash(FLASH_MS);
            sw_serial.write(buf, SECPLUS2_CODE_LEN);
            delayMicroseconds(100);
        }

        if (pkt_ac.inc_counter)
        {
            // Protect rolling code increment from concurrent access
            if (!rolling_code_operation_in_progress)
            {
                rolling_code = (rolling_code + 1) & 0xfffffff;
            }
        }
    }

    return true;
}

bool process_PacketAction(PacketAction &pkt_ac)
{
    bool success = false;

    if (doorControlType == 1)
    {
        // check which action
        switch (pkt_ac.pkt.m_data.type)
        {
        // using this type for emulation of wall panel
        case PacketDataType::Status:
        {
            // 0x38 || 0x39 || 0x3A
            if (pkt_ac.pkt.m_data.value.cmd)
            {
                success = transmitSec1(pkt_ac.pkt.m_data.value.cmd);
                if (success)
                {
                    // use for SEC1 debug only as its too chatty
#ifdef DEBUG_SECPLUS1_COMMS
                    ESP_LOGI(TAG, "SEC1 TX sent POLL: %02X", pkt_ac.pkt.m_data.value.cmd);
#endif
                }
            }
            break;
        }
        case PacketDataType::DoorAction:
        {
            if (pkt_ac.pkt.m_data.value.door_action.pressed == true)
            {
                // ESP_LOGI(TAG, "SEC1 TX sending DOOR button press");

                success = transmitSec1(secplus1Codes::DoorButtonPress);
                if (success)
                {
                    // ESP_LOGI(TAG, "SEC1 TX sent DOOR button press");
                }
                else
                {
                    ESP_LOGI(TAG, "SEC1 TX failed to send DOOR button press");
                }
            }
            else
            {
                // ESP_LOGI(TAG, "SEC1 TX sending DOOR button release");

                success = transmitSec1(secplus1Codes::DoorButtonRelease);
                if (success)
                {
                    // ESP_LOGI(TAG, "SEC1 TX sent DOOR button release");
                }
                else
                {
                    ESP_LOGI(TAG, "SEC1 TX failed to send DOOR button release");
                }
            }
            break;
        }

        case PacketDataType::Light:
        {
            if (pkt_ac.pkt.m_data.value.light.pressed == true)
            {
                // ESP_LOGI(TAG, "SEC1 TX sending LIGHT button press");

                success = transmitSec1(secplus1Codes::LightButtonPress);
                if (success)
                {
                    // ESP_LOGI(TAG, "SEC1 TX sent LIGHT button press");
                }
                else
                {
                    ESP_LOGI(TAG, "SEC1 TX failed to send LIGHT button press");
                }
            }
            else
            {
                // ESP_LOGI(TAG, "SEC1 TX sending LIGHT button release");

                success = transmitSec1(secplus1Codes::LightButtonRelease);
                if (success)
                {
                    // ESP_LOGI(TAG, "SEC1 TX sent LIGHT button release");
                }
                else
                {
                    ESP_LOGI(TAG, "SEC1 TX failed to send LIGHT button release");
                }
            }
            break;
        }

        case PacketDataType::Lock:
        {
            if (pkt_ac.pkt.m_data.value.lock.pressed == true)
            {
                // ESP_LOGI(TAG, "SEC1 TX sending LOCK button press");

                success = transmitSec1(secplus1Codes::LockButtonPress);
                if (success)
                {
                    // ESP_LOGI(TAG, "SEC1 TX sent LOCK button press");
                }
                else
                {
                    ESP_LOGI(TAG, "SEC1 TX failed to send LOCK button press");
                }
            }
            else
            {
                // ESP_LOGI(TAG, "SEC1 TX sending LOCK button release");

                success = transmitSec1(secplus1Codes::LockButtonRelease);
                if (success)
                {
                    // ESP_LOGI(TAG, "SEC1 TX sent LOCK button release");
                }
                else
                {
                    ESP_LOGI(TAG, "SEC1 TX failed to send LOCK button release");
                }
            }
            break;
        }

        default:
        {
            ESP_LOGE(TAG, "SEC1 TX UNHANDLED pkt_ac.pkt.m_data.type=%d", pkt_ac.pkt.m_data.type);
            break;
        }
        }
    }
    else
    {
        success = transmitSec2(pkt_ac);
    }

    return success;
}

void sync()
{
    // only for SECURITY2.0
    if (doorControlType != 2)
        return;

    // for exposition about this process, see docs/syncing.md
    ESP_LOGI(TAG, "Syncing rolling code counter after reboot...");
    PacketData d;
    d.type = PacketDataType::NoData;
    d.value.no_data = NoData();
    Packet pkt = Packet(PacketCommand::GetOpenings, d, id_code);
    PacketAction pkt_ac = {pkt, true};
    process_PacketAction(pkt_ac);
    delay(100);
    pkt = Packet(PacketCommand::GetStatus, d, id_code);
    pkt_ac.pkt = pkt;
    process_PacketAction(pkt_ac);
    delay(100);
    pkt = Packet(PacketCommand::GetOpenings, d, id_code);
    pkt_ac.pkt = pkt;
    process_PacketAction(pkt_ac);
}

void door_command(DoorAction action)
{
    if (doorControlType != 3)
    {
        // SECURITY1.0/2.0 commands
        PacketData data;
        data.type = PacketDataType::DoorAction;
        data.value.door_action.action = action;
        data.value.door_action.pressed = true;
        data.value.door_action.id = 1;

        Packet pkt = Packet(PacketCommand::DoorAction, data, id_code);
        PacketAction pkt_ac = {pkt, false};
#ifdef ESP8266
        if (!q_push(&pkt_q, &pkt_ac))
#else
        if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
        {
            ESP_LOGE(TAG, "packet queue full, dropping door command pressed pkt");
        }

        // do button release
        pkt_ac.pkt.m_data.value.door_action.pressed = false;
        pkt_ac.inc_counter = true;
#ifdef ESP8266
        if (!q_push(&pkt_q, &pkt_ac))
#else
        if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
        {
            ESP_LOGE(TAG, "packet queue full, dropping door command release pkt");
        }

        // if sec+1.0, repeat the release
        if (doorControlType == 1)
        {
#ifdef ESP8266
            if (!q_push(&pkt_q, &pkt_ac))
#else
            if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
            {
                ESP_LOGE(TAG, "packet queue full, dropping door command release pkt");
            }
        }
        send_get_status();
    }
    else
    {
        // Dry contact commands (only toggle functionality, open/close/toggle/stop -> toggle)
        // Toggle signal
        digitalWrite(UART_TX_PIN, HIGH);
        delay(500);
        digitalWrite(UART_TX_PIN, LOW);
    }
}

#endif // not USE_GDOLIB

void door_command_close()
{
    if (garage_door.current_state == GarageDoorCurrentState::CURR_OPEN && userConfig->getUseToggleToClose())
    {
        ESP_LOGD(TAG, "Close door using TOGGLE");
#ifdef USE_GDOLIB
        gdo_door_toggle();
#else
        door_command(DoorAction::Toggle);
#endif
    }
    else
    {
#ifdef USE_GDOLIB
        gdo_door_close();
#else
        door_command(DoorAction::Close);
#endif
    }
}

GarageDoorCurrentState open_door()
{
    if (TTCtimer.active())
    {
        // We are in a time-to-close delay timeout.
        // Effect of open is to cancel the timeout (leaving door open)
        ESP_LOGI(TAG, "Canceling TTC delay timer");
        TTCtimer.detach();
        // Reset light to state it was at before delay start.
        set_light(TTCwasLightOn);
        return GarageDoorCurrentState::CURR_OPEN;
    }

    // safety
    if (garage_door.current_state == GarageDoorCurrentState::CURR_OPEN)
    {
        ESP_LOGI(TAG, "Door already open; ignored request");
        return GarageDoorCurrentState::CURR_OPEN;
    }

    if (garage_door.current_state == GarageDoorCurrentState::CURR_CLOSING)
    {
        ESP_LOGI(TAG, "Door is closing; do stop");
#ifdef USE_GDOLIB
        gdo_door_stop();
#else
        door_command(DoorAction::Stop);
#endif
        return GarageDoorCurrentState::CURR_STOPPED;
    }
    ESP_LOGI(TAG, "Opening door");
#ifdef USE_GDOLIB
    if (doorControlType == 2 && userConfig->getBuiltInTTC())
        gdo_set_time_to_close(0);

    gdo_door_open();
#else
    door_command(DoorAction::Open);
#endif
    return GarageDoorCurrentState::CURR_OPENING;
}

// Call function after ms milliseconds during which we flash and beep
void delayFnCall(uint32_t ms, void (*callback)())
{
    static const uint32_t interval = 250;
    static uint32_t iterations = 0;

    bool light = userConfig->getTTClight(); // Whether to flash light during delay

    TTCtimer.detach();                 // Terminate existing timer if any
    iterations = ms / interval;        // Number of times to go through loop
    TTCwasLightOn = garage_door.light; // Current state of light
    ESP_LOGI(TAG, "Start function delay timer for %lums (%d iterations)", ms, iterations);
    TTCtimer.attach_ms(interval, [callback, light]()
                       {
                        if (iterations > 0)
                        {
                            if (light && (iterations % 2 == 0))
                            {
                                // If light is on, turn it off.  If off, turn it on.
                                if (doorControlType != 3)
                                {
                                    // dry contact cannot control lights
                                    set_light((iterations % 4) != 0, false);
                                }
                            }
#ifdef ESP32
                            tone(BEEPER_PIN, 1300, 125);
#endif
                            iterations--;
                        }
                        else
                        {
                            TTCtimer.detach();
                            ESP_LOGI(TAG, "End of function delay timer");
                            /*
                            if (light && (doorControlType != 3)) {
                                // dry contact cannot control lights
                                set_light(false);
                            }
                            */
                            if (callback)
                            {
                                // delay so that set_light() can do its thing
                                callbackDelay.once_ms(interval * 2, [callback]()
                                {
                                    if (callback == door_command_close)
                                    {
                                        ESP_LOGI(TAG,"Calling delayed function 'door_command_close()'");
                                    }
                                    else
                                    {
                                        ESP_LOGI(TAG,"Calling delayed function 0x%08lX", (uint32_t)callback);
                                    }
                                    callback();
                                });
                            }
                        } });
}

GarageDoorCurrentState close_door()
{
    if (garage_door.current_state == GarageDoorCurrentState::CURR_CLOSED)
    {
        ESP_LOGI(TAG, "Door already closed; ignored request");
        return GarageDoorCurrentState::CURR_CLOSED;
    }

    if (garage_door.current_state == GarageDoorCurrentState::CURR_OPENING)
    {
        ESP_LOGI(TAG, "Door already opening; do stop");
#ifdef USE_GDOLIB
        gdo_door_stop();
#else
        door_command(DoorAction::Stop);
#endif
        return GarageDoorCurrentState::CURR_STOPPED;
    }

    if (userConfig->getTTCseconds() == 0)
    {
        ESP_LOGI(TAG, "Closing door");
        door_command_close();
    }
    else
    {
        if (TTCtimer.active())
        {
            // We are in a time-to-close delay timeout, cancel the timeout
            ESP_LOGI(TAG, "Canceling TTC delay timer");
            TTCtimer.detach();
            // Reset light to state it was at before delay start.
            set_light(TTCwasLightOn);
            return GarageDoorCurrentState::CURR_OPEN;
        }
        else
        {
            ESP_LOGI(TAG, "Delay door close by %d seconds", userConfig->getTTCseconds());
#ifdef USE_GDOLIB
            if (doorControlType == 2 && userConfig->getBuiltInTTC())
            {
                gdo_set_time_to_close(userConfig->getTTCseconds());
            }
            else
            {
                delayFnCall(userConfig->getTTCseconds() * 1000, door_command_close);
            }
#else
            delayFnCall(userConfig->getTTCseconds() * 1000, door_command_close);
#endif // USE_GDOLIB
        }
    }
    return GarageDoorCurrentState::CURR_CLOSING;
}

#ifndef USE_GDOLIB
void send_get_status()
{
    // only used with SECURITY2.0
    if (doorControlType != 2)
        return;

    PacketData d;
    d.type = PacketDataType::NoData;
    d.value.no_data = NoData();
    Packet pkt = Packet(PacketCommand::GetStatus, d, id_code);
    PacketAction pkt_ac = {pkt, true};
#ifdef ESP8266
    if (!q_push(&pkt_q, &pkt_ac))
#else
    if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
    {
        ESP_LOGE(TAG, "packet queue full, dropping get status pkt");
    }
}

void send_get_openings()
{
    // only used with SECURITY2.0
    if (doorControlType != 2)
        return;

    PacketData d;
    d.type = PacketDataType::NoData;
    d.value.no_data = NoData();
    Packet pkt = Packet(PacketCommand::GetOpenings, d, id_code);
    PacketAction pkt_ac = {pkt, true};
#ifdef ESP8266
    if (!q_push(&pkt_q, &pkt_ac))
#else
    if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
    {
        ESP_LOGE(TAG, "packet queue full, dropping get status pkt");
    }
}
#endif

#ifdef USE_GDOLIB
bool set_lock(bool value, bool verify)
{
    // return value: true = lock state changed, else state unchanged
    if (verify && (garage_door.current_lock == ((value) ? LockCurrentState::CURR_LOCKED : LockCurrentState::CURR_UNLOCKED)))
    {
        ESP_LOGI(TAG, "Remote locks already %s; ignored request", (value) ? "locked" : "unlocked");
        return false;
    }

    garage_door.target_lock = (value) ? TGT_LOCKED : TGT_UNLOCKED;
    ESP_LOGI(TAG, "Set Garage Door Remote locks: %s", (value) ? "locked" : "unlocked");
    if (value)
        gdo_lock();
    else
        gdo_unlock();
    return true;
}
#else // USE_GDOLIB
bool set_lock(bool value, bool verify)
{
    // return value: true = lock state changed, else state unchanged
    if (verify && (garage_door.current_lock == ((value) ? LockCurrentState::CURR_LOCKED : LockCurrentState::CURR_UNLOCKED)))
    {
        ESP_LOGI(TAG, "Remote locks already %s; ignored request", (value) ? "locked" : "unlocked");
        return false;
    }

    PacketData data;
    data.type = PacketDataType::Lock;
    data.value.lock.lock = (value) ? LockState::On : LockState::Off;
    garage_door.target_lock = (value) ? TGT_LOCKED : TGT_UNLOCKED;
    ESP_LOGI(TAG, "Set Garage Door Remote locks: %s", (value) ? "locked" : "unlocked");

    // SECURITY1.0
    if (doorControlType == 1)
    {
        data.value.lock.pressed = true;
        Packet pkt = Packet(PacketCommand::Lock, data, id_code);
        PacketAction pkt_ac = {pkt, true};

#ifdef ESP8266
        if (!q_push(&pkt_q, &pkt_ac))
#else
        if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
        {
            ESP_LOGE(TAG, "packet queue full, dropping lock pkt");
        }
        // button release
        pkt_ac.pkt.m_data.value.lock.pressed = false;
#ifdef ESP8266
        if (!q_push(&pkt_q, &pkt_ac))
#else
        if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
        {
            ESP_LOGE(TAG, "packet queue full, dropping lock pkt");
        }
        // repeat the release
#ifdef ESP8266
        if (!q_push(&pkt_q, &pkt_ac))
#else
        if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
        {
            ESP_LOGE(TAG, "packet queue full, dropping lock pkt");
        }
    }
    // SECURITY2.0
    else
    {
        Packet pkt = Packet(PacketCommand::Lock, data, id_code);
        PacketAction pkt_ac = {pkt, true};
#ifdef ESP8266
        if (!q_push(&pkt_q, &pkt_ac))
#else
        if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
        {
            ESP_LOGE(TAG, "packet queue full, dropping lock pkt");
        }
    }
    return true;
}
#endif // USE_GDOLIB

#ifdef USE_GDOLIB
bool set_light(bool value, bool verify)
{
    ESP_LOGI(TAG, "Set Garage Door Light: %s", (value) ? "on" : "off");
    if (value)
        gdo_light_on_check(verify);
    else
        gdo_light_off_check(verify);
    return true;
}
#else

void sec1_light_press()
{
    PacketData data;
    data.type = PacketDataType::Light;
    data.value.light.light = LightState::On;
    data.value.light.pressed = true;
    Packet pkt = Packet(PacketCommand::Light, data, id_code);
    PacketAction pkt_ac = {pkt, true};
#ifdef ESP8266
    if (!q_push(&pkt_q, &pkt_ac))
#else
    if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
    {
        ESP_LOGE(TAG, "packet queue full, dropping light press pkt");
    }
}

void sec1_light_release(uint8_t howManyReleases)
{
    PacketData data;
    data.type = PacketDataType::Light;
    data.value.light.light = LightState::On;
    data.value.light.pressed = false;
    Packet pkt = Packet(PacketCommand::Light, data, id_code);
    PacketAction pkt_ac = {pkt, true};

    for (int numReleases = 0; numReleases < std::min(2, (int)howManyReleases); numReleases++)
    {
#ifdef ESP8266
        if (!q_push(&pkt_q, &pkt_ac))
#else
        if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
        {
            ESP_LOGE(TAG, "packet queue full, dropping light release pkt #%d", numReleases);
        }
    }
}

bool set_light(bool value, bool verify)
{
    // return value: true = light state changed, else state unchanged
    if (verify && (garage_door.light == value))
    {
        ESP_LOGI(TAG, "Light already %s; ignored request", (value) ? "on" : "off");
        return false;
    }

    ESP_LOGI(TAG, "Set Garage Door Light: %s", (value) ? "on" : "off");

    // SECURITY+1.0
    if (doorControlType == 1)
    {
        // only can toggle the light
        sec1_light_press();
        sec1_light_release();
    }
    // SECURITY+2.0
    else
    {
        PacketData data;
        data.type = PacketDataType::Light;
        data.value.light.light = (value) ? LightState::On : LightState::Off;
        Packet pkt = Packet(PacketCommand::Light, data, id_code);
        PacketAction pkt_ac = {pkt, true};
#ifdef ESP8266
        if (!q_push(&pkt_q, &pkt_ac))
#else
        if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL)
#endif
        {
            ESP_LOGE(TAG, "packet queue full, dropping light pkt");
        }
        if (verify)
            send_get_status();
    }
    return true;
}
#endif // USE_GDOLIB

void manual_recovery()
{
    // Don't check for manual recovery if in midst of a time-to-close delay
    if (TTCtimer.active())
        return;

    // Increment counter every time button is pushed.  If we hit 5 in 3 seconds,
    // go to WiFi recovery mode
    if (force_recover.push_count++ == 0)
    {
        ESP_LOGI(TAG, "Push count start");
        force_recover.timeout = _millis() + 3000;
    }
    else if ((int32_t)(_millis() - force_recover.timeout) > 0)
    {
        ESP_LOGI(TAG, "Push count reset");
        force_recover.push_count = 0;
    }
    ESP_LOGI(TAG, "Push count %d", force_recover.push_count);

    if (force_recover.push_count >= 5)
    {
        ESP_LOGI(TAG, "Request to boot into soft access point mode in %d seconds", force_recover_delay);
        userConfig->set(cfg_softAPmode, true);
        ESP8266_SAVE_CONFIG();
        delayFnCall(force_recover_delay * 1000, sync_and_restart);
    }
}

#ifndef USE_GDOLIB
/*************************** OBSTRUCTION DETECTION **************************
 *
 */
void obstruction_timer()
{
    if (get_obstruction_from_status)
        return;

    _millis_t current_millis = _millis();
    static _millis_t last_millis = 0;

    // the obstruction sensor has 3 states: clear (HIGH with LOW pulse every 7ms), obstructed (HIGH), asleep (LOW)
    // the transitions between awake and asleep are tricky because the voltage drops slowly when falling asleep
    // and is high without pulses when waking up

    // If at least 3 low pulses are counted within 50ms, the door is awake, not obstructed and we don't have to check anything else

    const uint32_t CHECK_PERIOD = 50;
    const uint32_t PULSES_LOWER_LIMIT = 3;
    if ((uint32_t)(current_millis - last_millis) > CHECK_PERIOD)
    {
        // Atomically read and reset the pulse count to prevent race with ISR
        noInterrupts();
        uint32_t pulse_count = obstruction_sensor.low_count;
        obstruction_sensor.low_count = 0;
        interrupts();

        // check to see if we got more then PULSES_LOWER_LIMIT pulses
        if (pulse_count > PULSES_LOWER_LIMIT)
        {
            // We're getting pulses, so pin detection is working
            obstruction_sensor.pin_ever_changed = true;
            if (!obstruction_sensor_detected)
            {
                obstruction_sensor_detected = true;
                ESP_LOGI(TAG, "Pin-based obstruction detection active");
            }

            // Only update if we are changing state
            if (garage_door.obstructed)
            {
                ESP_LOGI(TAG, "Obstruction Clear");
                notify_homekit_obstruction(false);
                digitalWrite(STATUS_OBST_PIN, HIGH);
                if (motionTriggers.bit.obstruction)
                {
                    notify_homekit_motion(false);
                }
            }
        }
        else if (pulse_count == 0)
        {
            // if there have been no pulses the line is steady high or low
            if (!digitalRead(INPUT_OBST_PIN))
            {
                // asleep - pin went LOW, so it's not stuck HIGH
                obstruction_sensor.last_asleep = current_millis;
                obstruction_sensor.pin_ever_changed = true;
            }
            else
            {
                // if the line is high and was last asleep more than 700ms ago, then there is an obstruction present
                if ((uint32_t)(current_millis - obstruction_sensor.last_asleep) > 700)
                {
                    // Don't trust a HIGH pin that has never changed - likely floating/stuck
                    if (!obstruction_sensor.pin_ever_changed)
                    {
                        // Pin has been HIGH since boot, probably no sensor connected
                        return;
                    }

                    // Only update if we are changing state
                    if (!garage_door.obstructed)
                    {
                        ESP_LOGI(TAG, "Obstruction Detected");
                        notify_homekit_obstruction(true);
                        digitalWrite(STATUS_OBST_PIN, LOW);
                        if (motionTriggers.bit.obstruction)
                        {
                            notify_homekit_motion(true);
                        }
                    }
                }
            }
        }

        last_millis = current_millis;
    }
}
#endif // !USE_GDOLIB
