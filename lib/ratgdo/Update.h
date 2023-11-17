#ifndef _UPDATE_H
#define _UPDATE_H

class SecPlus2Update {
    public:
        enum Update : uint16_t {
            Unknown = 0x00,
            StatusMsg = 0x81,
            LightToggle = 0x281,
            ObstructionMsg = 0x84,
            MotionToggle = 0x285,
        };

        SecPlus2Update() = default;
        constexpr SecPlus2Update(Update command) : m_update(command) {};

        constexpr operator Update() const { return m_update; };
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
        Update m_update;
};

#endif // _UPDATE_H
