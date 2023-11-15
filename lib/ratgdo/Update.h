#ifndef _COMMAND_H
#define _COMMAND_H

class SecPlus2Update {
    public:
        enum Command : uint16_t {
            Unknown = 0x00,
            StatusMsg = 0x81,
            LightToggle = 0x281,
            ObstructionMsg = 0x84,
            MotionToggle = 0x285,
        };

        SecPlus2Update() = default;
        constexpr SecPlus2Update(Command command) : m_command(command) {};

        constexpr operator Command() const { return m_command; };
        explicit operator bool() const = delete;

        static SecPlus2Update from_byte(uint16_t raw) {
            if (raw == StatusMsg) {
                return SecPlus2Update::StatusMsg;
            } else if (raw == LightToggle) {
                return SecPlus2Update::LightToggle;
            } else if (raw == ObstructionMsg) {
                return SecPlus2Update::ObstructionMsg;
            } else if (raw == MotionToggle) {
                return SecPlus2Update::MotionToggle;
            } else {
                return SecPlus2Update::Unknown;
            }
        };

    private:
        Command m_command;
};

#endif
