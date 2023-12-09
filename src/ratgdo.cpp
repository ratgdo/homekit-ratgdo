
#include <Arduino.h>

#include "ratgdo.h"
#include "wifi.h"
#include "homekit.h"
#include "comms.h"
#include "log.h"
#include "web.h"

/********************************* FWD DECLARATIONS *****************************************/

void setup_pins();

/********************************* RUNTIME STORAGE *****************************************/

// nothing yet

/********************************** MAIN LOOP CODE *****************************************/

void setup() {
    Serial.begin(115200);

    wifi_connect();

    setup_homekit();

    setup_pins();

    setup_comms();

    setup_web();

    RINFO("RATGDO setup completed");
    RINFO("Starting RATGDO Homekit version %s", AUTO_VERSION);
    RINFO("%s", ESP.getFullVersion());
}

void loop() {

    improv_loop();

    homekit_loop();

    comms_loop();

    web_loop();
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

    /* TODO add support for pin-based obstruction detection
     *
     * until then, only report obstruction based on status
    pinMode(INPUT_OBST_PIN, INPUT);
     */

    /*
     * TODO add support for dry contact switches
    pinMode(STATUS_DOOR_PIN, OUTPUT);
    pinMode(STATUS_OBSTRUCTION_PIN, OUTPUT);
    pinMode(DRY_CONTACT_OPEN_PIN, INPUT_PULLUP);
    pinMode(DRY_CONTACT_CLOSE_PIN, INPUT_PULLUP);
    pinMode(DRY_CONTACT_LIGHT_PIN, INPUT_PULLUP);
    */

    /* TODO add support for pin-based obstruction detection
    // FALLING from https://github.com/ratgdo/esphome-ratgdo/blob/e248c705c5342e99201de272cb3e6dc0607a0f84/components/ratgdo/ratgdo.cpp#L54C14-L54C14
    attachInterrupt(INPUT_OBST_PIN, isr_obstruction, FALLING);
     */
}

/*********************************** MODEL **************************************/

struct GarageDoor garage_door;
