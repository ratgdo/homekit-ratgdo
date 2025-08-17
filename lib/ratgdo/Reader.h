/****************************************************************************
 * RATGDO HomeKit
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-24 Brandon Matthews... https://github.com/thenewwazoo
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 */
#pragma once

#include <secplus2.h>

enum SecPlus2ReaderMode : uint8_t
{
    SCANNING,
    RECEIVING,
};

class SecPlus2Reader
{
private:
    bool m_is_reading = false;
    uint32_t m_msg_start = 0;
    size_t m_byte_count = 0;
    uint8_t m_rx_buf[SECPLUS2_CODE_LEN] = {0x55, 0x01, 0x00};
    SecPlus2ReaderMode m_mode = SCANNING;
    const char *TAG = "ratgdo-reader";

public:
    SecPlus2Reader() = default;

    bool push_byte(uint8_t inp)
    {
        bool msg_ready = false;

        switch (m_mode)
        {
        case SCANNING:
            m_msg_start <<= 8;
            m_msg_start |= inp;
            m_msg_start &= 0x00FFFFFF;

            if (m_msg_start == SECPLUS2_PREAMBLE)
            {
                m_byte_count = 3;
                m_mode = RECEIVING;
            }
            break;

        case RECEIVING:
            m_rx_buf[m_byte_count] = inp;
            m_byte_count += 1;

            if (m_byte_count == SECPLUS2_CODE_LEN)
            {
                m_mode = SCANNING;
                m_msg_start = 0;
                msg_ready = true;
            }
            break;
        }

        /* reduce noise in logs
        if (msg_ready)
        {
            ESP_LOGI(TAG, "reader completed packet");
        }
        */
        return msg_ready;
    };

    uint8_t *fetch_buf(void)
    {
        return m_rx_buf;
    }
};
