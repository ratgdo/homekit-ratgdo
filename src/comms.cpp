// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License
#define TAG ("COMMS")

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>

#include <esp_system.h>
#include <esp_log.h>

#include "softuart.h"

#include "ratgdo.h"
#include "homekit.h"
#include "Reader.h"
#include "secplus2.h"
#include "Packet.h"
#include "log.h"

/********************************** LOCAL STORAGE *****************************************/

struct PacketAction {
    Packet pkt;
    bool inc_counter;
};

QueueHandle_t pkt_q;
extern struct GarageDoor garage_door;

SecPlus2Reader reader;

uint32_t id_code = 0;
uint32_t rolling_code = 0;

extern TimerHandle_t led_on_timer;
extern TimerHandle_t motion_timer;

/*************************** FORWARD DECLARATIONS ******************************/

void sync(SoftUart& sw_serial);
void write_counter_to_flash(const char *filename, uint32_t* counter);
uint32_t read_counter_from_flash(const char* filename);
bool transmit(SoftUart& sw_serial, PacketAction& pkt_ac);
void door_command(DoorAction action);
void send_get_status();
void reset_packet_reader(TimerHandle_t t);

/********************************** MAIN LOOP CODE *****************************************/

void comms_task_entry(void* ctx) {
    ESP_LOGI(TAG, "Setting up comms for secplus2.0 protocol");

    SoftUart sw_serial = SoftUart(UART_RX_PIN, UART_TX_PIN, 9600, true);

    /*
    LittleFS.begin();
    id_code = read_counter_from_flash("id_code");
    if (!id_code) {
        ESP_LOGI(TAG, "id code not found");
        write_counter_to_flash("id_code", &id_code);
    }
    */
    // does not need to be cryptographically secure, just unlikely to repeat
    id_code = (((esp_random() % 0xFFF) + 1) << 12) | 0x539;
    ESP_LOGI(TAG, "id code %02X", id_code);

    // rolling_code = read_counter_from_flash("rolling");
    rolling_code = 0;
    ESP_LOGI(TAG, "rolling code %02X", rolling_code);

    pkt_q = xQueueCreate(5, sizeof(PacketAction));

    ESP_LOGI(TAG, "Syncing rolling code counter after reboot...");
    //sync(sw_serial);

    while (true) {

        // TODO each side of this branch should be a separate task, each handling one end of pkt_q
        if (sw_serial.available()) {

            uint8_t ser_data;
            sw_serial.read(&ser_data);
            if (reader.push_byte(ser_data)) {

                print_packet(reader.fetch_buf());
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
                                    ESP_LOGE(TAG, "Got door state unknown");
                                    break;
                            }

                            ESP_LOGI(TAG, "tgt %d curr %d", garage_door.target_state, garage_door.current_state);
                            notify_homekit_current_door_state_change();

                            if (pkt.m_data.value.status.light != garage_door.light) {
                                ESP_LOGI(TAG, "Light Status %s", pkt.m_data.value.status.light ? "On" : "Off");
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
                                ESP_LOGI(TAG, "Lock Cmd %d", lock);
                                garage_door.target_lock = lock;
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
                                ESP_LOGI(TAG, "Light Cmd %s", l ? "On" : "Off");
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
                            ESP_LOGI(TAG, "Motion Detected");
                            /* When we get the motion detect message, notify HomeKit. Motion sensor
                               will continue to send motion messages every 5s until motion stops.
                               set a timer for 5 seconds to disable motion after the last message */
                            xTimerReset(motion_timer, 0);
                            if (!garage_door.motion) {
                                garage_door.motion = true;
                                notify_homekit_motion();
                            }
                            // Update status because things like light may have changed states
                            send_get_status();
                            break;
                        }

                    default:
                        ESP_LOGI(TAG, "Support for %s packet unimplemented. Ignoring.", PacketCommand::to_string(pkt.m_pkt_cmd));
                        break;
                }
            }

        } else {
            PacketAction pkt_ac;

            if (uxQueueMessagesWaiting(pkt_q) > 0) {
                ESP_LOGD(TAG, "packet ready for tx");
                xQueueReceive(pkt_q, &pkt_ac, 0);  // ignore errors
                if (!transmit(sw_serial, pkt_ac)) {
                    ESP_LOGE(TAG, "transmit failed, will retry");
                    xQueueSendToFront(pkt_q, &pkt_ac, 0); // ignore errors
                }
            }
        }
    }
}

/********************************** CONTROLLER CODE *****************************************/

bool transmit(SoftUart& sw_serial, PacketAction& pkt_ac) {
    // Turn off LED
    gpio_set_level(LED_BUILTIN, true);
    xTimerReset(led_on_timer, 0);

    uint8_t buf[SECPLUS2_CODE_LEN];
    if (pkt_ac.pkt.encode(rolling_code, buf) != 0) {
        ESP_LOGE(TAG, "Could not encode packet");
        pkt_ac.pkt.print();
        return false;
    }

    // inverted logic, so this pulls the bus low to assert it
    gpio_set_level(UART_TX_PIN, true);
    ets_delay_us(1300);
    gpio_set_level(UART_TX_PIN, false);
    ets_delay_us(130);

    // check to see if anyone else is continuing to assert the bus after we have released it
    if (gpio_get_level(UART_RX_PIN)) {
        ESP_LOGI(TAG, "Collision detected, waiting to send packet");
        return false;
    }


    if (!sw_serial.transmit(buf, SECPLUS2_CODE_LEN)) {
        ESP_LOGE(TAG, "failed to write packet");
        return false;
    }

    if (pkt_ac.inc_counter) {
        rolling_code += 1;
        write_counter_to_flash("rolling", &rolling_code);
    }

    return true;
}

void reset_packet_reader(TimerHandle_t t) {
    ESP_LOGW(TAG, "reader timed out getting next byte. resetting.");
    reader.reset();
}

void sync(SoftUart& sw_serial) {
    // for exposition about this process, see docs/syncing.md

    PacketData d;
    d.type = PacketDataType::NoData;
    d.value.no_data = NoData();
    Packet pkt = Packet(PacketCommand::GetOpenings, d, id_code);
    PacketAction pkt_ac = {pkt, true};
    transmit(sw_serial, pkt_ac);

    vTaskDelay(pdMS_TO_TICKS(1000));

    pkt = Packet(PacketCommand::GetStatus, d, id_code);
    pkt_ac.pkt = pkt;
    transmit(sw_serial, pkt_ac);

}

void door_command(DoorAction action) {

    PacketData data;
    data.type = PacketDataType::DoorAction;
    data.value.door_action.action = action;
    data.value.door_action.pressed = true;
    data.value.door_action.id = 1;

    Packet pkt = Packet(PacketCommand::DoorAction, data, id_code);
    PacketAction pkt_ac = {pkt, false};

    if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "packet queue full, dropping door command pressed pkt");
    }

    pkt_ac.pkt.m_data.value.door_action.pressed = false;
    pkt_ac.inc_counter = true;

    if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "packet queue full, dropping door command release pkt");
    }
}

void open_door() {
    ESP_LOGI(TAG, "open door req\n");

    if (garage_door.current_state == CURR_OPENING) {
        ESP_LOGI(TAG, "door already opening; ignored req");
        return;
    }

    door_command(DoorAction::Open);
}

void close_door() {
    ESP_LOGI(TAG, "close door req\n");

    if (garage_door.current_state == CURR_CLOSING) {
        ESP_LOGI(TAG, "door already closing; ignored req");
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

void send_get_status() {
    PacketData d;
    d.type = PacketDataType::NoData;
    d.value.no_data = NoData();
    Packet pkt = Packet(PacketCommand::GetStatus, d, id_code);
    PacketAction pkt_ac = {pkt, true};
    if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "packet queue full, dropping get status pkt");
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

    Packet pkt = Packet(PacketCommand::Lock, data, id_code);
    PacketAction pkt_ac = {pkt, true};

    if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "packet queue full, dropping lock pkt");
    }
    send_get_status();
}

void set_light(bool value) {
    PacketData data;
    data.type = PacketDataType::Light;
    if (value) {
        data.value.light.light = LightState::On;
    } else {
        data.value.light.light = LightState::Off;
    }

    Packet pkt = Packet(PacketCommand::Light, data, id_code);
    PacketAction pkt_ac = {pkt, true};

    if (xQueueSendToBack(pkt_q, &pkt_ac, 0) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "packet queue full, dropping light pkt");
    }
    send_get_status();
}


/********************************** UTIL CODE *****************************************/

uint32_t read_counter_from_flash(const char* filename) {

    /*
    File file = LittleFS.open(filename, "r");

    if (!file) {
        ESP_LOGI(TAG, "%s doesn't exist. creating...", filename);

        uint32_t count = 0;
        write_counter_to_flash(filename, &count);
        return 0;
    }

    uint32_t counter = file.parseInt();

    file.close();

    return counter;
    */
    return 0;
}

void write_counter_to_flash(const char *filename, uint32_t* counter) {
    //File file = LittleFS.open(filename, "w");
    ESP_LOGI(TAG, "writing %02X to file %s", *counter, filename);

    //file.print(*counter);

    //file.close();
}
