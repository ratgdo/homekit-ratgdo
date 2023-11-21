#ifndef _COMMAND_H
#define _COMMAND_H

#include <secplus2.h>
#include <secplus.h>
#include "log.h"

class SecPlus2Command {

    public:
        enum Command : uint8_t {
            Sync = 0,
            Reboot,
            Door,
            /* TODO add support for light and lock
             * Light,
             * Lock,
             */
        };

        SecPlus2Command() = default;
        constexpr SecPlus2Command(Command command) : m_command(command) {};

        constexpr operator Command() const { return m_command; };
        explicit operator bool() const = delete;

        void prepare(const uint64_t id, uint32_t* rolling, auto xmit) {
            RINFO("preparing command %02X", m_command);

            uint8_t n;
            SecPlus2CommandDatum* data;

            switch (m_command) {
                case Sync:
                    n = m_sync_sz;
                    data = m_sync;
                    RINFO("sync");
                    break;
                case Reboot:
                    n = m_reboot_sz;
                    data = m_reboot;
                    RINFO("reboot");
                    break;
                case Door:
                    n = m_door_sz;
                    data = m_door;
                    RINFO("door");
                    break;
                default:
                    RINFO("unknown cmd");
                    return;
            }

            for (uint8_t i = 0; i < n; i++) {
                uint64_t fixed = data[i].fixed | id;
                uint8_t buf[SECPLUS2_CODE_LEN] = {0};

                encode_wireline(*rolling, fixed, data[i].data, buf);

                xmit(buf);

                if (data[i].inc_rolling) {
                    *rolling = (*rolling + 1) & 0x0fffFFFF;
                }
            }
        }


    private:
        Command m_command;

        struct SecPlus2CommandDatum {
            uint64_t fixed;
            uint32_t data;
            bool inc_rolling;
        };

        // TODO base this on the actual communication model and not just blind replay
        //
        // for an explanation of the following values, see:
        //  https://github.com/ratgdo/esphome-ratgdo/blob/5a0294/components/ratgdo/ratgdo.h#L58
        //  https://github.com/ratgdo/esphome-ratgdo/blob/5a0294/components/ratgdo/ratgdo.cpp#L621
        //  and so on

        static const uint8_t m_sync_sz = 6;
        SecPlus2CommandDatum m_sync[m_sync_sz] = {
            { 0x400000000, 0x0000618b, true },
            { 0x0, 0x01009080, true },
            { 0x0, 0x0000b1a0, true },
            { 0x0, 0x01009080, true },
            { 0x300000000, 0x00008092, true },
            { 0x300000000, 0x00008092, true },
        };

        static const uint8_t m_reboot_sz = 1;
        SecPlus2CommandDatum m_reboot[m_reboot_sz] = {
            { 0x0, 0x01009080, true },
        };

        static const uint8_t m_door_sz = 2;
        SecPlus2CommandDatum m_door[m_door_sz] = {
            { 0x200000000, 0x01018280, false },
            { 0x200000000, 0x01009280, true },
        };

        /* TODO add support for lights and lock
        SecPlus2CommandDatum m_light[1] = {
            { 0x200000000, 0x00009281, true },
        };

        SecPlus2CommandDatum m_lock[1] = {
            { 0x0100000000, 0x0000728c, true },
        };
         */
};

#endif // _COMMAND_H
