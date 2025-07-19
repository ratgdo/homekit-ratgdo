
#include <user_interface.h>
#include <LittleFS.h>

#include "ratgdo.h"
#include "wifi.h"
#include "homekit.h"
#include "comms.h"
#include "log.h"
#include "web.h"
#include "utilities.h"
#include "Packet.h"

#include <time.h>
#include <coredecls.h>

// Logger tag
static const char *TAG = "ratgdo-main";

time_t now = 0;
tm timeInfo;

void showTime()
{
    localtime_r(&now, &timeInfo); // update the structure tm with the current time
    Serial.print("year:");
    Serial.print(timeInfo.tm_year + 1900); // years since 1900
    Serial.print("\tmonth:");
    Serial.print(timeInfo.tm_mon + 1); // January = 0 (!)
    Serial.print("\tday:");
    Serial.print(timeInfo.tm_mday); // day of month
    Serial.print("\thour:");
    Serial.print(timeInfo.tm_hour); // hours since midnight  0-23
    Serial.print("\tmin:");
    Serial.print(timeInfo.tm_min); // minutes after the hour  0-59
    Serial.print("\tsec:");
    Serial.print(timeInfo.tm_sec); // seconds after the minute  0-61*
    Serial.print("\twday");
    Serial.print(timeInfo.tm_wday); // days since Sunday 0-6
    if (timeInfo.tm_isdst == 1)     // Daylight Saving Time flag
        Serial.print("\tDST");
    else
        Serial.print("\tstandard");
    Serial.println();
}

/********************************* FWD DECLARATIONS *****************************************/

void setup_pins();
void IRAM_ATTR isr_obstruction();
void service_timer_loop();
void dryContactLoop();
void onOpenSwitchPress();
void onCloseSwitchPress();
void onOpenSwitchRelease();
void onCloseSwitchRelease();

// Define OneButton objects for open/close pins
OneButton buttonOpen(DRY_CONTACT_OPEN_PIN, true, true); // Active low, with internal pull-up
OneButton buttonClose(DRY_CONTACT_CLOSE_PIN, true, true);
bool dryContactDoorOpen = false;
bool dryContactDoorClose = false;
bool previousDryContactDoorOpen = false;
bool previousDryContactDoorClose = false;

/********************************* RUNTIME STORAGE *****************************************/

struct obstruction_sensor_t
{
    unsigned int low_count = 0;    // count obstruction low pulses
    unsigned long last_asleep = 0; // count time between high pulses from the obst ISR
    bool pin_ever_changed = false; // track if pin has ever changed from initial state
} obstruction_sensor;

// long unsigned int led_reset_time = 0; // Stores time when LED should return to idle state
// uint8_t led_active_state = LOW;       // LOW == LED on, HIGH == LED off
// uint8_t led_idle_state = HIGH;        // opposite of active
LED led;

uint8_t loop_id;

extern bool flashCRC;

struct GarageDoor garage_door;

extern "C" uint32_t __crc_len;
extern "C" uint32_t __crc_val;

// Track our memory usage
uint32_t free_heap = 65535;
uint32_t min_heap = 65535;
#ifdef MMU_IRAM_HEAP
uint32_t free_iram = 65535;
uint32_t min_iram = 65535;
#endif

bool status_done = false;
unsigned long status_start = 0;

/********************************** MAIN LOOP CODE *****************************************/

void setup()
{
    disable_extra4k_at_link_time();
    Serial.begin(115200);
    flashCRC = ESP.checkFlashCRC();
    LittleFS.begin();

    while (!Serial)
        ; // Wait for serial port to open
    Serial.printf("\n\n\n=== R A T G D O ===\n");
    led = LED();

    ESP_LOGI(TAG, "=== Starting RATGDO Homekit version %s", AUTO_VERSION);
    ESP_LOGI(TAG, "%s", ESP.getFullVersion().c_str());
    ESP_LOGI(TAG, "Flash chip size 0x%X", ESP.getFlashChipSize());
    ESP_LOGI(TAG, "Flash chip mode 0x%X", ESP.getFlashChipMode());
    ESP_LOGI(TAG, "Flash chip speed 0x%X (%d MHz)", ESP.getFlashChipSpeed(), ESP.getFlashChipSpeed() / 1000000);
    // CRC checking starts at memory location 0x40200000, and proceeds until the address of __crc_len and __crc_val...
    // For CRC calculation purposes, those two long (32 bit) values are assumed to be zero.
    // The CRC calculation then proceeds until it get to 0x4020000 plus __crc_len.
    // Any memory writes/corruption within these blocks will cause checkFlashCRC() to fail.
    ESP_LOGI(TAG, "Firmware CRC value: 0x%08X, CRC length: 0x%X (%d), Memory address of __crc_len,__crc_val: 0x%08X,0x%08X", __crc_val, __crc_len, __crc_len, &__crc_len, &__crc_val);
    if (flashCRC)
    {
        ESP_LOGI(TAG, "checkFlashCRC: true");
    }
    else
    {
        ESP_LOGE(TAG, "checkFlashCRC: false");
    }
    load_all_config_settings();
    wifi_connect();
    setup_web();
    if (!softAPmode)
    {
        setup_pins();
        setup_comms();
        setup_homekit();
    }

    led.idle();
    ESP_LOGI(TAG, "=== RATGDO setup complete ===");
    ESP_LOGI(TAG, "=============================");
    status_start = millis();
}

void loop()
{
    improv_loop();
    comms_loop();
    // Poll OneButton objects
    buttonOpen.tick();
    buttonClose.tick();

    // wait for a status command to be processes to properly set the initial state of
    // all homekit characteristics.  Also timeout if we don't receive a status in
    // a reasonable amount of time.  This prevents unintentional state changes if
    // a home hub reads the state before we initialize everything
    // Note, secplus1 doesnt have a status command so it will just timeout
    if (status_done)
    {
        homekit_loop();
    }
    else if (millis() - status_start > 2000)
    {
        ESP_LOGI(TAG, "Status timeout, starting homekit");
        status_done = true;
    }
    service_timer_loop();
    web_loop();
    dryContactLoop();
    loop_id = LOOP_SYSTEM;
}

/*********************************** HELPER FUNCTIONS **************************************/

void setup_pins()
{
    ESP_LOGI(TAG, "Setting up pins");

    pinMode(UART_TX_PIN, OUTPUT);
    pinMode(UART_RX_PIN, INPUT_PULLUP);

    pinMode(INPUT_OBST_PIN, INPUT);

    pinMode(STATUS_DOOR_PIN, OUTPUT);

    pinMode(STATUS_OBST_PIN, OUTPUT);

    pinMode(DRY_CONTACT_OPEN_PIN, INPUT_PULLUP);
    pinMode(DRY_CONTACT_CLOSE_PIN, INPUT_PULLUP);

    // Attach OneButton handlers
    buttonOpen.attachPress(onOpenSwitchPress);
    buttonClose.attachPress(onCloseSwitchPress);
    buttonOpen.attachLongPressStop(onOpenSwitchRelease);
    buttonClose.attachLongPressStop(onCloseSwitchRelease);
    /* pin-based obstruction detection
    // FALLING from https://github.com/ratgdo/esphome-ratgdo/blob/e248c705c5342e99201de272cb3e6dc0607a0f84/components/ratgdo/ratgdo.cpp#L54C14-L54C14
     */
    attachInterrupt(INPUT_OBST_PIN, isr_obstruction, FALLING);
}

/*********************************** MODEL **************************************/

/*************************** DRY CONTACT CONTROL OF LIGHT & DOOR ***************************/

// Functions for sensing GDO open/closed
void onOpenSwitchPress()
{
    dryContactDoorOpen = true;
    ESP_LOGI(TAG, "Open switch pressed");
}

void onCloseSwitchPress()
{
    dryContactDoorClose = true;
    ESP_LOGI(TAG, "Close switch pressed");
}

void onOpenSwitchRelease()
{
    dryContactDoorOpen = false;
    ESP_LOGI(TAG, "Open switch released");
}

void onCloseSwitchRelease()
{
    dryContactDoorClose = false;
    ESP_LOGI(TAG, "Close switch released");
}

// handle changes to the dry contact state
void dryContactLoop()
{

    if (dryContactDoorOpen)
    {
        if (userConfig->gdoSecurityType == 3)
        {
            doorState = DoorState::Open;
        }
        else
        {
            Serial.println("Dry Contact: open the door");
            open_door();
            dryContactDoorOpen = false;
        }
    }

    if (dryContactDoorClose)
    {
        if (userConfig->gdoSecurityType == 3)
        {
            doorState = DoorState::Closed;
        }
        else
        {
            Serial.println("Dry Contact: close the door");
            close_door();
            dryContactDoorClose = false;
        }
    }

    if (userConfig->gdoSecurityType == 3)
    {
        if (!dryContactDoorClose && !dryContactDoorOpen)
        {
            if (previousDryContactDoorClose)
            {
                doorState = DoorState::Opening;
            }
            else if (previousDryContactDoorOpen)
            {
                doorState = DoorState::Closing;
            }
        }

        if (previousDryContactDoorOpen != dryContactDoorOpen)
        {
            previousDryContactDoorOpen = dryContactDoorOpen;
        }
        if (previousDryContactDoorClose != dryContactDoorClose)
        {
            previousDryContactDoorClose = dryContactDoorClose;
        }
    }
}

/*************************** OBSTRUCTION DETECTION ***************************/
void IRAM_ATTR isr_obstruction()
{
    obstruction_sensor.low_count++;
}

// Track if we've detected a working obstruction sensor
bool obstruction_sensor_detected = false; // Make it globally accessible for comms.cpp

void obstruction_timer()
{
    // Always try pin-based detection

    unsigned long current_millis = millis();
    static unsigned long last_millis = 0;

    // the obstruction sensor has 3 states: clear (HIGH with LOW pulse every 7ms), obstructed (HIGH), asleep (LOW)
    // the transitions between awake and asleep are tricky because the voltage drops slowly when falling asleep
    // and is high without pulses when waking up

    // If at least 3 low pulses are counted within 50ms, the door is awake, not obstructed and we don't have to check anything else

    const long CHECK_PERIOD = 50;
    const long PULSES_LOWER_LIMIT = 3;
    if (current_millis - last_millis > CHECK_PERIOD)
    {
        // Atomically read and reset the pulse count to prevent race with ISR
        noInterrupts();
        unsigned int pulse_count = obstruction_sensor.low_count;
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
                garage_door.obstructed = false;
                notify_homekit_obstruction();
                digitalWrite(STATUS_OBST_PIN, garage_door.obstructed);
                if (motionTriggers.bit.obstruction)
                {
                    garage_door.motion = false;
                    notify_homekit_motion();
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
                if (current_millis - obstruction_sensor.last_asleep > 700)
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
                        garage_door.obstructed = true;
                        notify_homekit_obstruction();
                        digitalWrite(STATUS_OBST_PIN, garage_door.obstructed);
                        if (motionTriggers.bit.obstruction)
                        {
                            garage_door.motion = true;
                            notify_homekit_motion();
                        }
                    }
                }
            }
        }

        last_millis = current_millis;
    }
}

void service_timer_loop()
{
    loop_id = LOOP_TIMER;
    // Service the Obstruction Timer
    obstruction_timer();

    unsigned long current_millis = millis();

#ifdef NTP_CLIENT
    if (enableNTP && clockSet && lastRebootAt == 0)
    {
        lastRebootAt = time(NULL) - (current_millis / 1000);
        ESP_LOGI(TAG, "System boot time: %s", timeString(lastRebootAt));
    }
#endif

    // LED flash timer
    led.flash();

    // Motion Clear Timer
    if (garage_door.motion && garage_door.motion_timer > 0 && (int32_t)(current_millis - garage_door.motion_timer) >= 0)
    {
        ESP_LOGI(TAG, "Motion Cleared");
        garage_door.motion = false;
        notify_homekit_motion();
    }

    // Check heap (both regular and IRAM)
    static unsigned long last_heap_check = 0;
    if (current_millis - last_heap_check >= 1000)
    {
        last_heap_check = current_millis;
        free_heap = ESP.getFreeHeap();
        if (free_heap < min_heap)
        {
            min_heap = free_heap;
            ESP_LOGI(TAG, "Free heap dropped to %d", min_heap);
        }

#ifdef MMU_IRAM_HEAP
        // Also track IRAM heap usage
        {
            HeapSelectIram ephemeral;
            free_iram = ESP.getFreeHeap();
            if (free_iram < min_iram)
            {
                min_iram = free_iram;
                ESP_LOGI(TAG, "Free IRAM heap dropped to %d", min_iram);
            }
        }
#endif
    }
}

// Constructor for LED class
LED::LED()
{
    if (UART_TX_PIN != LED_BUILTIN)
    {
        // Serial.printf("Enabling built-in LED object\n");
        pinMode(LED_BUILTIN, OUTPUT);
        on();
    }
}

void LED::on()
{
    digitalWrite(LED_BUILTIN, 0);
}

void LED::off()
{
    digitalWrite(LED_BUILTIN, 1);
}

void LED::idle()
{
    digitalWrite(LED_BUILTIN, idleState);
}

void LED::setIdleState(uint8_t state)
{
    // 0 = LED flashes off (idle is on)
    // 1 = LED flashes on (idle is off)
    // 3 = LED disabled (active and idle both off)
    if (state == 2)
    {
        idleState = activeState = 1;
    }
    else
    {
        idleState = state;
        activeState = (state == 1) ? 0 : 1;
    }
}

void LED::flash(unsigned long ms)
{
    if (ms)
    {
        digitalWrite(LED_BUILTIN, activeState);
        resetTime = millis() + ms;
    }
    else if ((digitalRead(LED_BUILTIN) == activeState) && resetTime > 0 && (int32_t)(millis() - resetTime) >= 0)
    {
        digitalWrite(LED_BUILTIN, idleState);
    }
}
