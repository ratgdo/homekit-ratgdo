#include "SoftwareSerial.h"
#include "ratgdo.h"
#include "homekit_debug.h"
#include "LittleFS.h"
// #include "secplus.h"
#include "homekit.h"
#include "log.h"

#include "Decoder.h"
#include "Reader.h"
#include "secplus2.h"
#include "Command.h"

/*************************** FORWARD DECLARATIONS ******************************/
void sync();
void write_counter_to_flash(const char *filename, uint32_t* counter);
uint32_t read_counter_from_flash(const char* filename);
void transmit_command(SecPlus2Command cmd);

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
            RINFO("got unknown door status wtf");
            break;

        case SecPlus2DoorStatus::Open:
            RINFO("got door open status");
            val = CURR_OPEN;
            break;

        case SecPlus2DoorStatus::Closed:
            RINFO("got door closed status");
            val = CURR_CLOSED;
            break;

        case SecPlus2DoorStatus::Stopped:
            RINFO("got door stopped status");
            val = CURR_STOPPED;
            break;

        case SecPlus2DoorStatus::Opening:
            RINFO("got door opening status");
            val = CURR_OPENING;
            break;

        case SecPlus2DoorStatus::Closing:
            RINFO("got door closing status");
            val = CURR_CLOSING;
            break;

        case SecPlus2DoorStatus::Syncing:
            RINFO("got syncing door status wtf");
            break;

    };

    garage_door.current_state = val;

    notify_homekit_current_door_state_change();
}

void transmit_command(SecPlus2Command cmd) {
    cmd.prepare(id_code, &rolling_code, [&](uint8_t pkt[SECPLUS2_CODE_LEN]) {
            // TODO add collision detection and backoff/retry/yield
            //
            // one possible approach is to store the state of the transmission in the cmd object,
            // and invoke prepare repeatedly while it, e.g. returns true, or something
            print_packet(pkt);

            digitalWrite(UART_TX_PIN, HIGH);
            delayMicroseconds(1300);
            digitalWrite(UART_TX_PIN, LOW);
            delayMicroseconds(130);

            sw_serial.write(pkt, SECPLUS2_CODE_LEN);
            delayMicroseconds(100);
    });
}

/********************************** MAIN LOOP CODE *****************************************/

void setup_comms() {
    RINFO("Setting up comms for secplus2.0 protocol");

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
    RINFO("id code %02X", id_code);

    rolling_code = read_counter_from_flash("rolling");
    RINFO("rolling code %02X", rolling_code);

    RINFO("Syncing rolling code counter after reboot...");
    sync();

}

void comms_loop() {
    if (!sw_serial.available()) {
        return;
    }

    uint8_t ser_data = sw_serial.read();
    reader.push_byte(ser_data);
}

/********************************** CONTROLLER CODE *****************************************/

void open_door() {
    RINFO("open door req\n");
    if (garage_door.current_state == CURR_OPEN || garage_door.current_state == CURR_OPENING) {
        RINFO("open door ignored\n");
        return;
    }

    transmit_command(SecPlus2Command::Door);
}

void close_door() {
    RINFO("close door req\n");
    if (garage_door.current_state == CURR_CLOSED || garage_door.current_state == CURR_CLOSING) {
        RINFO("close door ignored\n");
        return;
    }

    transmit_command(SecPlus2Command::Door);
}

/********************************** UTIL CODE *****************************************/

uint32_t read_counter_from_flash(const char* filename) {

    File file = LittleFS.open(filename, "r");

    if (!file) {
        RINFO("%s doesn't exist. creating...", filename);

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
    transmit_command(SecPlus2Command::Sync);
    RINFO("synced");
}
