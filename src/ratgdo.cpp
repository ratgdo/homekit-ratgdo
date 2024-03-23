
#include <Arduino.h>
#include "LittleFS.h"

#include "ratgdo.h"
#include "wifi.h"
#include "homekit.h"
#include "comms.h"
#include "log.h"
#include "web.h"

/********************************* FWD DECLARATIONS *****************************************/

void setup_pins();
void IRAM_ATTR isr_obstruction();
void service_timer_loop();

/********************************* RUNTIME STORAGE *****************************************/

struct obstruction_sensor_t {
    unsigned int low_count = 0;        // count obstruction low pulses
    bool detected = false;
    unsigned long last_high = 0;       // count time between high pulses from the obst ISR
} obstruction_sensor;


long unsigned int led_on_time = 0;     // Stores time when LED should turn back on

/********************************** MAIN LOOP CODE *****************************************/

void setup() {
    disable_extra4k_at_link_time();
    Serial.begin(115200);
    LittleFS.begin();

    wifi_connect();

    setup_pins();

    setup_comms();

    setup_homekit();

    setup_web();

    RINFO("RATGDO setup completed");
    RINFO("Starting RATGDO Homekit version %s", AUTO_VERSION);
    RINFO("%s", ESP.getFullVersion().c_str());
}

void loop() {

    improv_loop();

    comms_loop();

    homekit_loop();

    web_loop();

    service_timer_loop();
}

/*********************************** HELPER FUNCTIONS **************************************/

void setup_pins() {
    RINFO("Setting up pins");

    if (UART_TX_PIN != LED_BUILTIN) {
        RINFO("enabling built-in LED");
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, LOW);
    }

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

struct GarageDoor garage_door;

/*************************** OBSTRUCTION DETECTION ***************************/
void IRAM_ATTR isr_obstruction() {
    if (digitalRead(INPUT_OBST_PIN)) {
        obstruction_sensor.last_high = millis();
    } else {
        obstruction_sensor.detected = true;
        obstruction_sensor.low_count++;
    }
}

void obstruction_timer() {
    if (!obstruction_sensor.detected)
        return;
    unsigned long current_millis = millis();
    static unsigned long last_millis = 0;

    // the obstruction sensor has 3 states: clear (HIGH with LOW pulse every 7ms), obstructed (HIGH), asleep (LOW)
    // the transitions between awake and asleep are tricky because the voltage drops slowly when falling asleep
    // and is high without pulses when waking up

    // If at least 3 low pulses are counted within 50ms, the door is awake, not obstructed and we don't have to check anything else

    // Every 50ms
    if (current_millis - last_millis > 50) {
        // check to see if we got between 3 and 8 low pulses on the line
        if (obstruction_sensor.low_count >= 3 && obstruction_sensor.low_count <= 8) {
            // Only update if we are changing state
            if (garage_door.obstructed) {
                RINFO("Obstruction Clear");
                garage_door.obstructed = false;
                notify_homekit_obstruction();
                digitalWrite(STATUS_OBST_PIN,garage_door.obstructed);
            }

            // if there have been no pulses the line is steady high or low
        } else if (obstruction_sensor.low_count == 0) {
            // if the line is high and the last high pulse was more than 70ms ago, then there is an obstruction present
            if (digitalRead(INPUT_OBST_PIN) && current_millis - obstruction_sensor.last_high > 70) {
                // Only update if we are changing state
                if (!garage_door.obstructed) {
                    RINFO("Obstruction Detected");
                    garage_door.obstructed = true;
                    notify_homekit_obstruction();
                    digitalWrite(STATUS_OBST_PIN,garage_door.obstructed);
                }
            }
        }

        last_millis = current_millis;
        obstruction_sensor.low_count = 0;
    }
}

void service_timer_loop() {
    // Service the Obstruction Timer
    obstruction_timer();

    unsigned long current_millis = millis();

    // LED Timer
    if (digitalRead(LED_BUILTIN) && (current_millis > led_on_time)) {
        digitalWrite(LED_BUILTIN, LOW);
    }

    // Motion Clear Timer
    if (garage_door.motion && (current_millis > garage_door.motion_timer)) {
        RINFO("Motion Cleared");
        garage_door.motion = false;
        notify_homekit_motion();
    }
}
