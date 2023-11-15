#ifndef _DECODER_H
#define _DECODER_H

#include <secplus.h>
#include <secplus2.h>
#include <Status.h>
#include <Command.h>

class PacketDecoder {
    private:
        secplus_door_status_cb m_door_status_cb;

    public:
        PacketDecoder() = default;

        void set_door_status_cb(secplus_door_status_cb cb) {
            m_door_status_cb = cb;
        };
        void handle_code(uint8_t packet[SECPLUS2_CODE_LEN]) {
            uint32_t rolling = 0;
            uint64_t fixed = 0;
            uint32_t data = 0;

            uint8_t nibble = 0;
            /* TODO add support for lights, obstruction, etc
               uint8_t byte1 = 0;
               uint8_t byte2 = 0;
               */

            decode_wireline(packet, &rolling, &fixed, &data);

            SecPlus2Command cmd = SecPlus2Command::from_byte(((fixed >> 24) & 0xf00) | (data & 0xff));

            nibble = (data >> 8) & 0xf;
            /* TODO add support for lights, obstruction, etc
               byte1 = (data >> 16) & 0xff;
               byte2 = (data >> 24) & 0xff;
               */

            switch (cmd) {
                case SecPlus2Command::StatusMsg:
                    this->m_door_status_cb(SecPlus2DoorStatus::from_byte(nibble));
                    break;
                case SecPlus2Command::LightToggle:
                    break;
                case SecPlus2Command::ObstructionMsg:
                    break;
                case SecPlus2Command::MotionToggle:
                    break;
                case SecPlus2Command::Unknown:
                    break;
            }

        };
};

#endif // _DECODER_H
