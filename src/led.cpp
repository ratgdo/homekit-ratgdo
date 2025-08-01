/****************************************************************************
 * RATGDO HomeKit
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-25 David A Kerr... https://github.com/dkerr64/
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 */

// RATGDO project includes
#include "ratgdo.h"
#include "led.h"

// Logger tag
// static const char *TAG = "ratgdo-led";

// Construct the singleton object for LED access
LED led(LED_BUILTIN);
#ifndef ESP8266
// Feature not available on ESP8266
LED laser(LASER_PIN);
#endif

// Constructor for LED class
LED::LED(uint8_t gpio_num, uint8_t state)
{
    pin = gpio_num;
    activeState = onState = state;
    // off is opposite of on, which can be zero or one.
    currentState = offState = (onState == 1) ? 0 : 1;
    idleState = (activeState == 1) ? 0 : 1;
    LEDtimer = Ticker();
    pinMode(pin, OUTPUT);
}

void LED::on()
{
    digitalWrite(pin, onState);
    currentState = onState;
}

void LED::off()
{
    digitalWrite(pin, offState);
    currentState = offState;
}

void LED::idle()
{
    digitalWrite(pin, idleState);
}

void LED::setIdleState(uint8_t state)
{
    // 0 = LED flashes on (off when idle)
    // 1 = LED flashes off (on when idle)
    // 2 = LED disabled (active and idle both off)
    if (state == 2)
    {
        idleState = activeState = offState;
    }
    else
    {
        idleState = state ? onState : offState;
        // active state is opposite of idle state which can be zero or one.
        activeState = (idleState == 1) ? 0 : 1;
    }
}

void LED::flash(uint64_t ms)
{
    if (!LEDtimer.active())
    {
        // Don't flash if we are already in a flash.
        digitalWrite(pin, activeState);
        LEDtimer.once_ms(ms, [this]()
                         { this->idle(); });
    }
}
