#include "SoftwareSerial.h"
#include "ratgdo.h"
#include "homekit_debug.h"
#include "LittleFS.h"
#include "secplus.h"
#include "homekit.h"

const uint8_t SECPLUS2_CODE_LEN = 19;
const uint32_t SECPLUS2_PREAMBLE = 0x00550100;

/*************************** FORWARD DECLARATIONS ******************************/
void sync();
void write_counter_to_flash(const char *filename, uint32_t* counter);
uint32_t read_counter_from_flash(const char* filename);

class SecPlus2Command;
class SecPlus2DoorStatus;
class SecPlus2Reader;
class SecPlus2Writer;
class PacketDecoder;

class SecPlus2Command {
    public:
        enum Command : uint16_t {
            Unknown = 0x00,
            StatusMsg = 0x81,
            LightToggle = 0x281,
            ObstructionMsg = 0x84,
            MotionToggle = 0x285,
        };

        SecPlus2Command() = default;
        constexpr SecPlus2Command(Command command) : m_command(command) {};

        constexpr operator Command() const { return m_command; };
        explicit operator bool() const = delete;

        static SecPlus2Command from_byte(uint16_t raw);

    private:
        Command m_command;
};

SecPlus2Command SecPlus2Command::from_byte(uint16_t raw) {
    if (raw == StatusMsg) {
        return SecPlus2Command::StatusMsg;
    } else if (raw == LightToggle) {
        return SecPlus2Command::LightToggle;
    } else if (raw == ObstructionMsg) {
        return SecPlus2Command::ObstructionMsg;
    } else if (raw == MotionToggle) {
        return SecPlus2Command::MotionToggle;
    } else {
        return SecPlus2Command::Unknown;
    }
}

class SecPlus2DoorStatus {
    public:
        enum Status : uint8_t {
            Unknown,
            Open,
            Closed,
            Stopped,
            Opening,
            Closing,
            Syncing
        };

        SecPlus2DoorStatus() = default;
        constexpr SecPlus2DoorStatus(Status status) : m_status(status) {};

        constexpr operator Status() const { return m_status; };
        explicit operator bool() const = delete;

        static SecPlus2DoorStatus from_byte(uint8_t raw);

    private:
        Status m_status;
};

SecPlus2DoorStatus SecPlus2DoorStatus::from_byte(uint8_t raw) {
    return SecPlus2DoorStatus::Unknown;
}

typedef void (*secplus_door_status_cb)(SecPlus2DoorStatus door_status);

class PacketDecoder {
    private:
        secplus_door_status_cb m_door_status_cb;

    public:
        PacketDecoder() = default;

        void set_door_status_cb(secplus_door_status_cb cb);
        void handle_code(uint8_t packet[SECPLUS2_CODE_LEN]);
};

void PacketDecoder::set_door_status_cb(secplus_door_status_cb cb) {
    m_door_status_cb = cb;
}

void PacketDecoder::handle_code(uint8_t packet[SECPLUS2_CODE_LEN]) {
	uint32_t rolling = 0;
	uint64_t fixed = 0;
	uint32_t data = 0;

	uint8_t nibble = 0;
    /* TODO add support for lights, obstruction, etc
	uint8_t byte1 = 0;
	uint8_t byte2 = 0;
     */

	decode_wireline(packet, &rolling, &fixed, &data);

    SecPlus2Command cmd = SecPlus2Command::from_byte(((fixed >> 24) & 0xf00) | (data & 0xff));

	nibble = (data >> 8) & 0xf;
    /* TODO add support for lights, obstruction, etc
	byte1 = (data >> 16) & 0xff;
	byte2 = (data >> 24) & 0xff;
     */

    switch (cmd) {
        case SecPlus2Command::StatusMsg:
            this->m_door_status_cb(SecPlus2DoorStatus::from_byte(nibble));
            break;
        case SecPlus2Command::LightToggle:
            break;
        case SecPlus2Command::ObstructionMsg:
            break;
        case SecPlus2Command::MotionToggle:
            break;
        case SecPlus2Command::Unknown:
            break;
    }

}

enum SecPlus2ReaderMode : uint8_t {
    SCANNING,
    RECEIVING,
};

class SecPlus2Reader {
    private:
        bool m_is_reading = false;
        uint32_t m_msg_start = 0;
        size_t m_byte_count = 0;
        uint8_t m_rx_buf[SECPLUS2_CODE_LEN] = {0x55, 0x01, 0x00};
        SecPlus2ReaderMode m_mode = SCANNING;

        PacketDecoder* m_decoder;

    public:
        SecPlus2Reader() = default;

        void push_byte(uint8_t inp);
        void set_packet_decoder(PacketDecoder* decoder);

};

void SecPlus2Reader::push_byte(uint8_t inp) {
    bool msg_ready = false;

    switch (m_mode) {
        case SCANNING:
            m_msg_start <<= 8;
            m_msg_start |= inp;
            m_msg_start &= 0x00FFFFFF;

            if (m_msg_start == SECPLUS2_PREAMBLE) {
                m_byte_count = 3;
                m_mode = RECEIVING;
            }
            break;

        case RECEIVING:
            m_rx_buf[m_byte_count] = inp;
            m_byte_count += 1;

            if (m_byte_count == SECPLUS2_CODE_LEN) {
                m_mode = SCANNING;
                m_msg_start = 0;
                msg_ready = true;
            }
            break;
    }

    if (msg_ready && m_decoder) {
        m_decoder->handle_code(m_rx_buf);
    }
}

void SecPlus2Reader::set_packet_decoder(PacketDecoder* decoder) {
    m_decoder = decoder;
}

struct SecPlus2Writer {
    uint8_t tx_buf[SECPLUS2_CODE_LEN];
};

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
