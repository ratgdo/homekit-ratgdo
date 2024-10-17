
#include <user_interface.h>
#include <LittleFS.h>

#include "ratgdo.h"
#include "wifi.h"
#include "homekit.h"
#include "comms.h"
#include "log.h"
#include "web.h"
#include "utilities.h"

/********************************* FWD DECLARATIONS *****************************************/

void setup_pins();
void IRAM_ATTR isr_obstruction();
void service_timer_loop();

/********************************* RUNTIME STORAGE *****************************************/

struct obstruction_sensor_t
{
    unsigned int low_count = 0;    // count obstruction low pulses
    unsigned long last_asleep = 0; // count time between high pulses from the obst ISR
} obstruction_sensor;

// long unsigned int led_reset_time = 0; // Stores time when LED should return to idle state
// uint8_t led_active_state = LOW;       // LOW == LED on, HIGH == LED off
// uint8_t led_idle_state = HIGH;        // opposite of active
LED led;

extern bool flashCRC;

struct GarageDoor garage_door;
struct ForceRecover force_recover;

extern "C" uint32_t __crc_len;
extern "C" uint32_t __crc_val;

// Track our memory usage
uint32_t free_heap = 65535;
uint32_t min_heap = 65535;
unsigned long next_heap_check = 0;

/********************************** MAIN LOOP CODE *****************************************/

void setup()
{
    disable_extra4k_at_link_time();
    flashCRC = ESP.checkFlashCRC();
    Serial.begin(115200);
    LittleFS.begin();

    Serial.printf("\n"); // newline before we start
    led = LED();
    RINFO("=== Starting RATGDO Homekit version %s", AUTO_VERSION);
    RINFO("%s", ESP.getFullVersion().c_str());
    RINFO("Flash chip size 0x%X", ESP.getFlashChipSize());
    RINFO("Flash chip mode 0x%X", ESP.getFlashChipMode());
    RINFO("Flash chip speed 0x%X (%d MHz)", ESP.getFlashChipSpeed(), ESP.getFlashChipSpeed() / 1000000);
    // CRC checking starts at memory location 0x40200000, and proceeds until the address of __crc_len and __crc_val...
    // For CRC calculation purposes, those two long (32 bit) values are assumed to be zero.
    // The CRC calculation then proceeds until it get to 0x4020000 plus __crc_len.
    // Any memory writes/corruption within these blocks will cause checkFlashCRC() to fail.
    RINFO("Firmware CRC value: 0x%08X, CRC length: 0x%X (%d), Memory address of __crc_len,__crc_val: 0x%08X,0x%08X", __crc_val, __crc_len, __crc_len, &__crc_len, &__crc_val);
    if (flashCRC)
    {
        RINFO("checkFlashCRC: true");
    }
    else
    {
        RERROR("checkFlashCRC: false");
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

#ifdef NTP_CLIENT
    if (enableNTP)
    {
        timeClient.begin();
    }
#endif

    led.idle();
    RINFO("=== RATGDO setup complete ===");
    RINFO("=============================");
}

void loop()
{

    improv_loop();
    comms_loop();
    homekit_loop();
    service_timer_loop();
    web_loop();
}

/*********************************** HELPER FUNCTIONS **************************************/

void setup_pins()
{
    RINFO("Setting up pins");

    pinMode(UART_TX_PIN, OUTPUT);
    pinMode(UART_RX_PIN, INPUT_PULLUP);

    pinMode(INPUT_OBST_PIN, INPUT);

    /*
     * TODO add support for dry contact switches
    pinMode(STATUS_DOOR_PIN, OUTPUT);
    */
    pinMode(STATUS_OBST_PIN, OUTPUT);
    /*
    pinMode(DRY_CONTACT_OPEN_PIN, INPUT_PULLUP);
    pinMode(DRY_CONTACT_CLOSE_PIN, INPUT_PULLUP);
    pinMode(DRY_CONTACT_LIGHT_PIN, INPUT_PULLUP);
    */

    /* pin-based obstruction detection
    // FALLING from https://github.com/ratgdo/esphome-ratgdo/blob/e248c705c5342e99201de272cb3e6dc0607a0f84/components/ratgdo/ratgdo.cpp#L54C14-L54C14
     */
    attachInterrupt(INPUT_OBST_PIN, isr_obstruction, FALLING);
}

/*********************************** MODEL **************************************/

/*************************** OBSTRUCTION DETECTION ***************************/
void IRAM_ATTR isr_obstruction()
{
    obstruction_sensor.low_count++;
}

void obstruction_timer()
{
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
        // check to see if we got more then PULSES_LOWER_LIMIT pulses
        if (obstruction_sensor.low_count > PULSES_LOWER_LIMIT)
        {
            // Only update if we are changing state
            if (garage_door.obstructed)
            {
                RINFO("Obstruction Clear");
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
        else if (obstruction_sensor.low_count == 0)
        {
            // if there have been no pulses the line is steady high or low
            if (!digitalRead(INPUT_OBST_PIN))
            {
                // asleep
                obstruction_sensor.last_asleep = current_millis;
            }
            else
            {
                // if the line is high and was last asleep more than 700ms ago, then there is an obstruction present
                if (current_millis - obstruction_sensor.last_asleep > 700)
                {
                    // Only update if we are changing state
                    if (!garage_door.obstructed)
                    {
                        RINFO("Obstruction Detected");
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
        obstruction_sensor.low_count = 0;
    }
}

void service_timer_loop()
{
    // Service the Obstruction Timer
    obstruction_timer();

    unsigned long current_millis = millis();

#ifdef NTP_CLIENT
    if (enableNTP)
    {
        timeClient.update();
        if (lastRebootAt == 0 && timeClient.isTimeSet())
        {
            lastRebootAt = timeClient.getEpochTime() - (current_millis / 1000);
            RINFO("Current time: %s", timeString());
        }
    }
#endif
    // LED flash timer
    led.flash();

    // Motion Clear Timer
    if (garage_door.motion && (current_millis > garage_door.motion_timer))
    {
        RINFO("Motion Cleared");
        garage_door.motion = false;
        notify_homekit_motion();
    }

    // Check heap
    if (current_millis > next_heap_check)
    {
        next_heap_check = current_millis + 1000;
        free_heap = ESP.getFreeHeap();
        if (free_heap < min_heap)
        {
            min_heap = free_heap;
            RINFO("Free heap dropped to %d", min_heap);
        }
    }

    if (force_recover.push_count && (current_millis > force_recover.timeout)) {
        force_recover.push_count = 0;
    }
    if (force_recover.push_count >= 5) {
        RINFO("Request to boot into soft access point mode");
        write_int_to_file(softAPmodeFile, 1);
        delay(500);
        sync_and_restart();
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
    digitalWrite(LED_BUILTIN, LOW);
}

void LED::off()
{
    digitalWrite(LED_BUILTIN, HIGH);
}

void LED::idle()
{
    digitalWrite(LED_BUILTIN, idleState);
}

void LED::setIdleState(uint8_t state)
{
    idleState = state;
    activeState = (state == HIGH) ? LOW : HIGH;
}

void LED::flash(unsigned long ms)
{
    if (ms)
    {
        digitalWrite(LED_BUILTIN, activeState);
        resetTime = millis() + ms;
    }
    else if ((digitalRead(LED_BUILTIN) == activeState) && (millis() > resetTime))
    {
        digitalWrite(LED_BUILTIN, idleState);
    }
}
