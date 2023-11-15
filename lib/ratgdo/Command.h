#ifndef _COMMAND_H
#define _COMMAND_H

class SecPlus2Command {
    public:
        enum Command : uint16_t {
            Unknown = 0x00,
            StatusMsg = 0x81,
            LightToggle = 0x281,
            ObstructionMsg = 0x84,
            MotionToggle = 0x285,
        };

        SecPlus2Command() = default;
        constexpr SecPlus2Command(Command command) : m_command(command) {};

        constexpr operator Command() const { return m_command; };
        explicit operator bool() const = delete;

        static SecPlus2Command from_byte(uint16_t raw) {
            if (raw == StatusMsg) {
                return SecPlus2Command::StatusMsg;
            } else if (raw == LightToggle) {
                return SecPlus2Command::LightToggle;
            } else if (raw == ObstructionMsg) {
                return SecPlus2Command::ObstructionMsg;
            } else if (raw == MotionToggle) {
                return SecPlus2Command::MotionToggle;
            } else {
                return SecPlus2Command::Unknown;
            }
        };

    private:
        Command m_command;
};

#endif
