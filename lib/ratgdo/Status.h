#ifndef _STATUS_H
#define _STATUS_H

class SecPlus2DoorStatus {
    public:
        enum Status : uint8_t {
            Unknown,
            Open,
            Closed,
            Stopped,
            Opening,
            Closing,
            Syncing
        };

        SecPlus2DoorStatus() = default;
        constexpr SecPlus2DoorStatus(Status status) : m_status(status) {};

        constexpr operator Status() const { return m_status; };
        explicit operator bool() const = delete;

        static SecPlus2DoorStatus from_byte(uint8_t raw) {
            switch (raw) {
                case Open:
                    return SecPlus2DoorStatus::Open;
                case Closed:
                    return SecPlus2DoorStatus::Closed;
                case Stopped:
                    return SecPlus2DoorStatus::Stopped;
                case Opening:
                    return SecPlus2DoorStatus::Opening;
                case Closing:
                    return SecPlus2DoorStatus::Closing;
                case Syncing:
                    return SecPlus2DoorStatus::Syncing;
                default:
                    return SecPlus2DoorStatus::Unknown;
            }
        };

    private:
        Status m_status;
};

typedef void (*secplus_door_status_cb)(SecPlus2DoorStatus door_status);

#endif
