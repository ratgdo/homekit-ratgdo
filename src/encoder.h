/****************************************************************************
 * RATGDO Encoder Support
 *
 * Copyright (c) 2023-26 homekit-ratgdo contributors
 * Licensed under terms of the GPL-3.0 License.
 *
 * Encoder A = DRY_CONTACT_OPEN_PIN (GPIO 13)
 * Encoder B = DRY_CONTACT_CLOSE_PIN (GPIO 14)
 *
 * When encoder mode is enabled the limit-switch (OneButton) handling on
 * those two pins is skipped and replaced by this ISR-based encoder.
 */
#pragma once

void setup_encoder();
void encoder_loop();           // call from drycontact_loop() every main-loop tick
void reset_encoder_cal();      // called by web handler "resetEncoderCal"
void encoder_set_intended_open();
void encoder_set_intended_close();
int16_t encoder_last_step();   // current raw step value (for status JSON)
