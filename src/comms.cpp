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

#ifdef USE_GDOLIB
#include "gdo.h"
#else // USE_GDOLIB
#include "SoftwareSerial.h"
#include "Reader.h"
#include "secplus2.h"
#include "Packet.h"
#include "drycontact.h"
#endif // USE_GDOLIB

#ifdef ESP8266
#include "cQueue.h"
#endif // ESP8266

static const char *TAG = "ratgdo-comms";

bool comms_setup_done = false;

/********************************** LOCAL STORAGE *****************************************/
#ifndef USE_GDOLIB
struct __attribute__((aligned(4))) PacketAction
{
    Packet pkt;
    bool inc_counter;
    uint32_t delay;
};

// On ESP32 we use the FreeRTOS queues.  This is not available on our ESP8266 builds.
// Define inline functions here so that remaining code is cleaner.
#define COMMAND_QUEUE_SIZE 16
#ifdef ESP32
QueueHandle_t pkt_q;
#else
Queue_t pkt_q;
#endif // ESP32

inline bool txQueueCreate()
{
#ifdef ESP32
    pkt_q = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(PacketAction));
    return pkt_q != NULL;
#else
    return q_init(&pkt_q, sizeof(PacketAction), COMMAND_QUEUE_SIZE, FIFO, false) != NULL;
#endif
}

inline uint32_t txQueueCount()
{
#ifdef ESP32
    return (uint32_t)uxQueueMessagesWaiting(pkt_q);
#else
    return (uint32_t)q_getCount(&pkt_q);
#endif
}

inline bool txQueuePush(PacketAction *pkt)
{
#ifdef ESP32
    return xQueueSendToBack(pkt_q, pkt, 0) == pdTRUE;
#else
    return q_push(&pkt_q, pkt);
#endif
}

inline bool txQueuePeek(PacketAction *pkt)
{
#ifdef ESP32
    return xQueuePeek(pkt_q, pkt, 0) == pdTRUE;
#else
    return q_peek(&pkt_q, pkt);
#endif
}

inline bool txQueuePop(PacketAction *pkt)
{
#ifdef ESP32
    return xQueueReceive(pkt_q, pkt, 0) == pdTRUE;
#else
    return q_pop(&pkt_q, pkt);
#endif
}

// used by SEC+1.0
#ifdef ESP32
#define Sec1Serial Serial2
#else
#define Sec1Serial sw_serial
#endif

// used by SEC+2.0
SoftwareSerial sw_serial;

#endif // not USE_GDOLIB

#define SECPLUS1_DIGITAL_WALLPLATE_TIMEOUT 15000
#define SECPLUS1_RX_MESSAGE_TIMEOUT 20
#define SECPLUS1_TX_WINDOW_OPEN 5
#define SECPLUS1_TX_WINDOW_CLOSE 200
#define SECPLUS1_TX_MINIMUM_DELAY 30
#define SECPLUS2_TX_MINIMUM_DELAY 50

#define COMMS_STATUS_TIMEOUT 2000
bool comms_status_done = false;
_millis_t comms_status_start = 0;
_millis_t tx_minimum_delay = SECPLUS2_TX_MINIMUM_DELAY;
uint32_t doorControlType = 0;

static bool is_0x37_panel = false;
/* Removing this section as testing with 398LM (a 0x37 wall panel) was never successful.
static bool door_moving = false;
*/

// For Time-to-close control
static const uint32_t TTCinterval = 250;
static uint32_t TTCiterations = 0;
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

#ifndef USE_GDOLIB
// Becomes set from ISR / IRQ callback function.
static bool rxPending;
void IRAM_ATTR receiveHandler()
{
    rxPending = true;
}
/****************************************************************************
 * checks if there is any RX data in process of being received
 */
__attribute__((always_inline)) inline bool isRxPending()
{
    bool pending;
#ifdef ESP8266
    noInterrupts();
#else
    static portMUX_TYPE m_interruptsMux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&m_interruptsMux);
#endif
    // rxPending is set in ISR
    if ((pending = rxPending))
        rxPending = false;
#ifdef ESP8266
    interrupts();
#else
    taskEXIT_CRITICAL(&m_interruptsMux);
#endif
    return pending;
}
#endif // USE_GDOLIB

/****************************** COMMON SETTING *********************************/
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
_millis_t last_tx = 0;
_millis_t msg_start = 0;
_millis_t msg_complete = 0;
bool clearToSend = false;
// wall panel management
bool wallPanelBooting = false;
bool wallPanelDetected = false;
#define WP_CONNECTED LOW
#define WP_DISCONNECTED HIGH
uint8_t wallPanelConnected;
// states
GarageDoorCurrentState doorState = (GarageDoorCurrentState)0xFF;

// power up sequence + poll items for digitial wall panel 889LM
// MJS: this is what MY 889LM exhibited when powered up (release of all buttons, and then polls)
// MJS: the 0x53, GDO responds with 0x01 (since we dont use it, seems OK to not sent to GDO)
byte secplus1States[] = {0x31, 0x31, 0x35, 0x35, 0x33, 0x33, 0x53, 0x53, /* POLL ITEMS --> */ 0x38, 0x3A, 0x39, 0x3A};
#define SECPLUS1_POLL_ITEMS 4 // poll last x items at end of secplus1States[]

// values for SECURITY+1.0 communication
enum secplus1Codes : uint8_t
{
    DoorButtonPress = 0x30,
    DoorButtonRelease = 0x31,
    LightButtonPress = 0x32,
    LightButtonRelease = 0x33,
    LockButtonPress = 0x34,
    LockButtonRelease = 0x35,

    Unknown_0x36 = 0x36,
    QueryDoorStatus_0x37 = 0x37, // sent by a "0x37" wall panel

    DoorStatus = 0x38,
    ObstructionStatus = 0x39,
    LightLockStatus = 0x3A,

    DoorMovingStatus = 0x40, // sent by a "0x37" wall panel

    UnknownStatus_0x53 = 0x53, // sent by a "0x37" wall panel and WP when done its "power up"

    Unknown = 0xFF // (when rx fails parity test)
};

#define SEC1_CMD(s) (s == secplus1Codes::DoorButtonPress)      ? "door press"    \
                    : (s == secplus1Codes::DoorButtonRelease)  ? "door release"  \
                    : (s == secplus1Codes::LightButtonPress)   ? "light press"   \
                    : (s == secplus1Codes::LightButtonRelease) ? "light release" \
                    : (s == secplus1Codes::LockButtonPress)    ? "lock press"    \
                    : (s == secplus1Codes::LockButtonRelease)  ? "lock release"  \
                                                               : "unknown"

// prototypes
void sync();
bool process_PacketAction(PacketAction &pkt_ac);
void door_command(DoorAction action);
void send_get_status();
void send_get_openings();
bool transmitSec1(byte toSend);
bool transmitSec2(PacketAction &pkt_ac);
void obstruction_timer();
void sec1_poll_status(uint8_t sec1PollCmd);
#ifdef ESP32
void receiveErrorHandler(hardwareSerial_error_t error);
#endif
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
        ESP_LOGI(TAG, "GDO event: light: %s (%s)", gdo_light_state_to_string(status->light), timeString());
        notify_homekit_light(status->light == gdo_light_state_t::GDO_LIGHT_STATE_ON);
        break;
    case GDO_CB_EVENT_LOCK:
        ESP_LOGI(TAG, "GDO event: lock remotes: %s (%s)", gdo_lock_state_to_string(status->lock), timeString());
        notify_homekit_target_lock(gdo_to_homekit_lock_target_state[status->lock]);
        notify_homekit_current_lock(gdo_to_homekit_lock_current_state[status->lock]);
        break;
    case GDO_CB_EVENT_DOOR_POSITION:
    {
        ESP_LOGI(TAG, "GDO event: door: %s, %3d, target: %3d", gdo_door_state_to_string(status->door),
                 status->door_position / 100, (status->door_target >= 0) ? status->door_target / 100 : -1);
        garage_door.active = true;
        if ((garage_door.current_state != gdo_to_homekit_door_current_state[status->door]) && (status->door != GDO_DOOR_STATE_UNKNOWN))
        {
            notify_homekit_current_door_state_change(gdo_to_homekit_door_current_state[status->door]);
            notify_homekit_target_door_state_change(gdo_to_homekit_door_target_state[status->door]);

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
        ESP_LOGI(TAG, "GDO event: obstruction: %s (%s)", gdo_obstruction_state_to_string(status->obstruction), timeString());
        notify_homekit_obstruction(status->obstruction == gdo_obstruction_state_t::GDO_OBSTRUCTION_STATE_OBSTRUCTED);
        if (motionTriggers.bit.obstruction && garage_door.obstructed)
        {
            notify_homekit_motion(true);
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
        ESP_LOGI(TAG, "GDO event: open duration: %d seconds (%s)", garage_door.openDuration, timeString());
        break;
    case GDO_CB_EVENT_CLOSE_DURATION_MEASUREMENT:
        garage_door.closeDuration = (status->close_ms + 500) / 1000; // round up/down to closest second
        ESP_LOGI(TAG, "GDO event: close duration: %d seconds (%s)", garage_door.closeDuration, timeString());
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
    txQueueCreate();

    // set to output (not currently used (prob not ported over) using now for new disconnect of wall panel)
    pinMode(STATUS_DOOR_PIN, OUTPUT);

    if (doorControlType == 1)
    {
        ESP_LOGI(TAG, "=== Setting up comms for SECURITY+1.0 protocol");

        // ESP32:GPIO_NUM_26 - ESP8266:GPIO_NUM16(D0)
        // ⁡⁢⁣⁢NC RELAY (AQY412)⁡
        // enable wall panel
        wallPanelConnected = WP_CONNECTED;
        digitalWrite(STATUS_DOOR_PIN, wallPanelConnected);

        // set minimum delay between tx bytes
        tx_minimum_delay = SECPLUS1_TX_MINIMUM_DELAY;

#ifdef ESP32
        gpio_reset_pin(UART_TX_PIN);
        gpio_reset_pin(UART_RX_PIN);
        Sec1Serial.begin(1200, SERIAL_8E1, UART_RX_PIN, UART_TX_PIN, true);
        Sec1Serial.onReceiveError(receiveErrorHandler);
        Sec1Serial.setTimeout(10); // 10 ms used for Sec1Serial.readBytes() in transmitSec1()
#else
        Sec1Serial.begin(1200, SWSERIAL_8E1, UART_RX_PIN, UART_TX_PIN, true, 32);
        Sec1Serial.onReceive(receiveHandler);
#endif

        wallPanelDetected = false;
        wallPanelBooting = false;
        doorState = (GarageDoorCurrentState)0xFF;
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
    }
    else
    {
        ESP_LOGI(TAG, "=== Setting up comms for dry contact protocol");
        pinMode(UART_TX_PIN, OUTPUT);
    }
    force_recover.push_count = 0;
    force_recover.enable = true;
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
    }
    force_recover.push_count = 0;
    force_recover.enable = true;
#endif

#ifndef USE_GDOLIB
    if (!(get_obstruction_from_status = userConfig->getObstFromStatus()))
    {
        // pin-based obstruction detection attempted only if user not requested to get from status
        ESP_LOGI(TAG, "Initialize for pin-based obstruction detection");
#if defined(ESP8266) || defined(OBST_PIN_NORMAL)
        pinMode(INPUT_OBST_PIN, INPUT);
#else
        // enable pull up for pin inversion on RATDGO32/RATDGO32 DISCO (ESP32)
        pinMode(INPUT_OBST_PIN, INPUT_PULLUP);
#endif
        // FALLING from https://github.com/ratgdo/esphome-ratgdo/blob/e248c705c5342e99201de272cb3e6dc0607a0f84/components/ratgdo/ratgdo.cpp#L54C14-L54C14
        attachInterrupt(INPUT_OBST_PIN, isr_obstruction, FALLING);
    }
    else
    {
        ESP_LOGI(TAG, "Use status messages for obstruction detection");
    }
    // set the status pin for output
    pinMode(STATUS_OBST_PIN, OUTPUT);
#endif
    comms_setup_done = true;
    comms_status_start = _millis();
}

/****************************************************************************
 * shutdown communications
 */
void shutdown_comms()
{
    if (!comms_setup_done)
        return;

    comms_setup_done = false;
#ifdef USE_GDOLIB
    // Shutdown GDO comms
    gdo_deinit();
#else
    if (doorControlType == 1)
    {
        Sec1Serial.end();
    }
    else
    {
        sw_serial.end();
    }
#ifdef ESP32
    gpio_reset_pin(UART_TX_PIN);
    gpio_reset_pin(UART_RX_PIN);
#endif
#endif
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
void sec1_poll_status(uint8_t sec1PollCmd)
{
    // send through queue
    PacketData data;
    data.type = PacketDataType::Status;
    data.value.cmd = sec1PollCmd;
    Packet pkt = Packet(PacketCommand::Status, data, id_code);
    PacketAction pkt_ac = {pkt, true, 0};

    if (!txQueuePush(&pkt_ac))
    {
        ESP_LOGE(TAG, "packet queue full, dropping panel emulation status pkt");
    }
}

void wallPlate_Emulation()
{
    if (wallPanelDetected)
        return;

    _millis_t currentMillis = _millis();
    static _millis_t lastRequestMillis = 0;
    static _millis_t startMillis = currentMillis;
    static bool emulateWallPanel = false;

    // transmit every 250ms
    if (emulateWallPanel && (currentMillis - lastRequestMillis) > 250)
    {
        static uint8_t stateIndex = 0;
        lastRequestMillis = currentMillis;

        byte secplus1ToSend = byte(secplus1States[stateIndex]);

        sec1_poll_status(secplus1ToSend);

        // set next poll
        stateIndex++;
        if (stateIndex == sizeof(secplus1States))
        {
            stateIndex = sizeof(secplus1States) - SECPLUS1_POLL_ITEMS;
        }
        return;
    }

    // Only get this far if we have not detected a digital wall panel or started emulation 
    // wait up to 15 seconds to look for an existing wallplate or it could be booting, so need to wait
    if (currentMillis - startMillis < SECPLUS1_DIGITAL_WALLPLATE_TIMEOUT || wallPanelBooting == true)
    {
        if (currentMillis - lastRequestMillis > 1000)
        {
            ESP_LOGI(TAG, "Looking for security+ 1.0 DIGITAL wall panel...");
            lastRequestMillis = currentMillis;
        }

        if (!wallPanelDetected && (garage_door.current_state != (GarageDoorCurrentState)0xFF || garage_door.current_lock != (LockCurrentState)0xFF))
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
            garage_door.wallPanelEmulated = true;
            ESP_LOGI(TAG, "No DIGITAL wall panel detected. Switching to emulation mode.");
        }
    }
}

void update_door_state(GarageDoorCurrentState current_state)
{
    constexpr uint32_t MAX_HISTORY = 5;            // Number of door operations to average across
    constexpr uint32_t MAX_DURATION = (45 * 1000); // Maximum time it should take to open/close a door
    static _millis_t start_opening = 0;
    static _millis_t start_closing = 0;
    static _millis_t open_history[MAX_HISTORY] = {0};
    static uint32_t open_counter = 0;
    static _millis_t close_history[MAX_HISTORY] = {0};
    static uint32_t close_counter = 0;
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
    default:
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
        if (open_duration <= MAX_DURATION)
        {
            _millis_t open_average = 0;
            open_history[open_counter++ % MAX_HISTORY] = open_duration;
            uint32_t count = std::min(open_counter, MAX_HISTORY);
            for (uint32_t i = 0; i < count; i++)
                open_average += open_history[i];
            open_average /= count;
            garage_door.openDuration = (open_average + 500) / 1000; // round up/down to closest second
            ESP_LOGI(TAG, "Door open duration: %lums, average: %lums (%s)", (uint32_t)open_duration, (uint32_t)open_average, timeString());
        }
        else
        {
            start_opening = 0;
            ESP_LOGW(TAG, "Ignoring implausibly long open duration: %lums (%s)", (uint32_t)open_duration, timeString());
        }
    }
    else if (current_state == CURR_CLOSING && garage_door.current_state == CURR_OPEN)
    {
        start_closing = _millis();
        ESP_LOGD(TAG, "Record start time of door closing: %llums", (uint64_t)start_closing);
    }
    else if (current_state == CURR_CLOSED && garage_door.current_state == CURR_CLOSING && start_closing > 0)
    {
        _millis_t close_duration = _millis() - start_closing;
        if (close_duration <= MAX_DURATION)
        {
            _millis_t close_average = 0;
            close_history[close_counter++ % MAX_HISTORY] = close_duration;
            uint32_t count = std::min(close_counter, MAX_HISTORY);
            for (uint32_t i = 0; i < count; i++)
                close_average += close_history[i++];
            close_average /= count;
            garage_door.closeDuration = (close_average + 500) / 1000; // round up/down to closest second
            ESP_LOGI(TAG, "Door close duration: %lums, average: %lums (%s)", (uint32_t)close_duration, (uint32_t)close_average, timeString());
        }
        else
        {
            start_closing = 0;
            ESP_LOGW(TAG, "Ignoring implausibly long close duration: %lums (%s)", (uint32_t)close_duration, timeString());
        }
    }
    else if ((current_state == CURR_STOPPED) ||
             (current_state == CURR_OPENING && garage_door.current_state == CURR_CLOSING) ||
             (current_state == CURR_CLOSING && garage_door.current_state == CURR_OPENING))
    {
        // If door is stopped (neither fully open or fully closed) then abort measuring duration
        start_opening = 0;
        start_closing = 0;
        ESP_LOGD(TAG, "Aborting door open/close duration calculation");
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
        ESP_LOGI(TAG, "Door state changing from %s to %s (target %s)", DOOR_STATE(garage_door.current_state), DOOR_STATE(current_state), DOOR_STATE(target_state));
        notify_homekit_current_door_state_change(current_state);
        notify_homekit_target_door_state_change(target_state);
    }

    // Update the global
    doorState = current_state;
}

void sec1_process_message(uint8_t key, uint8_t value = 0xFF)
{
    if (value != 0xFF)
    {
        // Unknown key values will be logged in default of switch statement below.
        // This logs all key/value pairs from known "poll" commands and the response from the GDO.
        static _millis_t lastTime = 0;
        _millis_t now = _millis();
        ESP_LOGV(TAG, "SEC1 RX IDLE:%lums - MSG: 0x%02X:0x%02X", (uint32_t)(now - lastTime), key, value);
        lastTime = now;
    }

    switch (key)
    {
    // door button press
    case secplus1Codes::DoorButtonPress:
    {
        ESP_LOGD(TAG, "SEC1 RX 0x30 (door press)");
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
        ESP_LOGD(TAG, "SEC1 RX 0x31 (door release)");
        // Possible power up of 889LM
        if (doorState == (GarageDoorCurrentState)0xFF)
        {
            wallPanelBooting = true;
        }
        break;
    }

    // light button press
    case secplus1Codes::LightButtonPress:
    {
        ESP_LOGD(TAG, "SEC1 RX 0x32 (light press)");
        manual_recovery();
        break;
    }

    // light button release
    case secplus1Codes::LightButtonRelease:
    {
        ESP_LOGD(TAG, "SEC1 RX 0x33 (light release)");
        break;
    }

    // lock button press
    case secplus1Codes::LockButtonPress:
    {
        ESP_LOGD(TAG, "SEC1 RX 0x34 (lock press)");
        break;
    }

    // lock button release
    case secplus1Codes::LockButtonRelease:
    {
        ESP_LOGD(TAG, "SEC1 RX 0x35 (lock release)");
        break;
    }

    // Door moving - only seen with 0x37 panels
    case secplus1Codes::DoorMovingStatus:
    {
        static uint8_t previous = 0xFF;
        if (value != previous)
        {
            ESP_LOGD(TAG, "SEC1 RX 0x40 (door moving) value changed from 0x%02X to 0x%02X", previous, value);
            previous = value;
            /* Removing this section as testing with 398LM (a 0x37 wall panel) was never successful.
            static Ticker delayReset = Ticker();
            if (bitRead(value, 7))
            {
                delayReset.detach();
                door_moving = true;
            }
            else
            {
                // Delay reset of door moving flag for a couple of seconds... to allow
                // us to detect the new state of the door. It will also be reset immediately
                // if we detect door has stopped (handled elsewhere)
                delayReset.once_ms(2 * 1000, []()
                                   { door_moving = false; });
            }
            */
        }
        break;
    }

    // Unknown status packet
    case secplus1Codes::UnknownStatus_0x53:
    {
        static uint8_t previous = 0xFF;
        if (value != previous)
        {
            ESP_LOGD(TAG, "SEC1 RX 0x53 (Unknown) value changed from 0x%02X to 0x%02X", previous, value);
            previous = value;
        }
        break;
    }

    // Sent by 0x37 panels, values returned are unknown
    case secplus1Codes::QueryDoorStatus_0x37:
    {
        static uint8_t previous = 0xFF;
        if (value != previous)
        {
            ESP_LOGD(TAG, "SEC1 RX 0x37 (Unknown) value changed from 0x%02X to 0x%02X", previous, value);
            previous = value;
        }
        /* Removing this section as testing with 398LM (a 0x37 wall panel) was never successful.
        // ESPhome firmware will peek queue looking for TOGGLE_LOCK_PRESS
        // If yes then process it instead of sending door status request.
        // WHY??? I do not know
        PacketAction pkt_ac;
        if (txQueuePeek(&pkt_ac) && pkt_ac.pkt.m_data.type == PacketDataType::Lock && pkt_ac.pkt.m_data.value.lock.pressed)
        {
            if (transmitSec1(secplus1Codes::LockButtonPress))
            {
                // Successfully sent, so remove TX packet from the queue
                txQueuePop(&pkt_ac);
                // ESP_LOGI(TAG, "SEC1 TX sent LOCK button press");
            }
            else
            {
                ESP_LOGI(TAG, "SEC1 TX failed to send LOCK button press");
            }
        }
        else
        {
            // If door moving or no more frequently than once every 10 seconds, inject door status request
            static _millis_t last_status_query = 0;
            if (door_moving || (_millis() - last_status_query > 10000))
            {
                // Write directly rather than go through all checking done by transmitSec1()
                sw_serial.write(secplus1Codes::DoorStatus);
                // timestamp tx
                last_tx = last_status_query = _millis();
            }
        }
        */
        break;
    }

    // door status
    case secplus1Codes::DoorStatus:
    {
        // 0x5X = stopped
        // 0x0X = moving
        // upper nibble should be 0x5 or 0x0 (DK reported 0x1)
        // sec+1 doors sometimes report wrong door status
        // back to original code, MJS 8/14/2025 confirmed logging
        // it could report a valid byte but its not really valid
        // ie: opening when its already open
        static uint8_t prevDoor = 0xFF;          // Initialize to invalid value
        if (prevDoor != value && !is_0x37_panel) // don't require two back-to-back if 0x37 panel
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
            // door_moving = false;
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
            // door_moving = false;
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
            // door_moving = false;
            break;
        case 0x06:
            if (garage_door.current_state == CURR_CLOSED || garage_door.current_state == CURR_OPEN)
            {
                ESP_LOGI(TAG, "Ignoring invalid door state change from %s to STOPPED (0x06)", (garage_door.current_state == CURR_CLOSED) ? "CLOSED" : "OPEN");
                break;
            }
            current_state = GarageDoorCurrentState::CURR_STOPPED;
            // door_moving = false;
            break;
        default:
            ESP_LOGE(TAG, "SEC1 RX Got unknown \"value\" for door state");
            current_state = (GarageDoorCurrentState)0xFF;
            break;
        }
        update_door_state(current_state);
        break;
    }

    // obstruction states
    case secplus1Codes::ObstructionStatus:
    {
        // 0x00         No obstruction
        // 0x00 -> 0x04 Obstruction beam broken, implies motion
        // 0x04 -> 0x01 Stable obstruction
        // 0x01 -> 0x04 Obstruction removed, implies motion
        // 0x04 -> 0x00 No obstruction

        static uint8_t prevObstruction = 0xFF; // Initialize to invalid value
        if (value != prevObstruction)
        {
            ESP_LOGD(TAG, "0x39 (obstruction) value changed from 0x%02X to 0x%02X", prevObstruction, value);
            prevObstruction = value;
            // Handle obstruction from status packet if pin-based detection not used
            if (!garage_door.pinModeObstructionSensor)
            {
                // Reported value has changed
                bool status_obstructed = bitRead(value, 0);
                bool status_motion = bitRead(value, 2);
                if (garage_door.obstructed != status_obstructed)
                {
                    // Obstruction state changed
                    ESP_LOGI(TAG, "Obstruction: %s (Status packet) (%s)", status_obstructed ? "Obstructed" : "Clear", timeString());
                    notify_homekit_obstruction(status_obstructed);
                    digitalWrite(STATUS_OBST_PIN, !status_obstructed);
                }
                if (motionTriggers.bit.obstruction && status_motion)
                {
                    // User want to trigger motion sensor based on obstruction beam
                    notify_homekit_motion(true);
                }
            }
        }
        break;
    }

    // light & lock
    case secplus1Codes::LightLockStatus:
    {
        // only use for real sec1 comms debugging, its just too chatty
        // ESP_LOGD(TAG, "SEC1 RX 0x3A value: 0x%02X", value);

        // upper nibble should be 0x5 or 0x1
        // make sure 2 same in a row
        // MJS 8/14/2025 during logging observed this situation

        static uint8_t prevLightLock = 0xFF;  // for two-in-a-row detection
        static uint8_t lastLightState = 0xff; // for change detection
        if (TTCtimer.active())
        {
            // As we flash lights during TTC delay, avoid lots of updates to clients
            ESP_LOGV(TAG, "Ignoring light/lock status change during time-to-close delay");
            prevLightLock = lastLightState = 0xFF;
            break;
        }
        else if (lastLightState == 0xFF)
        {
            // Force update of light state in any listening client
            last_reported_garage_door.light = !garage_door.light;
        }

        if (value != prevLightLock)
        {
            prevLightLock = value;
            break;
        }

        uint8_t lightState = bitRead(value, 2);
        uint8_t lockState = !bitRead(value, 3);

        // light state change?
        if (lightState != lastLightState)
        {
            ESP_LOGI(TAG, "Light: %s (%s)", lightState ? "On" : "Off", timeString());
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
            ESP_LOGI(TAG, "Remotes lock: %s (%s)", LOCK_STATE(lockState), timeString());
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
    default:
    {
        ESP_LOGD(TAG, "SEC1 RX 0x%02X (unknown)", key);
        break;
    }
    }
}

bool process_send_queue()
{
    // PROCESS TRANSMIT QUEUE
    //
    PacketAction pkt_ac;
    uint32_t msgs;
    static uint32_t retryCount = 0;

    // Immediately return if there is nothing in the TX queue to process
    if ((msgs = txQueueCount()) == 0)
        return true;

    // Four packets is normal (e.g. sequence of light release after a TTC delay flash period)
    // But more than that may indicate a problem
    if (msgs > 8)
        ESP_LOGW(TAG, "WARNING: message packets in TX queue is > 8 (%lu)", msgs);

    txQueuePeek(&pkt_ac); // No need to check return value, we know queue is not empty

    bool okToSend = false;
    if ((_millis() - last_tx) >= std::max((uint32_t)tx_minimum_delay, (uint32_t)pkt_ac.delay))
    {
        okToSend = true;
    }

    // if there is a wall panel, need to make sure the clear to send timing is met
    // This will always be false for Sec+ 2.0 doors
    if (wallPanelDetected)
    {
        // set in ISR (SET on RX of START BIT)
        if (isRxPending())
        {
            clearToSend = false;
            ESP_LOGD(TAG, "SEC1 TX late detection isRxPending");
        }

        // close the tx window after Xms from start msg received
        if ((_millis() - msg_start) >= SECPLUS1_TX_WINDOW_CLOSE)
        {
            clearToSend = false;
        }
        okToSend &= clearToSend;
    }

    // meets our timing requirements
    if (okToSend)
    {
        if (process_PacketAction(pkt_ac))
        {
            // success, reset retry count
            retryCount = 0;
            // Remove TX packet from the queue
            txQueuePop(&pkt_ac);
        }
        else
        {
            if (retryCount++ < MAX_COMMS_RETRY)
            {
                if (doorControlType == 1)
                    ESP_LOGD(TAG, "SEC1 TX send [0x%02X] failed, will retry. retryCount at %d", pkt_ac.pkt.m_data.value.cmd, retryCount);
                else
                    ESP_LOGD(TAG, "SEC2 TX send failed, will retry. retryCount at %d", retryCount);

                // get out now, leaving the TX packet on the queue.
                return false;
            }
            else
            {
                ESP_LOGE(TAG, "SEC%d TX send failed, exceeded max retry", doorControlType);
                retryCount = 0;
                // Remove TX packet from the queue
                txQueuePop(&pkt_ac);
            }
        }
    }
    return true;
}

#ifdef ESP32
void receiveErrorHandler(hardwareSerial_error_t error)
{
    // ESP_LOGD(TAG, "-- onReceiveError: [ERR#%d:%s]", error, uartErrorStrings[error]);

    if (error == hardwareSerial_error_t::UART_PARITY_ERROR)
    {
        // LOOKS LIKE THE BYTE THE ERROR HAPPENS ON, IS NOT
        // PLACED IN THE RECEIVE BUFFER
        // might need to post in espressif to find out
    }
}
#endif

void comms_loop_sec1()
{
    static bool reading_msg = false;
    static RxPacket rx_packet;
    static uint8_t syncByteCount = 0;

    // CTS timer
    // when wall panel present, need 5ms elapsed after last complete message arrives.
    // if one arrives before that (ie multiple in rx buffers, the msg_complete time stamp is reset)
    if (!clearToSend)
    {
        // open the tx window
        if ((_millis() - msg_complete) >= SECPLUS1_TX_WINDOW_OPEN)
        {
            clearToSend = true;
        }
    }

    // get all the rxed bytes processed now
    // any rx bytes will reset clearToSend
    while (Sec1Serial.available())
    {
        uint8_t ser_byte = Sec1Serial.read();

        // reading byte so clear flag
        isRxPending();

        clearToSend = false;

        // this byte is received with invalid parity
        // it is sent when there is no buss traffic (need to look at it with scope)
        if (ser_byte == 0xFF)
        {
            syncByteCount++;
            if (syncByteCount == 10)
            {
                syncByteCount = 0;
                // alternate way to detect no wall panel
                // not in use as of now
                // but could start emulator here
            }

            // reset start of message (just incase somehow is 2nd byte)
            reading_msg = false;

            break;
        }

#ifdef ESP8266
        // parity check on byte (only available of SoftwareSerial)
        if (Sec1Serial.readParity() != Sec1Serial.parityEven(ser_byte))
        {
            if (reading_msg)
                ESP_LOGD(TAG, "SEC1 RX Parity error on 2nd byte of poll msg [0x%02X:0x%02X]", rx_packet[0], ser_byte);
            else
                ESP_LOGD(TAG, "SEC1 RX Parity error [0x%02X]", ser_byte);

            // toss message, start over
            reading_msg = false;

            continue;
        }
#endif

        if (ser_byte == secplus1Codes::QueryDoorStatus_0x37 && !reading_msg)
        {
            if (!is_0x37_panel)
            {
                // An older digital wall panel that send different sequence of codes
                is_0x37_panel = true;
                ESP_LOGW(TAG, "Detected a 0x37 digital wall panel, NOT SUPPORTED");
                ESP_LOGW(TAG, "Consider replacing your wall panel with a LiftMaster 889LM panel");
            }
        }

        // upper nibble always 0x3 for press/release/poll bytes (0x30 - 0x3A)
        // no GDO response has upper nibble 0x3, and its validated in sec1_process_message()
        // if a byte comes in as 0x3x even if reading 2 byte message, start over
        switch (ser_byte)
        {
        // Single byte... Commands sent by a wall panel or ourselves...
        case secplus1Codes::DoorButtonPress:
        case secplus1Codes::DoorButtonRelease:
        case secplus1Codes::LightButtonPress:
        case secplus1Codes::LightButtonRelease:
        case secplus1Codes::LockButtonPress:
        case secplus1Codes::LockButtonRelease:
        {
            sec1_process_message(ser_byte);
            // reset start of message
            reading_msg = false;
            break;
        }
        // Double byte... Commands sent by a wall panel or ourselves, plus reply from GDO...
        case secplus1Codes::QueryDoorStatus_0x37:
        case secplus1Codes::DoorMovingStatus:
        case secplus1Codes::UnknownStatus_0x53:
        case secplus1Codes::DoorStatus:
        case secplus1Codes::ObstructionStatus:
        case secplus1Codes::LightLockStatus:
        {
            // if we already waiting for a GDO response, and got a new poll...
            if (reading_msg)
            {
                ESP_LOGD(TAG, "SEC1 RX Prior poll msg incomplete [0x%02X] received, but lost GDO response", rx_packet[0]);
            }
            rx_packet[0] = ser_byte;
            // timestamp begining of message
            msg_start = _millis();
            reading_msg = true;
            break;
        }
        default:
        {
            if (reading_msg)
            {
                // we only allow 2 bytes max, and the reading_msg controls that
                // this is the value to response of the GDO query
                rx_packet[1] = ser_byte;
                sec1_process_message(rx_packet[0], rx_packet[1]);
                // time stamp
                msg_complete = _millis();
                // reset start of message
                reading_msg = false;
            }
            else
            {
                ESP_LOGD(TAG, "SEC1 RX invalid cmd byte 0x%02X", ser_byte);
            }
            break;
        }
        } // end of switch()
    } // end of while()

    // if still reading the message, no need to process further
    // or if a RX BIT has been has been received
    // or any ready bytes available (a byte came in somehow during above while loop, during testing it was only ever 1 byte)
    // process on next pass
    if (reading_msg == true || isRxPending() || Sec1Serial.available())
    {
        return;
    }

    if (process_send_queue())
    {
        // check for wall panel and provide emulator
        wallPlate_Emulation();
    }
}

/****************************************************************************
 * Sec+ 2.0 loop functions.
 */
void comms_loop_sec2()
{
    if (sw_serial.available())
    {
        uint8_t ser_data = sw_serial.read();
        // spin on receiving data until the whole packet has arrived
        // If we don't have a full packet yet, bail out now.
        if (!reader.push_byte(ser_data))
            return;

        // We have a full packet, process it.
        Packet pkt = Packet(reader.fetch_buf());
        // Log the received packet
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
                current_state = (GarageDoorCurrentState)0xFF;
                break;
            }
            update_door_state(current_state);

            if (pkt.m_data.value.status.light != garage_door.light)
            {
                ESP_LOGI(TAG, "Light: %s (%s)", pkt.m_data.value.status.light ? "On" : "Off", timeString());
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
                ESP_LOGI(TAG, "Remotes lock: %s (%s)", LOCK_STATE(current_lock), timeString());
                notify_homekit_target_lock(target_lock);
                notify_homekit_current_lock(current_lock);
            }

            // Handle obstruction from status packet if pin-based detection not available
            if (!garage_door.pinModeObstructionSensor)
            {
                // Status packet obstruction field is inverted: 1=clear, 0=obstructed
                bool status_obstructed = !pkt.m_data.value.status.obstruction;
                if (garage_door.obstructed != status_obstructed)
                {
                    ESP_LOGI(TAG, "Obstruction: %s (Status packet) (%s)", status_obstructed ? "Obstructed" : "Clear", timeString());
                    notify_homekit_obstruction(status_obstructed);
                    digitalWrite(STATUS_OBST_PIN, !status_obstructed);
                    if (status_obstructed && motionTriggers.bit.obstruction)
                    {
                        notify_homekit_motion(true);
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
                // Send a get status to make sure we are in sync
                send_get_status();
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
                // Send a get status to make sure we are in sync
                send_get_status();
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

        case PacketCommand::Obst1:
        case PacketCommand::Obst2:
        {
            // The messages indicate some movement across the obstruction sensors.
            if (motionTriggers.bit.obstruction)
            {
                notify_homekit_motion(true);
            }
            break;
        }

        case PacketCommand::Pair3Resp:
        {
            // Only use Pair3Resp for obstruction detection if no sensor detected
            if (!garage_door.pinModeObstructionSensor)
            {
                // Use Pair3Resp packets for obstruction detection via parity
                // byte1 9 = clear, byte1 14 = obstructed
                bool currently_obstructed = ((pkt.m_data.value.no_data.no_bits_set >> 16) & 0xFF) == 14;
                // Only update if obstruction state has changed
                if (garage_door.obstructed != currently_obstructed)
                {
                    ESP_LOGI(TAG, "Obstruction: %s (Pair3Resp) (%s)", currently_obstructed ? "Obstructed" : "Clear", timeString());
                    // Notify HomeKit of the state change
                    notify_homekit_obstruction(currently_obstructed);
                    digitalWrite(STATUS_OBST_PIN, !currently_obstructed);
                    // Trigger motion detection if enabled
                    if (currently_obstructed && motionTriggers.bit.obstruction)
                    {
                        notify_homekit_motion(true);
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
    else
    {
        // no incoming data, check if we have command queued
        process_send_queue();
    }

    // Only check if no rolling code operation is in progress to prevent race conditions
    if (!rolling_code_operation_in_progress && rolling_code >= (last_saved_code + MAX_CODES_WITHOUT_FLASH_WRITE))
    {
        save_rolling_code();
    }
}

void comms_loop_drycontact()
{
    static GarageDoorCurrentState previousDoorState = (GarageDoorCurrentState)0xFF;

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

    _millis_t current_millis = _millis();
    // wait for a status command to be processes to properly set the initial state of
    // all homekit characteristics.  Also timeout if we don't receive a status in
    // a reasonable amount of time.  This prevents unintentional state changes if
    // a home hub reads the state before we initialize everything
    // Note, secplus1 doesnt have a status command so it will just timeout
    if (!comms_status_done && (current_millis - comms_status_start > COMMS_STATUS_TIMEOUT))
    {
        ESP_LOGI(TAG, "Comms initial status timeout");
        comms_status_done = true;
    }

#ifdef ESP32
    // Room Occupancy Clear Timer
    if (garage_door.room_occupied && (current_millis > garage_door.room_occupancy_timeout))
    {
        notify_homekit_room_occupancy(false);
        ESP_LOGI(TAG, "Room occupancy cleared after %d minutes", userConfig->getOccupancyDuration() / 60);
    }
#endif
    // Motion Clear Timer
    if (garage_door.motion && garage_door.motion_timer > 0 && (int32_t)(current_millis - garage_door.motion_timer) >= 0)
    {
        notify_homekit_motion(false);
        ESP_LOGI(TAG, "Motion Cleared after %d seconds", MOTION_TIMER_DURATION / 1000);
    }

#ifndef USE_GDOLIB
    if (doorControlType == 1)
        comms_loop_sec1();
    else if (doorControlType == 2)
        comms_loop_sec2();
    else
        comms_loop_drycontact();

    obstruction_timer();
#endif // USE_GDOLIB
}

#ifndef USE_GDOLIB
/**************************** CONTROLLER CODE *******************************
 * SECURITY+1.0
 */
// TRANSMIT SEC+1.0 byte
// PERF: takes aprox 14ms-15ms (including delay(5))
bool transmitSec1(byte toSend)
{
    bool noSend = false;
    bool success = false;

    // safety #1
    if (Sec1Serial.available())
    {
        ESP_LOGD(TAG, "SEC1 TX incoming data detected, cannot send right now");
        noSend = true;
    }
    // safety #2
    if (digitalRead(UART_RX_PIN))
    {
        ESP_LOGD(TAG, "SEC1 TX UART_RX_PIN HIGH detected, cannot send right now");
        noSend = true;
    }
    // safety #3
    if (isRxPending())
    {
        ESP_LOGD(TAG, "SEC1 TX isRxPending detected, cannot send right now");
        noSend = true;
    }

    if (noSend == true)
    {
        clearToSend = false;
        return false;
    }

    // sending a poll (889LM emulation)
    bool poll_cmd = (toSend == secplus1Codes::DoorStatus) || (toSend == secplus1Codes::ObstructionStatus) || (toSend == secplus1Codes::LightLockStatus);
    // one time poll from (889LM emulation) at end of "power up sequence"
    poll_cmd = poll_cmd || (toSend == secplus1Codes::UnknownStatus_0x53);
    // if not a poll command (and polls are only with wall panel emulation enabled),
    // disable disable rx (allows for cleaner tx, and no echo)
    if (!poll_cmd)
    {
        // Use LED to signal activity
        led.flash(FLASH_ACTIVITY_MS);

        // TODO testing without disable
        // disable RX
        // sw_serial.enableRx(false);

        if (!garage_door.wallPanelEmulated)
        {
            // will reconnect in after tx complete + 5ms
            wallPanelConnected = WP_DISCONNECTED;
            digitalWrite(STATUS_DOOR_PIN, wallPanelConnected);
            // ESP_LOGD(TAG, "WP-");
            delay(2);
        }

        ESP_LOGD(TAG, "SEC1 TX 0x%02X (%s)", toSend, SEC1_CMD(toSend));
    }

    // aprox 10ms to write byte
    // every byte we send echos, but want the echo on polls to id the GDO response
    Sec1Serial.write(toSend);
    // timestamp tx
    last_tx = _millis();
    // byte sent
    success = true;

    // this to "confirm" tx byte
    // there is never any issues when sending without a wall panel
    // but all push/release commands need to be read in here(since enableRx is now enabled)
    if (!poll_cmd)
    {
        // read off echo, it is ready right after the write()
        byte echoByte;
        int count = Sec1Serial.readBytes(&echoByte, 1);
        // clear RxPending flag
        isRxPending();
        // check echo
        if (count == 0)
        {
            // LOST THE BYTE COMPLETELY
            ESP_LOGD(TAG, "SEC1 TX LOST ECHO OF: 0x%02X", toSend);
            // success = false;
        }
        else
        {
            // did the received byte match the sent?
            if (echoByte != toSend)
            {
                ESP_LOGD(TAG, "SEC1 TX MISMATCH ECHO OF: tx:0x%02X rx:0x%02X", toSend, echoByte);
                success = false;
            }
            else
            {
                // GOOD ECHO
                ESP_LOGV(TAG, "SEC1 TX ECHO OF: 0x%02X", echoByte);
            }
        }
    }

    // re-enable rx
    if (!poll_cmd)
    {
        // TODO enable RX if disabled above
        // sw_serial.enableRx(true);

        if (!garage_door.wallPanelEmulated)
        {
            // reconnect after tx complete
            delay(2);
            wallPanelConnected = WP_CONNECTED;
            digitalWrite(STATUS_DOOR_PIN, wallPanelConnected);
            // ESP_LOGD(TAG, "WP+");
            // settle
            delay(2);
            // we just connected the panel, if some bits coming in (due to connection), clear RxPending flag & flush
            isRxPending();
            Sec1Serial.flush();
        }
    }

    return success;
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
            led.flash(FLASH_ACTIVITY_MS);
            sw_serial.write(buf, SECPLUS2_CODE_LEN);
            delayMicroseconds(100);
            // timestamp tx
            last_tx = _millis();
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
    if (doorControlType == 2)
    {
        return transmitSec2(pkt_ac);
    }
    else if (pkt_ac.pkt.m_data.value.cmd)
    {
        return transmitSec1(pkt_ac.pkt.m_data.value.cmd);
    }
    return false;
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
    PacketAction pkt_ac = {pkt, true, 0};
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
        data.value.cmd = secplus1Codes::DoorButtonPress;
        data.value.door_action.id = 1;

        Packet pkt = Packet(PacketCommand::DoorAction, data, id_code);
        PacketAction pkt_ac = {pkt, false, 0};
        if (!txQueuePush(&pkt_ac))
        {
            ESP_LOGE(TAG, "packet queue full, dropping door command pressed pkt");
            return;
        }

        /* Removing this section as testing with 398LM (a 0x37 wall panel) was never successful.
        if (is_0x37_panel && (garage_door.current_state == GarageDoorCurrentState::CURR_CLOSED ||
                              garage_door.current_state == GarageDoorCurrentState::CURR_OPEN ||
                              garage_door.current_state == GarageDoorCurrentState::CURR_STOPPED))
        {
            // Anticipate that the door is about to start moving
            door_moving = true;
        }
        */

        // do button release
        pkt_ac.pkt.m_data.value.door_action.pressed = false;
        pkt_ac.pkt.m_data.value.cmd = secplus1Codes::DoorButtonRelease;
        pkt_ac.inc_counter = true;
        if (!txQueuePush(&pkt_ac))
        {
            ESP_LOGE(TAG, "packet queue full, dropping door command release pkt");
            return;
        }

        // if sec+1.0, repeat the release
        if (doorControlType == 1)
        {
            if (!txQueuePush(&pkt_ac))
            {
                ESP_LOGE(TAG, "packet queue full, dropping door command release pkt");
                return;
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
#ifdef USE_GDOLIB
    gdo_door_close();
#else
    if (garage_door.pinModeObstructionSensor)
    {
        door_command(DoorAction::Close);
    }
    else if (garage_door.current_state == GarageDoorCurrentState::CURR_OPEN)
    {
        ESP_LOGD(TAG, "No obstruction sensors detected. Close door using TOGGLE");
        door_command(DoorAction::Toggle);
    }

    if (garage_door.closeDuration > 0)
    {
        // ESPhome firmware starts a timer that fires two seconds after expected close duration
        // It checks that the door got to CLOSED or STOPPED state, and if not then proactively
        // queries the door for status (which only works on Sec+2.0)
        Ticker checkStatus = Ticker();
        checkStatus.once_ms((garage_door.closeDuration + 2) * 1000, []()
                            {
            if (garage_door.current_state != GarageDoorCurrentState::CURR_CLOSED &&
                garage_door.current_state != GarageDoorCurrentState::CURR_STOPPED)
            {
                garage_door.current_state = GarageDoorCurrentState::CURR_CLOSED; // probably missed a status mesage, assume it's closed
                send_get_status();  // query in case we're wrong and it's stopped
            } });
    }
#endif
    return;
}

void door_command_open()
{
#ifdef USE_GDOLIB
    if (doorControlType == 2 && userConfig->getBuiltInTTC())
        gdo_set_time_to_close(0);

    gdo_door_open();
#else
    door_command(DoorAction::Open);

    if (garage_door.openDuration > 0)
    {
        // ESPhome firmware starts a timer that fires two seconds after expected open duration
        // It checks that the door got to OPEN or STOPPED state, and if not then proactively
        // queries the door for status (which only works on Sec+2.0)
        Ticker checkStatus = Ticker();
        checkStatus.once_ms((garage_door.openDuration + 2) * 1000, []()
                            {
            if (garage_door.current_state != GarageDoorCurrentState::CURR_OPEN &&
                garage_door.current_state != GarageDoorCurrentState::CURR_STOPPED)
            {
                garage_door.current_state = GarageDoorCurrentState::CURR_OPEN; // probably missed a status mesage, assume it's open
                send_get_status();  // query in case we're wrong and it's stopped
            } });
    }
#endif
    return;
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
        // Reset last reported to we will update browser with actual state.
        last_reported_garage_door.current_state = (GarageDoorCurrentState)0xFF;
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
    door_command_open();
    return GarageDoorCurrentState::CURR_OPENING;
}

void TTCtimerFn(void (*callback)(), bool light)
{
    if (TTCiterations > 0)
    {
        // dry contact cannot control lights
        if (doorControlType != 3)
        {
            if (light && (TTCiterations % 2 == 0))
            {
#ifndef USE_GDOLIB
                // only SEC+1,0
                if (doorControlType == 1)
                {
                    // just do a press
                    sec1_light_press();
                }
                else
#endif
                {
                    // If light is on, turn it off.  If off, turn it on.
                    set_light((TTCiterations % 4) != 0, false);
                }
            }
        }
#ifdef RATGDO32_DISCO
        tone(BEEPER_PIN, 1300, 125);
#endif
        TTCiterations--;
    }
    else
    {
        TTCtimer.detach();
        ESP_LOGI(TAG, "End of function delay timer");
#ifndef USE_GDOLIB
        // only SEC+1,0
        if (doorControlType == 1)
        {
            // sec1_light_release(2, 250);
            sec1_light_release(4);
        }
#endif
        // delay so that set_light() can do its thing
        callbackDelay.once_ms(TTCinterval * 2, [callback]()
                              {
#ifdef ESP8266
                                  schedule_recurrent_function_us([callback]()
                                                                 {
                                                                     if (callback == sync_and_restart)
                                                                         ESP_LOGI(TAG, "Calling delayed function: sync_and_restart()");
                                                                     else if (callback == door_command_close)
                                                                         ESP_LOGI(TAG, "Calling delayed function: door_command_close()");
                                                                     else
                                                                         ESP_LOGI(TAG, "Calling delayed function at: 0x%08lX", (uint32_t)callback);

                                                                     if (callback)
                                                                         callback();
                                                                     return false; // run the fn only once
                                                                 },
                                                                 0); // zero micro seconds (run asap)
#else
                                  if (callback == sync_and_restart)
                                      ESP_LOGI(TAG, "Calling delayed function: sync_and_restart()");
                                  else if (callback == door_command_close)
                                      ESP_LOGI(TAG, "Calling delayed function: door_command_close()");
                                  else
                                      ESP_LOGI(TAG, "Calling delayed function at: 0x%08lX", (uint32_t)callback);

                                  if (callback)
                                      callback();
#endif
                              });
    }
}

// Call function after ms milliseconds during which we flash and beep
void delayFnCall(uint32_t ms, void (*callback)())
{
    bool light = userConfig->getTTClight(); // Whether to flash light during delay

    TTCtimer.detach();                 // Terminate existing timer if any
    TTCiterations = ms / TTCinterval;  // Number of times to go through loop
    TTCwasLightOn = garage_door.light; // Current state of light
    ESP_LOGI(TAG, "Start function delay timer for %lums (%d iterations)", ms, TTCiterations);
    TTCtimer.attach_ms(TTCinterval, [callback, light]()
                       {
#ifdef ESP8266
                           schedule_recurrent_function_us([callback, light]()
                                                          {
                                                              TTCtimerFn(callback, light);
                                                              return false; // run the fn only once
                                                          },
                                                          0); // zero micro seconds (run asap)
#else
                           TTCtimerFn(callback, light);
#endif
                       });
}

GarageDoorCurrentState close_door()
{
    if (garage_door.current_state == GarageDoorCurrentState::CURR_CLOSED)
    {
        ESP_LOGI(TAG, "Door already closed; ignored request");
        // Reset last reported to we will update browser with actual state.
        last_reported_garage_door.current_state = (GarageDoorCurrentState)0xFF;
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
    PacketAction pkt_ac = {pkt, true, 0};
    if (!txQueuePush(&pkt_ac))
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
    PacketAction pkt_ac = {pkt, true, 0};
    if (!txQueuePush(&pkt_ac))
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
        // Reset last reported to we will update browser with actual state.
        last_reported_garage_door.current_lock = (LockCurrentState)0xFF;
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
#else  // USE_GDOLIB
bool set_lock(bool value, bool verify)
{
    // return value: true = lock state changed, else state unchanged
    if (verify && (garage_door.current_lock == ((value) ? LockCurrentState::CURR_LOCKED : LockCurrentState::CURR_UNLOCKED)))
    {
        ESP_LOGI(TAG, "Remote locks already %s; ignored request", (value) ? "locked" : "unlocked");
        // Reset last reported to we will update browser with actual state.
        last_reported_garage_door.current_lock = (LockCurrentState)0xFF;
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
        data.value.cmd = secplus1Codes::LockButtonPress;
        Packet pkt = Packet(PacketCommand::Lock, data, id_code);
        PacketAction pkt_ac = {pkt, true, 0};

        if (!txQueuePush(&pkt_ac))
        {
            ESP_LOGE(TAG, "packet queue full, dropping lock pkt");
            return false;
        }
        // button release
        pkt_ac.pkt.m_data.value.lock.pressed = false;
        pkt_ac.pkt.m_data.value.cmd = secplus1Codes::LockButtonRelease;
        if (!txQueuePush(&pkt_ac))
        {
            ESP_LOGE(TAG, "packet queue full, dropping lock pkt");
            return false;
        }
        // repeat the release
        if (!txQueuePush(&pkt_ac))
        {
            ESP_LOGE(TAG, "packet queue full, dropping lock pkt");
            return false;
        }
    }
    // SECURITY2.0
    else
    {
        Packet pkt = Packet(PacketCommand::Lock, data, id_code);
        PacketAction pkt_ac = {pkt, true, 0};
        if (!txQueuePush(&pkt_ac))
        {
            ESP_LOGE(TAG, "packet queue full, dropping lock pkt");
            return false;
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
    // Reset last reported to we will update browser with actual state.
    last_reported_garage_door.light = !value;
    return true;
}
#else

void sec1_light_press(uint32_t delay)
{
    PacketData data;
    data.type = PacketDataType::Light;
    data.value.light.light = LightState::On;
    data.value.light.pressed = true;
    data.value.cmd = secplus1Codes::LightButtonPress;
    Packet pkt = Packet(PacketCommand::Light, data, id_code);
    PacketAction pkt_ac = {pkt, true, delay};

    if (!txQueuePush(&pkt_ac))
    {
        ESP_LOGE(TAG, "packet queue full, dropping light press pkt");
        return;
    }
    // this better emulates wall panel
    if (garage_door.wallPanelEmulated)
    {
        sec1_poll_status(secplus1Codes::LightLockStatus);
    }
}

void sec1_light_release(uint8_t howManyReleases, uint32_t delay)
{
    PacketData data;
    data.type = PacketDataType::Light;
    data.value.light.light = LightState::On;
    data.value.light.pressed = false;
    data.value.cmd = secplus1Codes::LightButtonRelease;
    Packet pkt = Packet(PacketCommand::Light, data, id_code);
    PacketAction pkt_ac = {pkt, true, delay};

    for (int numReleases = 0; numReleases < std::max(2, (int)howManyReleases); numReleases++)
    {
        if (!txQueuePush(&pkt_ac))
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
        // Reset last reported to we will update browser with actual state.
        last_reported_garage_door.light = !value;
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
        PacketAction pkt_ac = {pkt, true, 0};
        if (!txQueuePush(&pkt_ac))
        {
            ESP_LOGE(TAG, "packet queue full, dropping light pkt");
            return false;
        }
        if (verify)
            send_get_status();
    }
    return true;
}
#endif // USE_GDOLIB

void manual_recovery()
{
    if (!force_recover.enable)
        return;

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
            if (!garage_door.pinModeObstructionSensor)
            {
                garage_door.pinModeObstructionSensor = true;
                ESP_LOGI(TAG, "Pin-based obstruction detection active");
            }

            // Only update if we are changing state
            if (garage_door.obstructed)
            {
                ESP_LOGI(TAG, "Obstruction: Clear (ISR) (%s)", timeString());
                notify_homekit_obstruction(false);
                digitalWrite(STATUS_OBST_PIN, HIGH);
            }
        }
        else if (pulse_count == 0)
        {
#if defined(ESP8266) || defined(GRGDO1_V2)
            // LOW?
            if (!digitalRead(INPUT_OBST_PIN))
#else
            // HIGH? (pin inversion on RATDGO32/RATDGO32 DISCO (ESP32))
            if (digitalRead(INPUT_OBST_PIN))
#endif
            {
                // likely asleep
                obstruction_sensor.last_asleep = current_millis;
                obstruction_sensor.pin_ever_changed = true;
            }
            else
            {
                // was last asleep more than 700ms ago, then there is an obstruction present
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
                        ESP_LOGI(TAG, "Obstruction: Detected (ISR) (%s)", timeString());
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
