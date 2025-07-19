// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef _READER_H
#define _READER_H

#include <secplus2.h>
#include "log.h"

enum SecPlus2ReaderMode : uint8_t {
    SCANNING,
    RECEIVING,
};

class SecPlus2Reader {
    private:
        bool m_is_reading = false;
        uint32_t m_msg_start = 0;
        size_t m_byte_count = 0;
        uint8_t m_rx_buf[SECPLUS2_CODE_LEN] = {0x55, 0x01, 0x00};
        SecPlus2ReaderMode m_mode = SCANNING;

    public:
        SecPlus2Reader() = default;

        bool push_byte(uint8_t inp) {
            bool msg_ready = false;

            switch (m_mode) {
                case SCANNING:
                    m_msg_start <<= 8;
                    m_msg_start |= inp;
                    m_msg_start &= 0x00FFFFFF;

                    if (m_msg_start == SECPLUS2_PREAMBLE) {
                        m_byte_count = 3;
                        m_mode = RECEIVING;
                    }
                    break;

                case RECEIVING:
                    m_rx_buf[m_byte_count] = inp;
                    m_byte_count += 1;

                    if (m_byte_count == SECPLUS2_CODE_LEN) {
                        m_mode = SCANNING;
                        m_msg_start = 0;
                        msg_ready = true;
                    }
                    break;
            }

            if (msg_ready) {
                ESP_LOGI("ratgdo-reader","reader completed packet");
            }
            return msg_ready;
        };

        uint8_t const * const fetch_buf(void) {
            return m_rx_buf;
        }
};

#endif // _READER_H
