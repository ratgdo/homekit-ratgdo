// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

/* WiFi configuration and setup
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
#include <nvs.h>
#include <esp_err.h>

#include <improv.h>

#include "wifi.h"
#include "ratgdo.h"
#include "log.h"

#include <freertos/task.h>

#define UART_BUF_SZ (256)
#define UART_EVT_Q_SZ (8)
static QueueHandle_t uart0_queue;

#define MAX_ATTEMPTS_WIFI_CONNECTION 5
const uint16_t NETWORK_COUNT = 16;
uint8_t x_buffer[32 /* bytes per ssid */ + 64 /* bytes per password */ + 16 /* bytes overhead */];
uint8_t x_position = 0;
uint8_t dtmp[UART_BUF_SZ];

wifi_ap_record_t wifi_ap[NETWORK_COUNT];

void set_error(improv::Error error);
void send_response(std::vector<uint8_t> &response);
void set_state(improv::State state);
void get_available_wifi_networks();
bool on_command_callback(improv::ImprovCommand cmd);
void on_error_callback(improv::Error err);
bool connect_wifi(std::string& ssid, std::string& password);

wifi_config_t wifi_config = { };
const size_t WIFI_SSID_MAX_LEN = 31; // 802.11 point 7.3.2.1, plus NUL terminator
char wifi_ssid[WIFI_SSID_MAX_LEN + 1];
const size_t WIFI_PASS_MAX_LEN = 64;  // from defn of wifi_sta_config_t
char wifi_pass[WIFI_PASS_MAX_LEN];

static int s_retry_num = 0;
static WifiStatus wifi_status = WifiStatus::Disconnected;
static ip4_addr_t ip_info;

static nvs_handle_t wifi_nvs_handle;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGD(TAG, "wifi event station start");
        wifi_status = WifiStatus::Pending;
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGD(TAG, "wifi event station disconnected");
        if (s_retry_num < MAX_ATTEMPTS_WIFI_CONNECTION) {
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
        memcpy(&ip_info, &event->ip_info.ip, sizeof(ip4_addr_t));
        s_retry_num = 0;
    }
}


void wifi_task_entry(void* ctx) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &wifi_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening wifi NVS handle. Wifi task dying.", esp_err_to_name(err));
        return;
    }

    size_t ssid_len, pass_len;
    err = nvs_get_str(wifi_nvs_handle, "wifi_ssid", wifi_ssid, &ssid_len);  // TODO make sure ssid
    err |= nvs_get_str(wifi_nvs_handle, "wifi_pass", wifi_pass, &pass_len); // and pass are
                                                                            // nul-terminated?

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No wifi credentials stored in NVS");
    } else {
        std::string s(wifi_ssid);
        std::string p(wifi_pass);
        connect_wifi(s, p);
    }

    // set up the UART for incoming Improv bytes
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, UART_BUF_SZ, UART_BUF_SZ, UART_EVT_Q_SZ, &uart0_queue, 0);

    ESP_LOGI(TAG, "wifi and improv setup finished.");

    while (true) {

        uart_event_t event = { };

        if (xQueueReceive(uart0_queue, (void *)&event, (portTickType)portMAX_DELAY)) {
            bzero(dtmp, UART_BUF_SZ);

            if (event.type == UART_DATA) {
                uart_read_bytes(UART_NUM_0, dtmp, event.size, portMAX_DELAY);
                ESP_LOGI(TAG, "uart read %d bytes", event.size);

                for (size_t i = 0; i < event.size; i++) {
                    uint8_t b = dtmp[i];
                    ESP_LOGI(TAG, "handling byte %02X", b);

                    if (parse_improv_serial_byte(x_position, b, x_buffer, on_command_callback, on_error_callback)) {
                        x_buffer[x_position++] = b;
                    } else {
                        x_position = 0;

                        int count = uxTaskGetNumberOfTasks();
                        TaskStatus_t* tasks = (TaskStatus_t*)pvPortMalloc(sizeof(TaskStatus_t) * count);
                        if (tasks != NULL) {
                            uxTaskGetSystemState(tasks, count, NULL);

                            ESP_LOGI(TAG, "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-");
                            for (size_t i = 0; i < count; i++) {
                                ESP_LOGI(TAG, "%s\t\t%d\t\t%d", tasks[i].pcTaskName, tasks[i].uxBasePriority, tasks[i].usStackHighWaterMark);
                            }
                            ESP_LOGI(TAG, "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-");
                        }
                        vPortFree(tasks);

                    }

                }
            } else {
                ESP_LOGI(TAG, "unhandled event type %d", event.type);
            }
        }
    }

    vTaskDelete(NULL);
}

bool connect_wifi(std::string& ssid, std::string& password) {
    uint8_t count = 0;

    memcpy(wifi_config.sta.ssid, ssid.c_str(), ssid.length());
    memcpy(wifi_config.sta.password, password.c_str(), password.length());

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    while (wifi_status != WifiStatus::Connected) {
        vTaskDelay(pdMS_TO_TICKS(500));

        if (count > MAX_ATTEMPTS_WIFI_CONNECTION) {
            return false;
        }
        count++;
    }

    return true;
}

std::vector<std::string> get_local_url() {
    char buf[25];
    snprintf(buf, 25, "http://%s/", ip4addr_ntoa(&ip_info));
    return {
        // TODO
        // URL where user can finish onboarding or use device
        // Recommended to use website hosted by device
        std::string(buf)
    };
}

void on_error_callback(improv::Error err) {
    ESP_LOGE(TAG, "improv error: %02X", err);
}

bool on_command_callback(improv::ImprovCommand cmd) {

    switch (cmd.command) {
        case improv::Command::GET_CURRENT_STATE:
            {
                ESP_LOGD(TAG, "improv cmd GET_CURRENT_STATE");
                if ((wifi_status == WifiStatus::Connected)) {
                    std::vector<std::string> local_url = get_local_url();
                    ESP_LOGD(TAG, "wifi is connected, returning local url %s", local_url[0].c_str());
                    set_state(improv::State::STATE_PROVISIONED);
                    std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_CURRENT_STATE, local_url, false);
                    send_response(data);

                } else {
                    set_state(improv::State::STATE_AUTHORIZED);
                }

                break;
            }

        case improv::Command::WIFI_SETTINGS:
            {
                ESP_LOGD(TAG, "improv cmd WIFI_SETTINGS");
                if (cmd.ssid.length() == 0) {
                    set_error(improv::Error::ERROR_INVALID_RPC);
                    break;
                }

                set_state(improv::STATE_PROVISIONING);

                if (connect_wifi(cmd.ssid, cmd.password)) {
                    ESP_LOGD(TAG, "connect_wifi returned true");

                    set_state(improv::STATE_PROVISIONED);
                    std::vector<uint8_t> data = improv::build_rpc_response(improv::WIFI_SETTINGS, get_local_url(), false);
                    send_response(data);

                } else {
                    ESP_LOGD(TAG, "connect_wifi did not return true");

                    set_state(improv::STATE_STOPPED);
                    set_error(improv::Error::ERROR_UNABLE_TO_CONNECT);
                }

                break;
            }

        case improv::Command::GET_DEVICE_INFO:
            {
                ESP_LOGD(TAG, "improv cmd GET_DEVICE_INFO");

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
                ESP_LOGD(TAG, "improv cmd GET_WIFI_NETWORKS");

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
    esp_err_t err = esp_wifi_scan_start(NULL, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start wifi scan: %s", esp_err_to_name(err));
        return;
    }

    uint16_t network_count = NETWORK_COUNT;
    err = esp_wifi_scan_get_ap_records(&network_count, wifi_ap);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to get wifi scan results: %s", esp_err_to_name(err));
        return;
    }

    // TODO re-introduce feature that sorted by RSSI networks with the same SSID, and discarded all
    // but the strongest.

    for (uint16_t i = 0; i < network_count; ++i) {
        std::vector<uint8_t> data = improv::build_rpc_response(
                improv::GET_WIFI_NETWORKS,
                {
                    std::string((const char*)wifi_ap[i].ssid),
                    std::to_string(wifi_ap[i].rssi),
                    std::string(wifi_ap[i].authmode == WIFI_AUTH_OPEN ? "NO" : "YES")
                },
                false
                );
        send_response(data);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    // final response
    std::vector<uint8_t> data =
        improv::build_rpc_response(improv::GET_WIFI_NETWORKS, std::vector<std::string>{}, false);
    send_response(data);
}
enum State : uint8_t {
  STATE_STOPPED = 0x00,
  STATE_AWAITING_AUTHORIZATION = 0x01,
  STATE_AUTHORIZED = 0x02,
  STATE_PROVISIONING = 0x03,
  STATE_PROVISIONED = 0x04,
};

void set_state(improv::State state) {
    ESP_LOGD(TAG, "setting improv state to %d", state);

    std::vector<char> data = {'I', 'M', 'P', 'R', 'O', 'V'};
    data.resize(11);
    data[6] = improv::IMPROV_SERIAL_VERSION;
    data[7] = improv::TYPE_CURRENT_STATE;
    data[8] = 1;
    data[9] = state;

    uint8_t checksum = 0x00;
    for (char d : data)
        checksum += d;
    data[10] = checksum;

    uart_write_bytes(UART_NUM_0, data.data(), data.size());
}

void send_response(std::vector<uint8_t> &response) {
    std::vector<char> data = {'I', 'M', 'P', 'R', 'O', 'V'};
    data.resize(9);
    data[6] = improv::IMPROV_SERIAL_VERSION;
    data[7] = improv::TYPE_RPC_RESPONSE;
    data[8] = response.size();
    data.insert(data.end(), response.begin(), response.end());

    char checksum = 0x00;
    for (char d : data)
        checksum += d;
    data.push_back(checksum);

    uart_write_bytes(UART_NUM_0, data.data(), data.size());
}

void set_error(improv::Error error) {
    ESP_LOGW(TAG, "improv returning error %d", error);

    std::vector<char> data = {'I', 'M', 'P', 'R', 'O', 'V'};
    data.resize(11);
    data[6] = improv::IMPROV_SERIAL_VERSION;
    data[7] = improv::TYPE_ERROR_STATE;
    data[8] = 1;
    data[9] = error;

    char checksum = 0x00;
    for (char d : data)
        checksum += d;
    data[10] = checksum;

    uart_write_bytes(UART_NUM_0, data.data(), data.size());
}
