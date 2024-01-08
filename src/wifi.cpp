// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

/* WiFi configuration and setup
 *
 * This approach relies on the ESP8266 Arduino WiFi class storing credentials
 * in EEPROM on our behalf, and its ability to continue to attempt to
 * reconnect. If there are stored credentials, `WiFi.begin()` will use them. If
 * not, Improv will eventually call `WiFi.begin`, and in doing so will store
 * the credentials. If connection does not succeed after 2 seconds, the stored
 * credentials will be erased.
 *
 * Portions of this code written by Jonathas Barbosa <jnths@gmail.com>, and adapted from
 *   https://github.com/jnthas/improv-wifi-demo
 */
#define TAG ("WIFI")

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_wifi.h>

#include "wifi.h"
/* TODO Improv
#include "improv.h"
*/
#include "ratgdo.h"
#include "log.h"

#define UART_BUF_SIZE (128)

#define MAX_ATTEMPTS_WIFI_CONNECTION 20
uint8_t x_buffer[16];
uint8_t x_position = 0;

/* TODO Improv
void set_error(improv::Error error);
void send_response(std::vector<uint8_t> &response);
void set_state(improv::State state);
void get_available_wifi_networks();
bool on_command_callback(improv::ImprovCommand cmd);
void on_error_callback(improv::Error err);
*/

// TODO Improv
#define WIFI_SSID   ""
#define WIFI_PASS   ""
#define WIFI_RETRY  10

static int s_retry_num = 0;
static WifiStatus wifi_status = WifiStatus::Disconnected;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_status = WifiStatus::Pending;
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_RETRY) {
            wifi_status = WifiStatus::Pending;
            ESP_LOGI(TAG, "retry to connect to the AP");
            esp_wifi_connect();
            s_retry_num++;
        } else {
            wifi_status = WifiStatus::Disconnected;
            ESP_LOGI(TAG,"connect to the AP fail");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_status = WifiStatus::Connected;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
        s_retry_num = 0;
    }
}


void wifi_task_entry(void* ctx) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = { };
    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASS);

    if (strlen((char *)wifi_config.sta.password)) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    // set up the UART for incoming Improv bytes
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, UART_BUF_SIZE * 2, 0, 0, NULL, 0);

    ESP_LOGI(TAG, "wifi and improv setup finished.");

/* TODO Improv
    while (true) {
    if (Serial.available() > 0) {
        uint8_t b = Serial.read();

        if (parse_improv_serial_byte(x_position, b, x_buffer, on_command_callback, on_error_callback)) {
            x_buffer[x_position++] = b;
        } else {
            x_position = 0;
        }
    }
    }
*/
    vTaskDelete(NULL); // TODO Improv
}

/* TODO Improv
bool connect_wifi(std::string ssid, std::string password) {
    uint8_t count = 0;

    WiFi.begin(ssid.c_str(), password.c_str());

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        yield();

        if (count > MAX_ATTEMPTS_WIFI_CONNECTION) {
            WiFi.disconnect();
            return false;
        }
        count++;
    }

    return true;
}

std::vector<std::string> get_local_url() {
    return {
        // TODO
        // URL where user can finish onboarding or use device
        // Recommended to use website hosted by device
        String("http://" + WiFi.localIP().toString()).c_str()
    };
}

void on_error_callback(improv::Error err) {
    RERROR("improv error: %02X", err);
}

bool on_command_callback(improv::ImprovCommand cmd) {

    switch (cmd.command) {
        case improv::Command::GET_CURRENT_STATE:
            {
                if ((WiFi.status() == WL_CONNECTED)) {
                    set_state(improv::State::STATE_PROVISIONED);
                    std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_CURRENT_STATE, get_local_url(), false);
                    send_response(data);

                } else {
                    set_state(improv::State::STATE_AUTHORIZED);
                }

                break;
            }

        case improv::Command::WIFI_SETTINGS:
            {
                if (cmd.ssid.length() == 0) {
                    set_error(improv::Error::ERROR_INVALID_RPC);
                    break;
                }

                set_state(improv::STATE_PROVISIONING);

                if (connect_wifi(cmd.ssid, cmd.password)) {
                    set_state(improv::STATE_PROVISIONED);
                    std::vector<uint8_t> data = improv::build_rpc_response(improv::WIFI_SETTINGS, get_local_url(), false);
                    send_response(data);

                } else {
                    set_state(improv::STATE_STOPPED);
                    set_error(improv::Error::ERROR_UNABLE_TO_CONNECT);
                }

                break;
            }

        case improv::Command::GET_DEVICE_INFO:
            {
                std::vector<std::string> infos = {
                    DEVICE_NAME,
                    AUTO_VERSION,
                    CHIP_FAMILY,
                    MODEL_NAME
                };
                std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_DEVICE_INFO, infos, false);
                send_response(data);
                break;
            }

        case improv::Command::GET_WIFI_NETWORKS:
            {
                get_available_wifi_networks();
                break;
            }

        default: {
                     set_error(improv::ERROR_UNKNOWN_RPC);
                     return false;
                 }
    }

    return true;
}

void get_available_wifi_networks() {
    int networkNum = WiFi.scanNetworks();

    int sortedIndicies[networkNum];
    for (int i = 0; i < networkNum; i++) {
        sortedIndicies[i] = i;
    }

    // sort networks by RSSI, strongest to weakest
    for (int i = 0; i < networkNum; i++) {
        for (int j = i + 1; j < networkNum; j++) {
            if (WiFi.RSSI(sortedIndicies[j]) > WiFi.RSSI(sortedIndicies[i])) {
                std::swap(sortedIndicies[i], sortedIndicies[j]);
            }
        }
    }

    // find duplicates (must be RSSI sorted)
    String temp_ssid;
    for (int i = 0; i < networkNum; i++) {
        if (sortedIndicies[i] == -1) continue;       // skip duplicate
        temp_ssid = WiFi.SSID(sortedIndicies[i]);
        for (int j = i + 1; j < networkNum; j++) {
            if (temp_ssid == WiFi.SSID(sortedIndicies[j])) {
                sortedIndicies[j] = -1;              // set dupes to -1 to skip later
            }
        }
    }

    for (int id = 0; id < networkNum; ++id) {
        if (sortedIndicies[id] == -1) continue;      // skip duplicate
        std::vector<uint8_t> data = improv::build_rpc_response(
                improv::GET_WIFI_NETWORKS, {WiFi.SSID(sortedIndicies[id]), String(WiFi.RSSI(sortedIndicies[id])), (WiFi.encryptionType(sortedIndicies[id]) == ENC_TYPE_NONE ? "NO" : "YES")}, false);
        send_response(data);
        delay(1);
    }
    // final response
    std::vector<uint8_t> data =
        improv::build_rpc_response(improv::GET_WIFI_NETWORKS, std::vector<std::string>{}, false);
    send_response(data);

    // delete scan from memory
    WiFi.scanDelete();
}

void set_state(improv::State state) {

    std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
    data.resize(11);
    data[6] = improv::IMPROV_SERIAL_VERSION;
    data[7] = improv::TYPE_CURRENT_STATE;
    data[8] = 1;
    data[9] = state;

    uint8_t checksum = 0x00;
    for (uint8_t d : data)
        checksum += d;
    data[10] = checksum;

    Serial.write(data.data(), data.size());
}

void send_response(std::vector<uint8_t> &response) {
    std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
    data.resize(9);
    data[6] = improv::IMPROV_SERIAL_VERSION;
    data[7] = improv::TYPE_RPC_RESPONSE;
    data[8] = response.size();
    data.insert(data.end(), response.begin(), response.end());

    uint8_t checksum = 0x00;
    for (uint8_t d : data)
        checksum += d;
    data.push_back(checksum);

    Serial.write(data.data(), data.size());
}

void set_error(improv::Error error) {
    std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
    data.resize(11);
    data[6] = improv::IMPROV_SERIAL_VERSION;
    data[7] = improv::TYPE_ERROR_STATE;
    data[8] = 1;
    data[9] = error;

    uint8_t checksum = 0x00;
    for (uint8_t d : data)
        checksum += d;
    data[10] = checksum;

    Serial.write(data.data(), data.size());
}
*/
