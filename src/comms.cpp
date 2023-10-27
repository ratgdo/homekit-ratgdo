#include "SoftwareSerial.h"
#include "ratgdo.h"
#include "homekit_debug.h"

SoftwareSerial sw_serial;
extern struct GarageDoor garage_door;

// TODO REMOVE ME (see notes in open_door and close_door)
#include <arduino_homekit_server.h>
#include "homekit_decl.h"
extern "C" homekit_characteristic_t current_door_state;
// END REMOVE ME

/********************************** MAIN LOOP CODE *****************************************/

void setup_comms() {
    INFO("Setting up comms");

    // TODO uncomment when adding comms
    // sw_serial.begin(9600, SWSERIAL_8N1, UART_RX_PIN, UART_TX_PIN, true);
    // sw_serial.enableIntTx(false);
    // sw_serial.enableAutoBaud(true); // found in ratgdo/espsoftwareserial branch autobaud
}

void comms_loop() {
    // TODO read from sw_serial and notify the characteristic
    //
    // NOTE I'm still not entirely clear on what messages are sent by the GDO, but this should probably be something like:
    // * target state characteristic is set in homekit
    // * motor starts moving, which causes current state to be set
    //   * if target = OPEN, motor start causes current state OPENING
    //   * if target = CLOSED, motor start causes current state CLOSING
    // * motor stops or open/close message is received
    //   * causes current state OPEN or CLOSED
    // what happens then the door is stopped partway?
}

void open_door() {
    INFO("TODO: open door\n");

    // NOTE the following fakes the response to the action, but this should be
    // removed in the "real" code. When the comms are correctly wired, the door
    // should open and the controller should eventually notify us that the
    // state is now OPEN. That notification, handled above in `comms_loop`, is
    // responsible for updating the state. This fakes it.
    garage_door.current_state = CURR_OPEN;
    homekit_characteristic_notify(
        &current_door_state,
        HOMEKIT_UINT8_CPP(HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_OPEN)
    );
}

void close_door() {
    INFO("TODO: close door\n");

    // NOTE the following fakes the response to the action, but this should be
    // removed in the "real" code. When the comms are correctly wired, the door
    // should close, and the controller should eventually notify us that the
    // state is now CLOSED. That notification, handled above in `comms_loop`,
    // is responsible for updating the state. This fakes it.
    garage_door.current_state = CURR_CLOSED;
    homekit_characteristic_notify(
        &current_door_state,
        HOMEKIT_UINT8_CPP(HOMEKIT_CHARACTERISTIC_CURRENT_DOOR_STATE_CLOSED)
);
}
