#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <esp_log.h>
#include <esp_timer.h>

#include <driver/hw_timer.h>
#include <driver/gpio.h>

#include "secplus2.h"
#include "tasks.h"

// enable to emit an ISR edge on D0 aka GPIO_NUM_16
#define ISR_DEBUG

struct SoftUart;

IRAM_ATTR static void handle_tx(void* arg);
IRAM_ATTR void handle_rx_edge(SoftUart* uart);
void rx_isr_handler_entry(SoftUart* uart);

// we can buffer up to 10 complete Security+ 2.0 packets to transmit
const size_t BYTE_Q_BUF_SZ = SECPLUS2_CODE_LEN * 10;

// we can store up to 5 complete Security+ 2.0 packets' worth of data w/ value 0x55
const size_t ISR_Q_BUF_SZ = 10 /* bits per byte */ * SECPLUS2_CODE_LEN /* bytes per packet */ * 5 /* packets */;

enum class State : uint8_t {
    Idle,
    Start,
    Data,
    Stop,
};

struct ISREvent {
    int64_t ticks;
    bool level;
};

struct SoftUart {

    gpio_num_t rx_pin;
    gpio_num_t tx_pin;
    uint32_t speed; // bits per second
    uint32_t bit_time_us; // microseconds per bit
    bool invert;
    bool one_wire;

    QueueHandle_t tx_q;
    State tx_state = State::Idle;
    uint8_t tx_bit_count = 0;
    uint8_t tx_byte = 0;

    // task that receives and processes GPIO edge ISREvents
    TaskHandle_t rx_task;

    // queue to handle bit edges
    QueueHandle_t rx_isr_q;
    // queue to handle fully-received bytes
    QueueHandle_t rx_q;
    State rx_state = State::Idle;
    uint8_t rx_bit_count = 0;
    uint8_t rx_byte = 0;
    uint32_t last_isr_ticks = 0;
    bool last_isr_level = true;  // default to active-high

    SemaphoreHandle_t tx_flag;

    SoftUart(gpio_num_t rx_pin, gpio_num_t tx_pin, uint32_t speed, bool invert = false, bool one_wire = false) :
        rx_pin(rx_pin), tx_pin(tx_pin), speed(speed), invert(invert), one_wire(one_wire) {

            if (speed == 0) {
                ESP_LOGE(TAG, "speed cannot be zero. panicking!");
                abort();
            }

            rx_q = xQueueCreate(BYTE_Q_BUF_SZ, sizeof(uint8_t));
            if (!rx_q) {
                ESP_LOGE(TAG, "could not create rx byte queue. panicking!");
                abort();
            }

            rx_isr_q = xQueueCreate(ISR_Q_BUF_SZ, sizeof(ISREvent));
            if (!rx_q) {
                ESP_LOGE(TAG, "could not create rx isr queue. panicking!");
                abort();
            }

            tx_q = xQueueCreate(BYTE_Q_BUF_SZ, sizeof(uint8_t));
            if (!tx_q) {
                ESP_LOGE(TAG, "could not create tx byte queue. panicking!");
                abort();
            }

            tx_flag =  xSemaphoreCreateBinary();
            if (!tx_flag) {
                ESP_LOGE(TAG, "could not create tx flag. panicking!");
                abort();
            }

            xTaskCreate(
                    reinterpret_cast<void (*)(void*)>(rx_isr_handler_entry),
                    RX_ISR_TASK_NAME,
                    RX_ISR_TASK_STK_SZ,
                    this,
                    RX_ISR_TASK_PRIO,
                    &this->rx_task
                    );

            // Calculate bit_time
            this->bit_time_us = (1000000 / speed);
            if (((100000000 / speed) - (100 * bit_time_us)) > 50) {
                this->bit_time_us++;
            }
            ESP_LOGD(TAG, "bit time is %d", bit_time_us);

            // Setup Rx
            gpio_set_direction(rx_pin, GPIO_MODE_INPUT);
            gpio_set_pull_mode(rx_pin, GPIO_PULLUP_ONLY);

            // Setup Tx
            gpio_set_direction(tx_pin, GPIO_MODE_OUTPUT_OD);
            gpio_set_pull_mode(tx_pin, GPIO_PULLUP_ONLY);

#ifdef ISR_DEBUG
            gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
            gpio_set_pull_mode(GPIO_NUM_16, GPIO_PULLUP_ONLY);
#endif

            //install gpio isr service
            gpio_install_isr_service(0);

            // IDLE high
            gpio_set_level(tx_pin, !invert);

            // set up the hardware timer, readying it for activation
            ESP_LOGD(TAG, "setting up hw_timer intr");
            hw_timer_init(handle_tx, (void*)this);
            hw_timer_set_clkdiv(TIMER_CLKDIV_16);
            hw_timer_set_intr_type(TIMER_EDGE_INT);

            uint32_t hw_timer_load_data = ((TIMER_BASE_CLK >> hw_timer_get_clkdiv()) / 1000000) * bit_time_us;
            ESP_LOGD(TAG, "hw_timer_load_data is %d", hw_timer_load_data);
            hw_timer_set_load_data(hw_timer_load_data);
            hw_timer_set_reload(true);

            // Setup the interrupt handler to get edges
            ESP_LOGD(TAG, "setting up gpio intr");
            gpio_set_intr_type(this->rx_pin, GPIO_INTR_ANYEDGE);
            gpio_isr_handler_add(this->rx_pin, reinterpret_cast<void (*)(void*)>(handle_rx_edge), (void *)this);

        };

    bool transmit(uint8_t bytebuf[], size_t len) {
        ESP_LOGD(TAG, "sending %d bytes", len);

        if (this->tx_state == State::Idle) {

            // move the state machine
            this->tx_state = State::Start;

            if (this->one_wire) {
                // disable reception
                gpio_set_intr_type(this->rx_pin, GPIO_INTR_DISABLE);
            }

            // wake us up in one bit width and start sending bits. this results in a one-bit-width
            // delay before starting but makes the state machine more elegant
            hw_timer_enable(true);

            // save the byte to be transmitted
            this->tx_byte = bytebuf[0];
            // queue the rest, skipping the first byte
            for (size_t i = 1; i < len; i ++) {
                xQueueSendToBack(this->tx_q, &bytebuf[i], 0);
            }
            ESP_LOGD(TAG, "queued bytes, starting transmission");

        } else {
            ESP_LOGE(TAG, "invalid state at tx start %d. abandoning tx", (uint8_t)this->tx_state);
            return false;
        }

        bool ret = true;

        // now block and wait for the tx flag to get set
        if (!xSemaphoreTake(this->tx_flag, pdMS_TO_TICKS(500))) {
            ESP_LOGE(TAG, "transmission of %d bytes never succeeded", len);
            ret = false;
        }

        // re-enable reception
        gpio_set_intr_type(this->rx_pin, GPIO_INTR_ANYEDGE);

        return ret;
    }

    bool available(void) {
        return uxQueueMessagesWaiting(this->rx_q) > 0;
    }

    bool read(uint8_t* out) {
        return xQueueReceive(this->rx_q, out, 0);
    }

    void process_isr(ISREvent& e) {
        int64_t ticks = e.ticks - this->last_isr_ticks;

        // calculate how many bit periods it's been since the last edge
        uint32_t bits = ticks / this->bit_time_us;
        if ((ticks % this->bit_time_us) > (this->bit_time_us / 2)) {
            bits += 1;
        }

        // inspect each bit period, moving the state machine accordingly
        while (bits) {

            switch (this->rx_state) {

                case State::Idle:
                    {
                        if (e.level == this->last_isr_level) {
                            // nothing has changed since the last interrupt, which means this is a
                            // timeout when we were already idle. we can just break out of it
                            // instead of continuing to loop for no reason
                            bits = 1; // decremented to zero at the end of the loop
                            break;
                        }

                        // if this is the last bit period before a low, then we're entering the
                        // Start state
                        if ((e.level ^ this->invert) == false && bits == 1) {
                            this->rx_state = State::Start;
                            this->rx_bit_count = 0;
                        }
                        break;
                    }

                case State::Start:
                    {
                        if (this->last_isr_level ^ this->invert) {
                            // start cannot be a logic HIGH. how did we even get here?
                            this->rx_state = State::Idle;
                            this->rx_bit_count = 0;
                        } else {
                            // this bit period carries no value so we move the state machine and
                            // that's it
                            this->rx_state = State::Data;
                        }
                        break;
                    }

                case State::Data:
                    {
                        // make room for the incoming bit
                        this->rx_byte >>= 1;

                        // take the prior level as the value for this bit period which occurred
                        // before the edge (i.e. level change)
                        if (this->last_isr_level ^ this->invert) {
                            this->rx_byte |= 0x80;
                        }
                        this->rx_bit_count += 1;

                        // if that was the 8th bit, we're done with data bits and the next is a STOP
                        // bit
                        if (this->rx_bit_count == 8) {
                            this->rx_state = State::Stop;
                        }
                        break;
                    }

                case State::Stop:
                    {
                        // if the value during this bit period was logic-high, there's
                        // presumably no framing error and we should keep the byte
                        if (this->last_isr_level ^ this->invert) {
                            ESP_LOGD(TAG, "byte complete %02X", this->rx_byte);
                            xQueueSendToBack(this->rx_q, &this->rx_byte, 0);
                            this->rx_byte = 0;
                            this->rx_bit_count = 0;
                        }

                        // if this is the last bit period before the detected edge, then we're
                        // entering the Start state
                        if (bits == 1) {
                            this->rx_state = State::Start;
                        } else {
                            // if we still have more bit periods to go, the we idle for a bit
                            this->rx_state = State::Idle;
                        }

                        break;
                    }

            } // switch (this->rx_state)

            // consume the bit period
            bits -= 1;
        } // while (bits)

        this->last_isr_ticks = e.ticks;
        this->last_isr_level = e.level;
    }

};

IRAM_ATTR static void handle_tx(void* arg) {
    SoftUart* uart = (SoftUart*)arg;

    switch (uart->tx_state) {
        case State::Start:
            {
                // set the start bit
                gpio_set_level(uart->tx_pin, uart->invert);

                // set the initial conditions for data bit transmission
                uart->tx_bit_count = 0;

                // move the state machine
                uart->tx_state = State::Data;

                break;
            }

        case State::Data:
            {
                // get the bit to be output
                bool bit = (uart->tx_byte & 0x1) ^ uart->invert;
                gpio_set_level(uart->tx_pin, bit);

                // prepare the next bit
                uart->tx_byte >>= 1;

                // track which bit we've emitted
                uart->tx_bit_count += 1;

                // if we've written 8 bits
                if (uart->tx_bit_count == 8) {
                    // move the state machine
                    uart->tx_state = State::Stop;
                }

                break;
            }

        case State::Stop:
            {
                // set the stop bit to logic high
                gpio_set_level(uart->tx_pin, !uart->invert);

                // after that we're back to idle and done
                uart->tx_state = State::Idle;

                break;
            }

        case State::Idle:
            {

                if (xQueueReceiveFromISR(uart->tx_q, &uart->tx_byte, NULL)) {
                    // there was another byte waiting to be sent
                    uart->tx_state = State::Start;
                } else {
                    // disarm the timer and remove the callback (hw_timer might be used for rx)
                    hw_timer_enable(false);

                    // notify caller that transmission has finished
                    xSemaphoreGiveFromISR(uart->tx_flag, NULL);
                }

                break;
            }
    }

}

void rx_isr_handler_entry(SoftUart* uart) {
    // STOP bits sometimes aren't preceded by a transition if the bits preceding it are all zeroes.
    // this is the number of milliseconds it takes a whole byte (1 start + 8 data + 1 stop bits) to
    // arrive, plus one. we use this as a timeout to know when to consider a byte "complete".
    //
    uint32_t byte_timeout_ms = (10000 / uart->speed) + 1;
    while (true) {
        ISREvent e;
        if (xQueueReceive(uart->rx_isr_q, &e, pdMS_TO_TICKS(byte_timeout_ms))) {
            // got a bit
            uart->process_isr(e);
        } else {
            int64_t ct = esp_timer_get_time();
            // didn't get a bit
            //
            // if we get here, we're waiting for more bits, but we've timed out waiting for them
            if (uart->rx_state != State::Idle) {
                e = {
                    // why can we pick any old timestamp here? well, because the state machine
                    // permits us to do so. the time since the last interrupt will get chopped into
                    // some number of bit periods. we know there has been no transition since then,
                    // so we know every bit period has the same value. even if the calculated bit
                    // periods don't match the real ones, there's no difference because there's no
                    // edge, so it doesn't matter "when" the "samples" happen. and even if there are
                    // "too many" bit periods for the remaining number of bits, we no-op the
                    // State::Idle bit periods (TODO: add an optimization to skip them entirely)
                    ct,
                    gpio_get_level(uart->rx_pin)
                };
                uart->process_isr(e);
            }
        }
    }
}

IRAM_ATTR void handle_rx_edge(SoftUart* uart) {
#ifdef ISR_DEBUG
    gpio_set_level(GPIO_NUM_16, true);
#endif
    ISREvent e = {
        esp_timer_get_time(),
        gpio_get_level(uart->rx_pin)
    };
    xQueueSendToBackFromISR(uart->rx_isr_q, &e, NULL);
#ifdef ISR_DEBUG
    gpio_set_level(GPIO_NUM_16, false);
#endif
}
