#include "SoftwareSerial.h"
#include "ratgdo.h"
#include "homekit_debug.h"
#include "LittleFS.h"
#include "secplus.h"
#include "homekit.h"

#include "Decoder.h"
#include "Reader.h"
#include "secplus2.h"

/*************************** FORWARD DECLARATIONS ******************************/
void sync();
void write_counter_to_flash(const char *filename, uint32_t* counter);
uint32_t read_counter_from_flash(const char* filename);

/* TODO add support for tx
struct SecPlus2Writer {
    uint8_t tx_buf[SECPLUS2_CODE_LEN];
};
 */

/********************************** LOCAL STORAGE *****************************************/

SoftwareSerial sw_serial;
extern struct GarageDoor garage_door;

SecPlus2Reader reader;
PacketDecoder decoder;

uint32_t id_code = 0;
uint32_t rolling_code = 0;

void handle_door_status(SecPlus2DoorStatus status) {

    GarageDoorCurrentState val = CURR_OPEN;
    switch (status) {
        case SecPlus2DoorStatus::Unknown:
            Serial.println("got unknown door status wtf");
            break;

        case SecPlus2DoorStatus::Open:
            val = CURR_OPEN;
            break;

        case SecPlus2DoorStatus::Closed:
            val = CURR_CLOSED;
            break;

        case SecPlus2DoorStatus::Stopped:
            val = CURR_STOPPED;
            break;

        case SecPlus2DoorStatus::Opening:
            val = CURR_OPENING;
            break;

        case SecPlus2DoorStatus::Closing:
            val = CURR_CLOSING;
            break;

        case SecPlus2DoorStatus::Syncing:
            Serial.println("got syncing door status wtf");
            break;

    };

    garage_door.current_state = val;

    notify_homekit_current_door_state_change();
}


/********************************** MAIN LOOP CODE *****************************************/

void setup_comms() {
    INFO("Setting up comms for secplus2.0 protocol");

    reader.set_packet_decoder(&decoder);
    decoder.set_door_status_cb(handle_door_status);

    sw_serial.begin(9600, SWSERIAL_8N1, UART_RX_PIN, UART_TX_PIN, true);
    sw_serial.enableIntTx(false);
    sw_serial.enableAutoBaud(true); // found in ratgdo/espsoftwareserial branch autobaud

    LittleFS.begin();
    id_code = read_counter_from_flash("id_code");
    if (!id_code) {
        id_code = random(0x1, 0xFFFF);
        write_counter_to_flash("id_code", &id_code);
    }

    rolling_code = read_counter_from_flash("rolling");

    Serial.println("Syncing rolling code counter after reboot...");
    sync();

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

    if (!sw_serial.available()) {
        return;
    }

    uint8_t ser_data = sw_serial.read();
    reader.push_byte(ser_data);
}

/********************************** CONTROLLER CODE *****************************************/

void open_door() {
    INFO("TODO: open door\n");
    // TODO xmit open door cmd
}

void close_door() {
    INFO("TODO: close door\n");
    // TODO xmit close door command
}

/********************************** UTIL CODE *****************************************/

uint32_t read_counter_from_flash(const char* filename) {

    File file = LittleFS.open(filename, "r");

    if (!file) {
        Serial.print(filename);
        Serial.println(" doesn't exist. creating...");

        write_counter_to_flash(filename, 0);
        return 0;
    }

    uint32_t counter = file.parseInt();

    file.close();

    return counter;
}

void write_counter_to_flash(const char *filename, uint32_t* counter) {
    File file = LittleFS.open(filename, "w");

    file.print(*counter);

    file.close();
}

void sync() {
    // TODO
    Serial.println("synced");
}
