// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License
#define TAG ("RATGDO")

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>

#include "ratgdo.h"
#include "wifi.h"
#include "homekit.h"
#include "comms.h"
#include "log.h"
// #include "web.h"
#include "tasks.h"

/********************************* FWD DECLARATIONS *****************************************/

void setup_pins();
// void isr_obstruction();  // TODO obstruction refactor
void motion_timer_cb(void* ctx);
void led_on_timer_cb(void* ctx);

/********************************* RUNTIME STORAGE *****************************************/

struct obstruction_sensor_t {
    uint32_t low_count = 0;        // count obstruction low pulses
    bool detected = false;
    uint32_t last_high = 0;       // count time between high pulses from the obst ISR
} obstruction_sensor;

// This timer is reset when a packet is sent or received.
TimerHandle_t led_on_timer;

// This timer is periodically reset by incoming packets when motion is detected.
TimerHandle_t motion_timer;

/********************************** MAIN LOOP CODE *****************************************/

extern "C" void app_main() {
    ESP_ERROR_CHECK(uart_set_baudrate(UART_NUM_0, 115200));
    ESP_LOGI(TAG, "RATGDO main app starting");

    // core setup
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_set_cpu_freq(ESP_CPU_FREQ_160M); // returns void
    tcpip_adapter_init();  // returns void
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Print chip information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP8266 chip with %d CPU cores, WiFi, ",
            chip_info.cores);
    printf("silicon revision %d, ", chip_info.revision);
    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    xTaskCreate(wifi_task_entry, WIFI_TASK_NAME, WIFI_TASK_STK_SZ, NULL, WIFI_TASK_PRIO, NULL);

    setup_pins();

    xTaskCreate(comms_task_entry, COMMS_TASK_NAME, COMMS_TASK_STK_SZ, NULL, COMMS_TASK_PRIO, NULL);

    xTaskCreate(homekit_task_entry, HOMEKIT_TASK_NAME, HOMEKIT_TASK_STK_SZ, NULL, HOMEKIT_TASK_PRIO, NULL);

    // setup_web();  // TODO

    ESP_LOGI(TAG, "RATGDO setup completed");
    ESP_LOGI(TAG, "Starting RATGDO Homekit version %s", "esptest");  // TODO
    ESP_LOGI(TAG, "%s", IDF_VER);

    // improv_loop();

    // comms_loop();

    // homekit_loop();

    // web_loop();

    // service_timer_loop();
}

/*********************************** HELPER FUNCTIONS **************************************/

void setup_pins() {
    ESP_LOGI(TAG, "Setting up pins");
    gpio_install_isr_service(0);  // meaningless zero to appease the API

    if (UART_TX_PIN != LED_BUILTIN) {
        ESP_LOGI(TAG, "enabling built-in LED");
        gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);
        gpio_set_level(LED_BUILTIN, false);

        led_on_timer = xTimerCreate(
            "led_timer",
            pdMS_TO_TICKS(500),
            false,
            NULL,
            led_on_timer_cb
        );
    }

    motion_timer = xTimerCreate(
        "motion_timer",
        pdMS_TO_TICKS(5000),
        false,
        NULL,
        motion_timer_cb
    );

    /* TODO obstruction refactor
    gpio_set_direction(INPUT_OBST_PIN, GPIO_MODE_INPUT);
    */

    /*
     * TODO add support for dry contact switches
    pinMode(STATUS_DOOR_PIN, OUTPUT);
    */
    /* TODO obstruction refactor
    gpio_set_direction(STATUS_OBST_PIN, GPIO_MODE_OUTPUT);
    */
    /*
    pinMode(DRY_CONTACT_OPEN_PIN, INPUT_PULLUP);
    pinMode(DRY_CONTACT_CLOSE_PIN, INPUT_PULLUP);
    pinMode(DRY_CONTACT_LIGHT_PIN, INPUT_PULLUP);
    */

    /* pin-based obstruction detection
    // FALLING from https://github.com/ratgdo/esphome-ratgdo/blob/e248c705c5342e99201de272cb3e6dc0607a0f84/components/ratgdo/ratgdo.cpp#L54C14-L54C14
     */
    /* TODO obstruction refactor
    gpio_set_intr_type(INPUT_OBST_PIN, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(INPUT_OBST_PIN, isr_obstruction);
    */
}

/*********************************** MODEL **************************************/

struct GarageDoor garage_door;

/*************************** OBSTRUCTION DETECTION ***************************/
/* TODO obstruction refactor
void isr_obstruction() {
    if (gpio_get_level(INPUT_OBST_PIN)) {
        obstruction_sensor.last_high = esp_get_time();
    } else {
        obstruction_sensor.detected = true;
        obstruction_sensor.low_count++;
    }
}

void obstruction_timer() {
    if (!obstruction_sensor.detected)
        return;
    unsigned long current_millis = esp_get_time();
    static unsigned long last_millis = 0;

    // the obstruction sensor has 3 states: clear (HIGH with LOW pulse every 7ms), obstructed (HIGH), asleep (LOW)
    // the transitions between awake and asleep are tricky because the voltage drops slowly when falling asleep
    // and is high without pulses when waking up

    // If at least 3 low pulses are counted within 50ms, the door is awake, not obstructed and we don't have to check anything else

    // Every 50ms
    if (current_millis - last_millis > 50) {
        // check to see if we got between 3 and 8 low pulses on the line
        if (obstruction_sensor.low_count >= 3 && obstruction_sensor.low_count <= 8) {
            // Only update if we are changing state
            if (garage_door.obstructed) {
                ESP_LOGI(TAG, "Obstruction Clear");
                garage_door.obstructed = false;
                notify_homekit_obstruction();
                gpio_set_level(STATUS_OBST_PIN, garage_door.obstructed);
            }

            // if there have been no pulses the line is steady high or low
        } else if (obstruction_sensor.low_count == 0) {
            // if the line is high and the last high pulse was more than 70ms ago, then there is an obstruction present
            if (digitalRead(INPUT_OBST_PIN) && current_millis - obstruction_sensor.last_high > 70) {
                // Only update if we are changing state
                if (!garage_door.obstructed) {
                    ESP_LOGI(TAG, "Obstruction Detected");
                    garage_door.obstructed = true;
                    notify_homekit_obstruction();
                    gpio_set_level(STATUS_OBST_PIN, garage_door.obstructed);
                }
            }
        }

        last_millis = current_millis;
        obstruction_sensor.low_count = 0;
    }
}
*/

void led_on_timer_cb(void* ctx) {
    // LED Timer
    gpio_set_level(LED_BUILTIN, false);
}

void motion_timer_cb(void* ctx) {
    // Motion Clear Timer
    ESP_LOGI(TAG, "Motion Cleared");
    garage_door.motion = false;
    notify_homekit_motion();
}

uint8_t system_get_cpu_freq() {
    return 160;
}
