
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
#include "utilities.h"

/********************************** LOCAL STORAGE *****************************************/

struct PacketAction {
    Packet pkt;
    bool inc_counter;
    uint32_t delay;
};

Queue_t pkt_q;
SoftwareSerial sw_serial;
extern long unsigned int led_on_time;

extern struct GarageDoor garage_door;

uint8_t gdoSecurityType;

/******************************* SECURITY 2.0 *********************************/

SecPlus2Reader reader;
uint32_t id_code = 0;
uint32_t rolling_code = 0;

/******************************* SECURITY 1.0 *********************************/

static const uint8_t RX_LENGTH = 2;
typedef uint8_t RxPacket[RX_LENGTH * 4];
unsigned long last_rx;
unsigned long last_tx;

bool wallplateBooting= false;
bool wallPanelDetected = false;
DoorState doorState = DoorState::Unknown;
uint8_t lightState;
uint8_t lockState;

// keep this here incase at somepoint its needed
// it is used for emulation of wall panel
//byte secplus1States[19] = {0x35,0x35,0x35,0x35,0x33,0x33,0x53,0x53,0x38,0x3A,0x3A,0x3A,0x39,0x38,0x3A, 0x38,0x3A,0x39,0x3A};
// this is what MY 889LM exhibited when powered up (release of all buttons, and then polls)
byte secplus1States[] = {0x35,0x35,0x33,0x33, 0x38,0x3A,0x39};

// values for SECURITY+1.0 communication
enum secplus1Codes : uint8_t {
    DoorButtonPress = 0x30,
    DoorButtonRelease = 0x31,
    LightButtonPress = 0x32,
    LightButtonRelease = 0x33,
    LockButtonPress = 0x34,
    LockButtonRelease = 0x35,

    Unkown_0x36  = 0x36,
    Unknown_0x37 = 0x37,

    DoorStatus = 0x38,
    ObstructionStatus = 0x39,   // this is not proven
    LightLockStatus = 0x3A,
    Unknown = 0xFF
};

/*************************** FORWARD DECLARATIONS ******************************/

void sync();
bool process_PacketAction(PacketAction& pkt_ac);
void door_command(DoorAction action);
void send_get_status();
bool transmitSec1(byte toSend);
bool transmitSec2(PacketAction& pkt_ac);

/********************************** MAIN LOOP CODE *****************************************/

void setup_comms() {

    // init queue
    q_init(&pkt_q, sizeof(PacketAction), 8, FIFO,  false);

    LittleFS.begin();

    // read from flash, default of 2 (SECURITY+2.0) if file not exist
    gdoSecurityType = (uint8_t)read_int_from_file("gdo_security", 2);

    if (gdoSecurityType == 1) {

        RINFO("Setting up comms for Secuirty+1.0 protocol");

        sw_serial.begin(1200, SWSERIAL_8E1, UART_RX_PIN, UART_TX_PIN, true);

        wallPanelDetected = false;
        wallplateBooting = false;
        doorState = DoorState::Unknown;
        lightState = 2;
        lockState  = 2;

    }
    else {
        RINFO("Setting up comms for Secuirty+2.0 protocol");

        sw_serial.begin(9600, SWSERIAL_8N1, UART_RX_PIN, UART_TX_PIN, true);
        sw_serial.enableIntTx(false);
        sw_serial.enableAutoBaud(true); // found in ratgdo/espsoftwareserial branch autobaud

        // read from flash, default of 0 if file not exist
        id_code = read_int_from_file("id_code");
        if (!id_code) {
            RINFO("id code not found");
            id_code = (random(0x1, 0xFFF) << 12) | 0x539;
            write_int_to_file("id_code", &id_code);
        }
        RINFO("id code %02X", id_code);

        // read from flash, default of 0 if file not exist
        rolling_code = read_int_from_file("rolling");
        RINFO("rolling code %02X", rolling_code);

        RINFO("Syncing rolling code counter after reboot...");
        sync();
    }
}

void wallPlate_Emulation() {
    
	if (wallPanelDetected) return;

	unsigned long currentMillis = millis();
	static unsigned long lastRequestMillis = 0;
	static bool emulateWallPanel = false;
	static unsigned long serialDetected = 0;
	static uint8_t stateIndex = 0;

	if (!serialDetected) {
		if (sw_serial.available()) {
			serialDetected = currentMillis;
		}

		return;
	}

    // wait up to 15 seconds to look for an existing wallplate or it could be booting, so need to wait
	if (currentMillis - serialDetected < 15000 || wallplateBooting == true) {
		if (currentMillis - lastRequestMillis > 1000) {
			Serial.println("Looking for security+ 1.0 wall panel...");
			lastRequestMillis = currentMillis;
		}

		if(!wallPanelDetected && (doorState != DoorState::Unknown || lightState != 2)){
			wallPanelDetected = true;
            wallplateBooting = false;
			Serial.println("Wall panel detected.");
			return;
		}
	}else{
		if (!emulateWallPanel && !wallPanelDetected){
			emulateWallPanel = true;
			Serial.println("No wall panel detected. Switching to emulation mode.");
		}

		if (emulateWallPanel && currentMillis - lastRequestMillis > 250){
			lastRequestMillis = currentMillis;
			
            byte secplus1ToSend = byte(secplus1States[stateIndex]);
			transmitSec1(secplus1ToSend);
            stateIndex++;
			if (stateIndex == sizeof(secplus1States)) stateIndex = sizeof(secplus1States) - 3;
		}
	}
}

void comms_loop() {
    // SECUIRTY1.0
    if (gdoSecurityType == 1) {

        static bool reading_msg = false;
        static uint16_t byte_count = 0;
        static RxPacket rx_packet;
        bool gotMessage = false;

        
        if (sw_serial.available()) {
            uint8_t ser_byte = sw_serial.read();
            last_rx = millis();

            if (!reading_msg) {
                // valid?
                if (ser_byte >= 0x30 && ser_byte <= 0x3A) {
                    byte_count = 0;
                    rx_packet[byte_count++] = ser_byte;
                    reading_msg = true;
                }
                // is it single byte command? 
                // really all commands are single byte
                // is it a button push or release? (FROM WALL PANEL)
                if (ser_byte >= 0x30 && ser_byte <= 0x37) {
                    rx_packet[1] = 0;
                    reading_msg = false;
                    byte_count = 0;
                    
                    gotMessage = true;
                }
            }
            else {
                // save next byte
                rx_packet[byte_count++] = ser_byte;

                if (byte_count == RX_LENGTH) {
                    reading_msg = false;
                    byte_count = 0;
                    
                    gotMessage = true;
                }

                if (gotMessage == false && (millis() - last_rx) > 100) {
                    RINFO("RX message timeout");
                    // if we have a partial packet and it's been over 100ms since last byte was read,
                    // the rest is not coming (a full packet should be received in ~20ms),
                    // discard it so we can read the following packet correctly
                    reading_msg = false;
                    byte_count = 0;
                }
            }
        }

        // got data?
        if (gotMessage) {
            gotMessage = false;

            // get kvp
            // button press/release have no val, just a single byte
            uint8_t key = rx_packet[0];
            uint8_t val = rx_packet[1];   

            if (key == secplus1Codes::DoorButtonPress) { RINFO("0x30 RX (door press)"); }
            // wall panel is sending out 0x31 (Door Button Release) when it starts up
            // but also on release of door button
            else if (key == secplus1Codes::DoorButtonRelease) {
                RINFO("0x31 RX (door release)"); 
                
                // Possible power up of 889LM
                if ((DoorState)doorState == DoorState::Unknown) {
                    wallplateBooting = true;
                }
            }
            else if (key == secplus1Codes::LightButtonPress) { RINFO("0x32 RX (light press)"); }
            else if (key == secplus1Codes::LightButtonRelease) { RINFO("0x33 RX (light release)"); }

            // 2 byte status messages (0x38 - 0x3A)
            // its the byte sent out by the wallplate + the byte transmitted by the opener
            if (key == secplus1Codes::DoorStatus || key == secplus1Codes::ObstructionStatus || key == secplus1Codes::LightLockStatus) {
                
               // RINFO("MSG: %X%02X",key,val);

                switch (key) {
                    // door status
                    case secplus1Codes::DoorStatus:
                        
                        //RINFO("0x38 MSG: %02X",val);

                        // 0x5X = stopped
                        // 0x0X = moving
                        // best attempt to trap invalid values (due to collisions)
                        if (((val & 0xF0) != 0x00) && ((val & 0xF0) != 0x50) && ((val & 0xF0) != 0xB0)) {
                            RINFO("0x38 val upper nible not 0x0 or 0x5 or 0xB: %02X",val);
                            break;
                        }

                        val = (val & 0x7);
                        // 000 0x0 stopped
                        // 001 0x1 opening
                        // 010 0x2 open
                        // 100 0x4 closing
                        // 101 0x5 closed
                        // 110 0x6 stopped

                        // sec+1 doors sometimes report wrong door status
                        // require two sequential matching door states 
                        // I have not seen this to be the case on my unit (MJS)
                        static uint8_t prevDoor;
                        if(prevDoor != val){
                            prevDoor = val;
                            break;
                        }

                        switch (val){
                            case 0x00: doorState = DoorState::Stopped; break;
                            case 0x01: doorState = DoorState::Opening; break;
                            case 0x02: doorState = DoorState::Open; break;
                            // no 0x03 known
                            case 0x04: doorState = DoorState::Closing; break;
                            case 0x05: doorState = DoorState::Closed; break;
                            case 0x06: doorState = DoorState::Stopped; break;
                            default:   doorState = DoorState::Unknown; break;
                        }                       

                        //RINFO("doorstate: %d", doorState);
                        
                        switch (doorState) {
                            case DoorState::Open:
                                garage_door.current_state = CURR_OPEN;
                                garage_door.target_state = TGT_OPEN;
                                break;
                            case DoorState::Closed:
                                garage_door.current_state = CURR_CLOSED;
                                garage_door.target_state = TGT_CLOSED;
                                break;
                            case DoorState::Stopped:
                                garage_door.current_state = CURR_STOPPED;
                                garage_door.target_state = TGT_OPEN;
                                break;
                            case DoorState::Opening:
                                garage_door.current_state = CURR_OPENING;
                                garage_door.target_state = TGT_OPEN;
                                break;
                            case DoorState::Closing:
                                garage_door.current_state = CURR_CLOSING;
                                garage_door.target_state = TGT_CLOSED;
                                break;
                            case DoorState::Unknown:
                                RERROR("Got door state unknown");
                                break;
                        }

                        if (!garage_door.active) {
                            RINFO("activating door");
                            garage_door.active = true;
                            notify_homekit_active();
                            if (garage_door.current_state == CURR_OPENING || garage_door.current_state == CURR_OPEN) {
                                garage_door.target_state = TGT_OPEN;
                            } else {
                                garage_door.target_state = TGT_CLOSED;
                            }
                        }

                        static GarageDoorCurrentState gd_currentstate;
                        if (garage_door.current_state != gd_currentstate) {
                            gd_currentstate = garage_door.current_state;

                            const char* l = "unknown door state";
                            switch (gd_currentstate) {
                            case GarageDoorCurrentState::CURR_STOPPED:
                                l = "Stopped";
                                break;
                            case GarageDoorCurrentState::CURR_OPEN:
                                l = "Open";
                                break;
                            case GarageDoorCurrentState::CURR_OPENING:
                                l = "Opening";
                                break;
                            case GarageDoorCurrentState::CURR_CLOSED:
                                l = "Closed";
                                break;
                            case GarageDoorCurrentState::CURR_CLOSING:
                                l = "Closing";
                                break;
                            }
                            RINFO("status DOOR: %s",l);

                            notify_homekit_current_door_state_change();
                        }

                        static GarageDoorTargetState gd_TargetState;
                        if (garage_door.target_state != gd_TargetState) {
                            gd_TargetState = garage_door.target_state;
                            notify_homekit_target_door_state_change();
                        }

                        break;
                    
                    // objstruction states (not confired)
                    case secplus1Codes::ObstructionStatus:
                        // currently not using
                        break;

                    // light & lock
                    case secplus1Codes::LightLockStatus:

                        //RINFO("0x3A MSG: %X%02X",key,val);

                        // upper nibble must be 5
                        if ((val & 0xF0) != 0x50) {
                            RINFO("0x3A val upper nible not 5: %02X",val);
                            break;
                        }

                        lightState = bitRead(val, 2);
                        lockState  = !bitRead(val, 3);

                        // light status
                        static uint8_t lastLightState = 0xff;
                        // light state change?
                        if (lightState != lastLightState) {
                            RINFO("status LIGHT: %s", lightState ? "On" : "Off");
                            lastLightState = lightState;
                            
                            garage_door.light = (bool)lightState;
                            notify_homekit_light();
                        }

                        // lock status
                        static uint8_t lastLockState = 0xff;
                        // lock state change?
                        if (lockState != lastLockState) {
                            RINFO("status LOCK: %s", lockState ? "Secured" : "Unsecured");
                            lastLockState = lockState;

                            if (lockState) {
                                garage_door.current_lock = CURR_LOCKED;
                                garage_door.target_lock = TGT_LOCKED;
                            } else {
                                garage_door.current_lock = CURR_UNLOCKED;
                                garage_door.target_lock = TGT_UNLOCKED;
                            }
                            notify_homekit_target_lock();
                            notify_homekit_current_lock();
                        }

                        break;
                }
            }
        }

        //
        // PROCESS TRANSMIT QUEUE
        //
        PacketAction pkt_ac;
        static unsigned long cmdDelay = 0;
        unsigned long now;
        bool okToSend;
        
        if (!q_isEmpty(&pkt_q)) {
     
            now = millis();
            
            okToSend  = (now - last_rx > 20);       // after 20ms since last rx
            okToSend &= (now - last_rx < 200);      // before 200ms since last rx
            okToSend &= (now - last_tx > 20);       // after 20ms since last tx
            okToSend &= (now - last_tx > cmdDelay); // after any command delays

            // OK to send after a complete message comes in and 20ms elapses but not more than 100ms
            if (okToSend) {
                if (q_peek(&pkt_q, &pkt_ac)) {
                    if (process_PacketAction(pkt_ac)) {
                        // get next delay "between" transmits
                        cmdDelay = pkt_ac.delay;
                        q_drop(&pkt_q);
                    } else {
                        cmdDelay = 0;
                        RERROR("transmit failed, will retry");
                    }
                }
            }
        }

        // check for wall panel and provide emulator
        wallPlate_Emulation();
    }
    // SECUIRTY2.0+
    else {
        // no incoming data, check if we have command queued
        if (!sw_serial.available())
        {
            PacketAction pkt_ac;

            if (q_peek(&pkt_q, &pkt_ac)) {
                if (process_PacketAction(pkt_ac)) {
                    q_drop(&pkt_q);
                } else {
                    RERROR("transmit failed, will retry");
                }
            }
        }
        else
        {               
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
                                    garage_door.target_state = TGT_OPEN;
                                    break;
                                case DoorState::Closed:
                                    garage_door.current_state = CURR_CLOSED;
                                    garage_door.target_state = TGT_CLOSED;
                                    break;
                                case DoorState::Stopped:
                                    garage_door.current_state = CURR_STOPPED;
                                    garage_door.target_state = TGT_OPEN;
                                    break;
                                case DoorState::Opening:
                                    garage_door.current_state = CURR_OPENING;
                                    garage_door.target_state = TGT_OPEN;
                                    break;
                                case DoorState::Closing:
                                    garage_door.current_state = CURR_CLOSING;
                                    garage_door.target_state = TGT_CLOSED;
                                    break;
                                case DoorState::Unknown:
                                    RERROR("Got door state unknown");
                                    break;
                            }

                            if (!garage_door.active) {
                                RINFO("activating door");
                                garage_door.active = true;
                                notify_homekit_active();
                                if (garage_door.current_state == CURR_OPENING || garage_door.current_state == CURR_OPEN) {
                                    garage_door.target_state = TGT_OPEN;
                                } else {
                                    garage_door.target_state = TGT_CLOSED;
                                }
                            }

                            RINFO("tgt %d curr %d", garage_door.target_state, garage_door.current_state);
                            notify_homekit_target_door_state_change();
                            notify_homekit_current_door_state_change();

                            if (pkt.m_data.value.status.light != garage_door.light) {
                                RINFO("Light Status %s", pkt.m_data.value.status.light ? "On" : "Off");
                                garage_door.light = pkt.m_data.value.status.light;
                                notify_homekit_light();
                            }

                            if (pkt.m_data.value.status.lock) {
                                garage_door.current_lock = CURR_LOCKED;
                                garage_door.target_lock = TGT_LOCKED;
                            } else {
                                garage_door.current_lock = CURR_UNLOCKED;
                                garage_door.target_lock = TGT_UNLOCKED;
                            }
                            notify_homekit_target_lock();
                            notify_homekit_current_lock();

                            break;
                        }

                    case PacketCommand::Lock:
                        {
                            LockTargetState lock = garage_door.target_lock;
                            switch (pkt.m_data.value.lock.lock) {
                                case LockState::Off:
                                    lock = TGT_UNLOCKED;
                                    break;
                                case LockState::On:
                                    lock = TGT_LOCKED;
                                    break;
                                case LockState::Toggle:
                                    if (lock == TGT_LOCKED) {
                                        lock = TGT_UNLOCKED;
                                    } else {
                                        lock = TGT_LOCKED;
                                    }
                                    break;
                            }
                            if (lock != garage_door.target_lock) {
                                RINFO("Lock Cmd %d", lock);
                                garage_door.target_lock = lock;
                                notify_homekit_target_lock();
                            }
                            // Send a get status to make sure we are in sync
                            send_get_status();
                            break;
                        }

                    case PacketCommand::Light:
                        {
                            bool l = garage_door.light;
                            switch (pkt.m_data.value.light.light) {
                                case LightState::Off:
                                    l = false;
                                    break;
                                case LightState::On:
                                    l = true;
                                    break;
                                case LightState::Toggle:
                                case LightState::Toggle2:
                                    l = !garage_door.light;
                                    break;
                            }
                            if (l != garage_door.light) {
                                RINFO("Light Cmd %s", l ? "On" : "Off");
                                garage_door.light = l;
                                notify_homekit_light();
                            }
                            // Send a get status to make sure we are in sync
                            // Should really only need to do this on a toggle,
                            // But safer to do it always
                            send_get_status();
                            break;
                        }

                    case PacketCommand::Motion:
                        {
                            RINFO("Motion Detected");
                            // We got a motion message, so we know we have a motion sensor
                            // If it's not yet enabled, add the service
                            if (!garage_door.has_motion_sensor) {
                                RINFO("Detected new Motion Sensor. Enabling Service");
                                enable_service_homekit_motion();
                                garage_door.has_motion_sensor = true;
                            }

                            /* When we get the motion detect message, notify HomeKit. Motion sensor
                               will continue to send motion messages every 5s until motion stops.
                               set a timer for 5 seconds to disable motion after the last message */
                            garage_door.motion_timer = millis() + 5000;
                            if (!garage_door.motion) {
                                garage_door.motion = true;
                                notify_homekit_motion();
                            }
                            // Update status because things like light may have changed states
                            send_get_status();
                            break;
                        }

                    default:
                        RINFO("Support for %s packet unimplemented. Ignoring.", PacketCommand::to_string(pkt.m_pkt_cmd));
                        break;
                }
            }
        }
    } 
}

/********************************** CONTROLLER CODE *****************************************/
// SECURITY+1.0
bool transmitSec1(byte toSend) {

    // safety
    if (digitalRead(UART_RX_PIN) || sw_serial.available()) {
        return false;
    }
    
    // if no wall panel, we can disable rx while we transmit
    if (!wallPanelDetected) {
        sw_serial.enableRx(false);
    }

    sw_serial.write(toSend);
    last_tx = millis();

    // if no wall panel, we need to enable rx, since we disabled above
    if (!wallPanelDetected) {
        sw_serial.enableRx(true);
    }

    return true;
}

// SECURITY+2.0
bool transmitSec2(PacketAction& pkt_ac) {

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
            write_int_to_file("rolling", &rolling_code);
        }
    }

    return true;
}

bool process_PacketAction(PacketAction& pkt_ac) {

    bool success = false;

    // Turn off LED
    digitalWrite(LED_BUILTIN, HIGH);
    led_on_time = millis() + 500;
    
    if (gdoSecurityType == 1) {
        // check which action
        switch (pkt_ac.pkt.m_data.type)
        {
            case PacketDataType::DoorAction:
            {
                if (pkt_ac.pkt.m_data.value.door_action.pressed == true)
                {
                    success = transmitSec1(secplus1Codes::DoorButtonPress);
                    if (success) {
                        last_tx = millis();
                        RINFO("sending DOOR button press");
                    }
                }
                else
                {
                    success = transmitSec1(secplus1Codes::DoorButtonRelease);
                    if (success) {
                        last_tx = millis();
                        RINFO("sending DOOR button release");
                    }
                }

                break;
            }

            case PacketDataType::Light:
            {
                if (pkt_ac.pkt.m_data.value.light.pressed == true) {
                    success = transmitSec1(secplus1Codes::LightButtonPress);
                    if (success) {
                        last_tx = millis();
                        RINFO("sending LIGHT button press");
                    }
                }
                else {
                    success = transmitSec1(secplus1Codes::LightButtonRelease);
                    if (success) {
                        last_tx = millis();
                        RINFO("Sending LIGHT button release");
                    }
                }
            
                break;
            }

            case PacketDataType::Lock:
            {
                if (pkt_ac.pkt.m_data.value.lock.pressed == true) {
                    success = transmitSec1(secplus1Codes::LockButtonPress);
                    if (success) {
                        last_tx = millis();
                        RINFO("sending LOCK button press");
                    }
                }
                else {
                    success = transmitSec1(secplus1Codes::LockButtonRelease);
                    if (success) {
                        last_tx = millis();
                        RINFO("sending LOCK button release");
                    }
                }
            
                break;
            }

            default: 
            {
                RINFO("pkt_ac.pkt.m_data.type=%d", pkt_ac.pkt.m_data.type);
                
                break;
            }
        }
    }
    else {
        success = transmitSec2(pkt_ac);
    }

    return success;
}

void sync() {
    // only for SECURITY2.0
    // for exposition about this process, see docs/syncing.md

    PacketData d;
    d.type = PacketDataType::NoData;
    d.value.no_data = NoData();
    Packet pkt = Packet(PacketCommand::GetOpenings, d, id_code);
    PacketAction pkt_ac = {pkt, true};
    process_PacketAction(pkt_ac);

    delay(100);

    pkt = Packet(PacketCommand::GetStatus, d, id_code);
    pkt_ac.pkt = pkt;
    process_PacketAction(pkt_ac);
}

void door_command(DoorAction action) {

    PacketData data;
    data.type = PacketDataType::DoorAction;
    data.value.door_action.action = action;
    data.value.door_action.pressed = true;
    data.value.door_action.id = 1;

    Packet pkt = Packet(PacketCommand::DoorAction, data, id_code);
    PacketAction pkt_ac = {pkt, false, 250}; // 250ms delay for SECURITY1.0

    q_push(&pkt_q, &pkt_ac);

    // do button release
    pkt_ac.pkt.m_data.value.door_action.pressed = false;
    pkt_ac.inc_counter = true;
    pkt_ac.delay = 40;  // 40ms delay for SECURITY1.0

    q_push(&pkt_q, &pkt_ac);

    // when observing wall panel 2 releases happen, so we do the same
    if (gdoSecurityType == 1) {
        q_push(&pkt_q, &pkt_ac);
    }

    send_get_status();
}

void open_door() {
    RINFO("open door request");

    // safety
    if (garage_door.current_state == GarageDoorCurrentState::CURR_OPEN) { RINFO("door already open; ignored request"); return; } 

    if (garage_door.current_state == GarageDoorCurrentState::CURR_CLOSING) {
        RINFO("door is closing; do stop");
        door_command(DoorAction::Stop);
        return;
    }

    door_command(DoorAction::Open);
}

void close_door() {
    RINFO("close door request");

    // safety
    if (garage_door.current_state == GarageDoorCurrentState::CURR_CLOSED) { RINFO("door already closed; ignored request"); return; } 

    if (garage_door.current_state == GarageDoorCurrentState::CURR_OPENING) {
        RINFO("door already opening; do stop");
        door_command(DoorAction::Stop);
        return;
    }

    // !!! this needs testing
    if (gdoSecurityType == 2) {
        if (garage_door.current_state == GarageDoorCurrentState::CURR_OPENING) {
            door_command(DoorAction::Stop);
            // TODO? delay here and await the door having stopped, pending
            // implementation of a richer method of building conditions?
            // delay(1000);
        }
    }

    door_command(DoorAction::Close);
}

void send_get_status() {
    // only used with SECURITY2.0
    if (gdoSecurityType == 2) {
        PacketData d;
        d.type = PacketDataType::NoData;
        d.value.no_data = NoData();
        Packet pkt = Packet(PacketCommand::GetStatus, d, id_code);
        PacketAction pkt_ac = {pkt, true};
        q_push(&pkt_q, &pkt_ac);
    }
}

void set_lock(uint8_t value) {
    PacketData data;
    data.type = PacketDataType::Lock;
    if (value) {
        data.value.lock.lock = LockState::On;
        garage_door.target_lock = TGT_LOCKED;
    } else {
        data.value.lock.lock = LockState::Off;
        garage_door.target_lock = TGT_UNLOCKED;
    }

    // safety
    if (data.value.lock.lock == LockState::On  && garage_door.current_lock == LockCurrentState::CURR_LOCKED)  { RINFO("Lock already Locked"); return; }
    if (data.value.lock.lock == LockState::Off && garage_door.current_lock == LockCurrentState::CURR_UNLOCKED) { RINFO("Lock already Unlocked"); return; }

     // SECUIRTY1.0
    if (gdoSecurityType == 1) {

        // this emulates the "look" button press+release
        // - PRESS (0x34) 
        // - DELAY 3000ms
        // - RELEASE (0x35)
        // - DELAY 40ms
        // - RELEASE (0x35)
        // - DELAY 40ms

        data.value.lock.pressed = true;
        Packet pkt = Packet(PacketCommand::Lock, data, id_code);
        PacketAction pkt_ac = {pkt, true, 3000}; // 3000ms delay for SECURITY1.0

        q_push(&pkt_q, &pkt_ac);

        // button release
        pkt_ac.pkt.m_data.value.lock.pressed = false;   
        pkt_ac.delay = 40; // 40ms delay for SECURITY1.0
        // observed the wall plate does 2 releases, so we will too
        q_push(&pkt_q, &pkt_ac);
        q_push(&pkt_q, &pkt_ac);
    }
    // SECURITY2.0
    else {
        Packet pkt = Packet(PacketCommand::Lock, data, id_code);
        PacketAction pkt_ac = {pkt, true};

        q_push(&pkt_q, &pkt_ac);
        send_get_status();
    }
}

void set_light(bool value) {
    PacketData data;
    data.type = PacketDataType::Light;
    if (value) {
        data.value.light.light = LightState::On;
    } else {
        data.value.light.light = LightState::Off;
    }

    // safety
    if (data.value.light.light == LightState::On  && garage_door.light == true)  { RINFO("Light already On"); return; }
    if (data.value.light.light == LightState::Off && garage_door.light == false) { RINFO("Light already Off"); return; }

    // SECUIRTY+1.0
    if (gdoSecurityType == 1) {
        // this emulates the "light" button press+release
        // - PRESS (0x32) 
        // - DELAY 250ms
        // - RELEASE (0x33)
        // - DELAY 40ms
        // - RELEASE (0x33)
        // - DELAY 40ms
        data.value.light.pressed = true;

        Packet pkt = Packet(PacketCommand::Light, data, id_code);
        PacketAction pkt_ac = {pkt, true, 250}; // 250ms delay for SECURITY1.0

        q_push(&pkt_q, &pkt_ac);

        // button release
        pkt_ac.pkt.m_data.value.light.pressed = false;   
        pkt_ac.delay = 40; // 40ms delay for SECURITY1.0
        // observed the wall plate does 2 releases, so we will too
        q_push(&pkt_q, &pkt_ac);
        q_push(&pkt_q, &pkt_ac);
    }
    // SECURITY+2.0
    else {
        Packet pkt = Packet(PacketCommand::Light, data, id_code);
        PacketAction pkt_ac = {pkt, true};

        q_push(&pkt_q, &pkt_ac);
        send_get_status();
    }
}
