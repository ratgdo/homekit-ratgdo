#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <esp_log.h>

#include <driver/hw_timer.h>
#include <driver/gpio.h>

#include "secplus2.h"

IRAM_ATTR static void tmr_isr(void* arg);
IRAM_ATTR static void handle_tx(void* arg);
IRAM_ATTR static void handle_rx(void* arg);

const size_t SOFTUART_BUF_SZ = SECPLUS2_CODE_LEN * 10;

enum class State : uint8_t {
    Idle,
    Start,
    Data,
    Stop,
};

enum class TransceiverState : uint8_t {
    Idle,
    Transmitting,
    Receiving,
};

struct SoftUart {

    gpio_num_t rx_pin;
    gpio_num_t tx_pin;
    uint32_t speed;
    bool invert;
    uint32_t hw_timer_load_data;
    QueueHandle_t input_q;
    QueueHandle_t tx_q;
    State state;

    TransceiverState trx_state;

    SemaphoreHandle_t tx_flag;

    uint8_t bit_count;
    uint8_t inp_byte;
    uint8_t out_byte;

    SoftUart(gpio_num_t rx_pin, gpio_num_t tx_pin, uint32_t speed, bool invert) :
        rx_pin(rx_pin), tx_pin(tx_pin), speed(speed), invert(invert), state(State::Idle) {

            trx_state = TransceiverState::Idle;

            // FIXME magic
            gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);

            if (speed == 0) {
                ESP_LOGE(TAG, "speed cannot be zero. panicking!");
                abort();
            }

            input_q = xQueueCreate(SOFTUART_BUF_SZ, sizeof(uint8_t));
            if (!input_q) {
                ESP_LOGE(TAG, "could not create rx queue. panicking!");
                abort();
            }

            tx_q = xQueueCreate(SOFTUART_BUF_SZ, sizeof(uint8_t));
            if (!tx_q) {
                ESP_LOGE(TAG, "could not create tx queue. panicking!");
                abort();
            }

            tx_flag =  xSemaphoreCreateBinary();
            if (!tx_flag) {
                ESP_LOGE(TAG, "could not create tx flag. panicking!");
                abort();
            }

            // Calculate bit_time
            uint32_t bit_time_us = (1000000 / speed);
            if (((100000000 / speed) - (100 * bit_time_us)) > 50) {
                bit_time_us++;
            }
            ESP_LOGD(TAG, "bit time is %d", bit_time_us);

            // Setup Rx
            gpio_set_direction(rx_pin, GPIO_MODE_INPUT);
            gpio_set_pull_mode(rx_pin, GPIO_PULLUP_ONLY);

            // Setup Tx
            gpio_set_direction(tx_pin, GPIO_MODE_OUTPUT_OD);
            gpio_set_pull_mode(tx_pin, GPIO_PULLUP_ONLY);

            // IDLE high
            gpio_set_level(tx_pin, !invert);

            // set up the hardware timer, readying it for activation
            ESP_LOGI(TAG, "setting up hw_timer intr");
            hw_timer_init(tmr_isr, (void*)this);
            hw_timer_set_clkdiv(TIMER_CLKDIV_16);
            hw_timer_set_intr_type(TIMER_EDGE_INT);

            hw_timer_load_data = ((TIMER_BASE_CLK >> hw_timer_get_clkdiv()) / 1000000) * bit_time_us;
            ESP_LOGD(TAG, "hw_timer_load_data is %d", hw_timer_load_data);

            //install gpio isr service
            gpio_install_isr_service(0);

            // Setup the interrupt handler to get the start bit
            ESP_LOGI(TAG, "setting up gpio intr");
            gpio_set_intr_type(rx_pin, invert ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);
            gpio_isr_handler_add(rx_pin, tmr_isr, (void *)this);
        };

    State& get_state() {
        return state;
    }

    void set_state(State s) {
        state = s;
    }

    bool transmit(uint8_t bytebuf[], size_t len) {
        ESP_LOGD(TAG, "sending %d bytes", len);

        // disable reception
        gpio_set_intr_type(this->rx_pin, GPIO_INTR_DISABLE);

        if (this->state == State::Idle) {

            // save the byte to be transmitted
            this->out_byte = bytebuf[0];
            // queue the rest, skipping the first byte
            for (size_t i = 1; i < len; i ++) {
                xQueueSendToBack(this->tx_q, &bytebuf[i], 0);
            }
            ESP_LOGD(TAG, "queued bytes, starting transmission");

            // move the state machine
            this->state = State::Start;
            // set the uart as transmitting
            this->trx_state = TransceiverState::Transmitting;

            // wake us up in one bit width and start sending bits. this results in a one-bit-width
            // delay before starting but makes the state machine more elegant
            hw_timer_set_reload(true);
            hw_timer_set_load_data(hw_timer_load_data);
            hw_timer_enable(true);

        } else {
            ESP_LOGE(TAG, "invalid state at tx start %d. abandoning tx", (uint8_t)this->state);
            return false;
        }

        // now block and wait for the tx flag to get set
        if (!xSemaphoreTake(this->tx_flag, pdMS_TO_TICKS(500))) {
            ESP_LOGE(TAG, "transmission of %d bytes never succeeded", len);
            return false;
        }

        this->trx_state = TransceiverState::Idle;

        // re-enable reception
        gpio_set_intr_type(this->rx_pin, this->invert ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);

        return true;
    }

    bool available(void) {
        return uxQueueMessagesWaiting(this->input_q) > 0;
    }

    bool read(uint8_t* out) {
        return xQueueReceive(this->input_q, out, 0);
    }

};

IRAM_ATTR static void tmr_isr(void* arg) {
    SoftUart* uart = (SoftUart*)arg;

    switch (uart->trx_state) {
        case TransceiverState::Idle:
            {
                // this was triggered by the GPIO, and thus we are about to receive
                gpio_set_intr_type(uart->rx_pin, GPIO_INTR_DISABLE);
                uart->trx_state = TransceiverState::Receiving;

                handle_rx(arg);
                break;
            }
        case TransceiverState::Receiving:
            {
                handle_rx(arg);
                break;
            }
        case TransceiverState::Transmitting:
            {
                handle_tx(arg);
                break;
            }
    }
}


IRAM_ATTR static void handle_tx(void* arg) {
    SoftUart* uart = (SoftUart*)arg;

    switch (uart->get_state()) {
        case State::Start:
            {
                gpio_set_level(GPIO_NUM_16, true);
                // set the start bit
                gpio_set_level(uart->tx_pin, uart->invert);

                // set the initial conditions for data bit transmission
                uart->bit_count = 0;

                // move the state machine
                uart->set_state(State::Data);

                gpio_set_level(GPIO_NUM_16, false);
                break;
            }

        case State::Data:
            {
                gpio_set_level(GPIO_NUM_16, true);
                // get the bit to be output
                bool bit = (uart->out_byte & 0x1) ^ uart->invert;
                gpio_set_level(uart->tx_pin, bit);

                // prepare the next bit
                uart->out_byte >>= 1;

                // track which bit we've emitted
                uart->bit_count += 1;

                // if we've written 8 bits
                if (uart->bit_count == 8) {
                    // move the state machine
                    uart->set_state(State::Stop);
                }

                gpio_set_level(GPIO_NUM_16, false);
                break;
            }

        case State::Stop:
            {
                gpio_set_level(GPIO_NUM_16, true);
                // set the stop bit to logic high
                gpio_set_level(uart->tx_pin, !uart->invert);

                // after that we're back to idle and done
                uart->set_state(State::Idle);

                gpio_set_level(GPIO_NUM_16, false);
                break;
            }

        case State::Idle:
            {
                gpio_set_level(GPIO_NUM_16, true);

                if (xQueueReceiveFromISR(uart->tx_q, &uart->out_byte, NULL)) {
                    // there was another byte waiting to be sent
                    uart->set_state(State::Start);
                } else {
                    // disarm the timer and remove the callback (hw_timer might be used for rx)
                    hw_timer_enable(false);

                    // notify caller that transmission has finished
                    xSemaphoreGiveFromISR(uart->tx_flag, NULL);
                }

                gpio_set_level(GPIO_NUM_16, false);
                break;
            }
    }
}

IRAM_ATTR static void handle_rx(void* arg) {
    SoftUart* uart = (SoftUart*)arg;

    switch (uart->get_state()) {
        case State::Idle:
            {

                portENTER_CRITICAL();

                gpio_set_level(GPIO_NUM_16, true);

                // wake us up halfway through the start bit so subsequent sampling is in the center
                hw_timer_set_reload(false);
                hw_timer_set_load_data(uart->hw_timer_load_data >> 2);
                hw_timer_enable(true);

                //hw_timer_alarm_us((uart->bit_time_us / 2) - 50, false);
                // ... except that doesn't work.

                // setting up the hardware timer (above) and setting the alarm (below) takes
                // anywhere from about half a bit time (~42 us) to most of a bit time (~77 us)
                // at 9600 bps, so by the time we're done here, we're in the middle of the start
                // bit (the beginning of which is what woke us up). the soonest the hardware
                // timer can interrupt will be during the first bit, so we set our timers to
                // wake us up then.
                //hw_timer_alarm_us(uart->bit_time_us, true);

                // stop interrupting based on gpio edge
                // v-- already done in tmr_isr
                //gpio_set_intr_type(uart->rx_pin, GPIO_INTR_DISABLE);

                // move the state machine
                uart->set_state(State::Start);

                gpio_set_level(GPIO_NUM_16, false);

                portEXIT_CRITICAL();

                break;
            }

        case State::Start:
            {

                portENTER_CRITICAL();
                gpio_set_level(GPIO_NUM_16, true);

                // set the initial conditions for reading a byte
                uart->bit_count = 0;
                uart->inp_byte = 0;

                // set a reloadable timer to wake us up repeatedly for the next 8 bits
                hw_timer_set_reload(true);
                hw_timer_set_load_data(uart->hw_timer_load_data);
                hw_timer_enable(true);

                // move the state machine
                uart->set_state(State::Data);

                gpio_set_level(GPIO_NUM_16, false);
                portEXIT_CRITICAL();

                break;
            }

        case State::Data:
            {
                gpio_set_level(GPIO_NUM_16, true);
                // shift the input byte in order to store the next most-significant bit
                uart->inp_byte >>= 1;
                // sample the bit
                if (gpio_get_level(uart->rx_pin) ^ uart->invert) {
                    // set the top bit of the byte (0b1000_0000)
                    uart->inp_byte |= 0x80;
                } else {
                }
                // move to the next bit
                uart->bit_count += 1;

                // if we've read 8 data bits, move the state machine
                if (uart->bit_count == 8) {
                    uart->set_state(State::Stop);
                }
                gpio_set_level(GPIO_NUM_16, false);
                break;
            }

        case State::Stop:
            {
                gpio_set_level(GPIO_NUM_16, true);
                // if STOP bit is logic-high there's (presumably) no framing error, so use the byte
                if (gpio_get_level(uart->rx_pin) ^ uart->invert) {
                    xQueueSendToBackFromISR(uart->input_q, &(uart->inp_byte), NULL);
                    // we ignore the return value when pushing to the queue because we can't
                    // recover from overflow
                } else {
                    ESP_EARLY_LOGE(TAG, "rx frame err");
                }

                // stop the timer
                hw_timer_enable(false);

                // move the state machine
                uart->set_state(State::Idle);

                uart->trx_state = TransceiverState::Idle;

                // start interrupting on gpio edges again
                gpio_set_intr_type(uart->rx_pin, uart->invert ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);
                gpio_set_level(GPIO_NUM_16, false);
                break;
            }
    }
}
