#include "SoftwareSerial.h"
#include "ratgdo.h"
#include "homekit_debug.h"
#include "LittleFS.h"
// #include "secplus.h"
#include "homekit.h"
#include "log.h"

#include "Reader.h"
#include "secplus2.h"
#include "Packet.h"
#include "cQueue.h"

/********************************** LOCAL STORAGE *****************************************/

struct PacketAction {
    Packet pkt;
    bool inc_counter;
};

Queue_t pkt_q;
SoftwareSerial sw_serial;
extern struct GarageDoor garage_door;

SecPlus2Reader reader;

uint32_t id_code = 0;
uint32_t rolling_code = 0;

/*************************** FORWARD DECLARATIONS ******************************/

void sync();
void write_counter_to_flash(const char *filename, uint32_t* counter);
uint32_t read_counter_from_flash(const char* filename);
bool transmit(PacketAction& pkt_ac);
void door_command(DoorAction action);

/********************************** MAIN LOOP CODE *****************************************/

void setup_comms() {
    RINFO("Setting up comms for secplus2.0 protocol");

    sw_serial.begin(9600, SWSERIAL_8N1, UART_RX_PIN, UART_TX_PIN, true);
    sw_serial.enableIntTx(false);
    sw_serial.enableAutoBaud(true); // found in ratgdo/espsoftwareserial branch autobaud

    LittleFS.begin();
    id_code = read_counter_from_flash("id_code");
    if (!id_code) {
        RINFO("id code not found");
        id_code = (random(0x1, 0xFFF) << 12) | 0x539;
        write_counter_to_flash("id_code", &id_code);
    }
    RINFO("id code %02X", id_code);

    rolling_code = read_counter_from_flash("rolling");
    RINFO("rolling code %02X", rolling_code);

    q_init(&pkt_q, sizeof(PacketAction), 5, FIFO,  false);

    RINFO("Syncing rolling code counter after reboot...");
    sync();

}

void comms_loop() {
    if (sw_serial.available()) {
        // spin on receiving data until the whole packet has arrived

        uint8_t ser_data = sw_serial.read();
        if (reader.push_byte(ser_data)) {
            Packet pkt = Packet(reader.fetch_buf());
            pkt.print();

            switch (pkt.m_pkt_cmd) {
                case PacketCommand::Status:
                    {
                        switch (pkt.m_data.value.status.door) {
                            case DoorState::Open:
                                garage_door.current_state = CURR_OPEN;
                                break;
                            case DoorState::Closed:
                                garage_door.current_state = CURR_CLOSED;
                                break;
                            case DoorState::Stopped:
                                garage_door.current_state = CURR_STOPPED;
                                break;
                            case DoorState::Opening:
                                garage_door.current_state = CURR_OPENING;
                                break;
                            case DoorState::Closing:
                                garage_door.current_state = CURR_CLOSING;
                                break;
                            case DoorState::Unknown:
                                RERROR("Got door state unknown");
                                break;
                        }
                        notify_homekit_current_door_state_change();
                        break;
                    }
                default:
                    RINFO("Support for %s packet unimplemented. Ignoring.", PacketCommand::to_string(pkt.m_pkt_cmd));
                    break;
            }
        }

    } else {
        // no incoming data waiting, so we can start transmitting

        PacketAction pkt_ac;

        if (q_peek(&pkt_q, &pkt_ac)) {
            if (transmit(pkt_ac)) {
                q_drop(&pkt_q);
            } else {
                RERROR("transmit failed, will retry");
            }
        }
    }
}

/********************************** CONTROLLER CODE *****************************************/

bool transmit(PacketAction& pkt_ac) {
    // inverted logic, so this pulls the bus low to assert it
    digitalWrite(UART_TX_PIN, HIGH);
    delayMicroseconds(1300);
    digitalWrite(UART_TX_PIN, LOW);
    delayMicroseconds(130);

    // check to see if anyone else is continuing to assert the bus after we have released it
    if (digitalRead(UART_RX_PIN)) {
        RINFO("Collision detected, waiting to send packet");
        return false;
    } else {
        uint8_t buf[SECPLUS2_CODE_LEN];
        if (pkt_ac.pkt.encode(rolling_code, buf) != 0) {
            RERROR("Could not encode packet");
            pkt_ac.pkt.print();
        } else {
            sw_serial.write(buf, SECPLUS2_CODE_LEN);
            delayMicroseconds(100);
        }

        if (pkt_ac.inc_counter) {
            rolling_code += 1;
            // TODO slow this rate down to save eeprom wear
            write_counter_to_flash("rolling", &rolling_code);
        }
    }

    return true;
}

void sync() {
    PacketData d;
    d.type = PacketDataType::NoData;
    d.value.no_data = NoData();
    Packet pkt = Packet(PacketCommand::GetStatus, d, id_code);
    PacketAction pkt_ac = {pkt, true};
    transmit(pkt_ac);

    delay(500);

    pkt = Packet(PacketCommand::GetOpenings, d, id_code);
    pkt_ac.pkt = pkt;
    transmit(pkt_ac);

    delay(500);

}

void door_command(DoorAction action) {

    PacketData data;
    data.type = PacketDataType::DoorAction;
    data.value.door_action.action = action;
    data.value.door_action.pressed = true;
    data.value.door_action.id = 1;

    Packet pkt = Packet(PacketCommand::DoorAction, data, id_code);
    PacketAction pkt_ac = {pkt, false};

    q_push(&pkt_q, &pkt_ac);

    pkt_ac.pkt.m_data.value.door_action.pressed = false;
    pkt_ac.inc_counter = true;

    q_push(&pkt_q, &pkt_ac);
}

void open_door() {
    RINFO("open door req\n");

    if (garage_door.current_state == CURR_OPENING) {
        RINFO("door already opening; ignored req");
        return;
    }

    door_command(DoorAction::Open);
}

void close_door() {
    RINFO("close door req\n");

    if (garage_door.current_state == CURR_CLOSING) {
        RINFO("door already closing; ignored req");
        return;
    }

    if (garage_door.current_state == CURR_OPENING) {
        door_command(DoorAction::Stop);
        // TODO? delay here and await the door having stopped, pending
        // implementation of a richer method of building conditions?
        // delay(1000);
    }

    door_command(DoorAction::Close);
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
    RINFO("writing %02X to file %s", *counter, filename);

    file.print(*counter);

    file.close();
}
