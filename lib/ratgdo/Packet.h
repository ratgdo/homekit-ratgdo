/****************************************************************************
 * RATGDO HomeKit for ESP32
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-24 Brandon Matthews... https://github.com/thenewwazoo
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 * Contributions acknowledged from
 * David Kerr...     https://github.com/dkerr64
 * 
 */
#pragma once

#include <stdint.h>
#include "secplus2.h"
#include <secplus.h>

// Chamberlain security+ 2.0 wireline packets (i.e. 0x55, 0x10, 0x00, ...) all decode (using
// `decode_wireline`) into 16 bytes, split across three values:
//
// "rolling" 32 bits - the per-device, 24-bit monotonically incrementing value included with every packet
// "fixed"   64 bits - the value that includes the device ID, as well as the high nibble of the 12-bit command
// "data"    32 bits - flags and values associated with the command, and the low byte of the 12-bit command
//
// [rolling]
//   The "rolling code" is a 24-bit value in the low three bytes that monotonically increases with
//   (almost) every transmitted packet from a given device (as identified by its 24-bit device ID).
//   Packets that are the same or lower than prior packets are discarded by the garage door opener.
//
// [fixed]
//   The "fixed" value contains the 24-bit client ID in the low three bytes, and one nibble of
//   "command" in the low half of byte 4.
//
//     7        6        5        4        3        2        1        0
// |--------|--------|--------|--------|--------|--------|--------|--------| 64 bits
//                                              [76543210 76543210 76543210] <-- client ID
//                                [3210] <-------------------------------------- command nibble
//
//   Byte 3, above, is used, but its purpose is unknown.
//
//   Client IDs are remembered by the garage door opener, but can be chosen at random. The first
//   (few?) packets transmitted by a device ID are discarded until such time as the garage door
//   opener remembers it, and can therefore be picked at random (and, indeed, discarded at bootup
//   time if desired).
//
// [data]
//   The bulk of the following code is written to handle the "data" packet, which includes a command
//   in its low two bytes, a parity nibble in the high half of byte 1, and 20 bits of "data".
//
//     3        2        1        0
// |--------|--------|--------|--------| 64 bits
//                            [76543210] <-- command
//                   [7654] <--------------- parity
// [76543210 76543210]   [3210]  <---------- data
//
//   Each command has its own data layout, with various bits meaning various things. Where relevant
//   to serialization and deserialization required to implement HomeKit support, a struct is defined
//   to represent the data, along with de/serialization methods to pack the values into the
//   appropriate bits.
//
//   Because C++ is a garbage language, bitfields are broken by design, so the elegant method of
//   specifying bit layout in order to show which bits do which things doesn't work portably. As
//   such, we get to use mask-and-shift values.
//
//
// A note on unknowns:
//
//   There are bits and bytes observed "in the wild" that are not (or nearly not) accounted for
//   here. Byte 3 in the "fixed" 64-bit value has been observed but other implementations don't read
//   or use it. There are some bits in the Status packet that I likewise could not find documented;
//   I named those "unknown" in the Status struct but don't use or print them.
//
//   There are many command types that are not implemented here, as they were not necessary for
//   implementing HomeKit support. That does not mean I would not like a more-complete
//   implementation.

// Tag for union of data structures attached to packets
enum class PacketDataType
{
    NoData,
    Status,
    Light,
    Lock,
    DoorAction,
    Openings,
    Battery,
    Unknown,
};

// Parity is applicable to all incoming packets; outgoing packets leave this unset
const uint8_t COMMAND_PARITY_MASK = 0b1111;
const uint8_t COMMAND_PARITY_SHIFT = 12;

const uint8_t DOOR_ACTION_MASK = 0b11;
const uint8_t DOOR_ACTION_SHIFT = 8;
// valid values for DoorActionCommandData
enum class DoorAction : uint8_t
{
    Close = 0,
    Open = 1,
    Toggle = 2,
    Stop = 3,
};

const uint8_t DOOR_ACTION_PRESSED_MASK = 0b1;
const uint8_t DOOR_ACTION_PRESSED_SHIFT = 16;
const uint8_t DOOR_ACTION_ID_MASK = 0b11; // total guess
const uint8_t DOOR_ACTION_ID_SHIFT = 24;
// data attached to PacketCommand::DoorAction
struct DoorActionCommandData
{
    DoorAction action;
    uint8_t parity;
    bool pressed;
    uint8_t id;

    DoorActionCommandData() = default;
    DoorActionCommandData(uint32_t pkt_data)
    {
        action = static_cast<DoorAction>((pkt_data >> DOOR_ACTION_SHIFT) & DOOR_ACTION_MASK);
        parity = ((pkt_data >> COMMAND_PARITY_SHIFT) & COMMAND_PARITY_MASK);
        pressed = ((pkt_data >> DOOR_ACTION_PRESSED_SHIFT) & DOOR_ACTION_PRESSED_MASK);
        id = ((pkt_data >> DOOR_ACTION_ID_SHIFT) & DOOR_ACTION_ID_MASK);
    };

    uint32_t to_data(void)
    {
        uint32_t pkt_data = 0;
        pkt_data |= static_cast<uint32_t>(action) << DOOR_ACTION_SHIFT;
        pkt_data |= parity << COMMAND_PARITY_SHIFT;
        pkt_data |= pressed << DOOR_ACTION_PRESSED_SHIFT;
        pkt_data |= (id & DOOR_ACTION_ID_MASK) << DOOR_ACTION_ID_SHIFT;
        return pkt_data;
    };

    void to_string(char *buf, size_t buflen)
    {
        const char *d = "invalid door action";
        switch (action)
        {
        case DoorAction::Close:
            d = "Close";
            break;
        case DoorAction::Open:
            d = "Open";
            break;
        case DoorAction::Toggle:
            d = "Toggle";
            break;
        case DoorAction::Stop:
            d = "Stop";
            break;
        }
        snprintf(buf, buflen, "DoorAction %s, Pressed %d, Id %02X", d, pressed, id);
    };
};

const uint8_t LOCK_DATA_MASK = 0b11;
const uint8_t LOCK_DATA_SHIFT = 8;
// const uint8_t LOCK_DATA_OFF    = 0b00;
// const uint8_t LOCK_DATA_ON     = 0b01;
// const uint8_t LOCK_DATA_TOGGLE = 0b10;
//  valid values for LockCommandData
enum class LockState : uint8_t
{
    Off = 0,
    On = 1,
    Toggle = 2
};

// data attached to PacketCommand::Lock
struct LockCommandData
{
    LockState lock;
    uint8_t parity;
    bool pressed;

    LockCommandData() = default;
    LockCommandData(uint32_t pkt_data)
    {
        lock = static_cast<LockState>((pkt_data >> LOCK_DATA_SHIFT) & LOCK_DATA_MASK);
        parity = ((pkt_data >> COMMAND_PARITY_SHIFT) & COMMAND_PARITY_MASK);
    };

    uint32_t to_data(void)
    {
        uint32_t pkt_data = 0;
        pkt_data |= static_cast<uint32_t>(lock) << LOCK_DATA_SHIFT;
        pkt_data |= parity << COMMAND_PARITY_SHIFT;
        return pkt_data;
    };

    void to_string(char *buf, size_t buflen)
    {
        const char *l = "invalid lock command";
        switch (lock)
        {
        case LockState::Off:
            l = "Off";
            break;
        case LockState::On:
            l = "On";
            break;
        case LockState::Toggle:
            l = "Toggle";
            break;
        }
        snprintf(buf, buflen, "LockState %s", l);
    };
};

const uint8_t LIGHT_DATA_MASK = 0b11;
const uint8_t LIGHT_DATA_SHIFT = 8;
// const uint8_t LIGHT_DATA_OFF    = 0b00;
// const uint8_t LIGHT_DATA_ON     = 0b01;
// const uint8_t LIGHT_DATA_TOGGLE = 0b10;
// const uint8_t LIGHT_DATA_OFF    = 0b11;
//  valid values for LightCommandData
enum class LightState : uint8_t
{
    Off = 0,
    On = 1,
    Toggle = 2,
    Toggle2 = 3
};

// data attached to PacketCommand::Light
struct LightCommandData
{
    LightState light;
    uint8_t parity;
    bool pressed;

    LightCommandData() = default;
    LightCommandData(uint32_t pkt_data)
    {
        light = static_cast<LightState>((pkt_data >> LIGHT_DATA_SHIFT) & LIGHT_DATA_MASK);
        parity = ((pkt_data >> COMMAND_PARITY_SHIFT) & COMMAND_PARITY_MASK);
    };

    uint32_t to_data(void)
    {
        uint32_t pkt_data = 0;
        pkt_data |= static_cast<uint32_t>(light) << LIGHT_DATA_SHIFT;
        pkt_data |= parity << COMMAND_PARITY_SHIFT;
        return pkt_data;
    };

    void to_string(char *buf, size_t buflen)
    {
        const char *l = "invalid light command";
        switch (light)
        {
        case LightState::Off:
            l = "Off";
            break;
        case LightState::On:
            l = "On";
            break;
        case LightState::Toggle:
            l = "Toggle";
            break;
        case LightState::Toggle2:
            l = "Toggle2";
            break;
        }
        snprintf(buf, buflen, "LightState %s", l);
    };
};

const uint8_t STATUS_DOOR_STATE_MASK = 0b1111;
const uint8_t STATUS_DOOR_STATE_SHIFT = 8;
// valid states for doors in StatusCommandData
enum class DoorState : uint8_t
{
    Unknown = 0,
    Open = 1,
    Closed = 2,
    Stopped = 3,
    Opening = 4,
    Closing = 5,
};

const uint8_t STATUS_UNKNOWN1_MASK = 0b1;
const uint8_t STATUS_UNKNOWN1_SHIFT = 21;
const uint8_t STATUS_OBSTRUCTION_MASK = 0b1;
const uint8_t STATUS_OBSTRUCTION_SHIFT = 22;
const uint8_t STATUS_LOCK_STATE_MASK = 0b1;
const uint8_t STATUS_LOCK_STATE_SHIFT = 24;
const uint8_t STATUS_LIGHT_STATE_MASK = 0b1;
const uint8_t STATUS_LIGHT_STATE_SHIFT = 25;
const uint8_t STATUS_UNKNOWN2_MASK = 0b1;
const uint8_t STATUS_UNKNOWN2_SHIFT = 30;
// data attached to PacketCommand::Status
struct StatusCommandData
{
    DoorState door;
    uint8_t parity;
    bool unknown1;
    bool obstruction;
    bool lock;
    bool light;
    bool unknown2;

    StatusCommandData() = default;

    StatusCommandData(uint32_t pkt_data)
    {
        door = static_cast<DoorState>((pkt_data >> STATUS_DOOR_STATE_SHIFT) & STATUS_DOOR_STATE_MASK);
        parity = ((pkt_data >> COMMAND_PARITY_SHIFT) & COMMAND_PARITY_MASK);
        unknown1 = ((pkt_data >> STATUS_UNKNOWN1_SHIFT) & STATUS_UNKNOWN1_MASK);
        obstruction = ((pkt_data >> STATUS_OBSTRUCTION_SHIFT) & STATUS_OBSTRUCTION_MASK);
        lock = ((pkt_data >> STATUS_LOCK_STATE_SHIFT) & STATUS_LOCK_STATE_MASK);
        light = ((pkt_data >> STATUS_LIGHT_STATE_SHIFT) & STATUS_LIGHT_STATE_MASK);
        unknown2 = ((pkt_data >> STATUS_UNKNOWN2_SHIFT) & STATUS_UNKNOWN2_MASK);
    };

    uint32_t to_data(void)
    {
        uint32_t pkt_data = 0;
        pkt_data |= static_cast<uint8_t>(door) << STATUS_DOOR_STATE_SHIFT;
        pkt_data |= parity << COMMAND_PARITY_SHIFT;
        pkt_data |= unknown1 << STATUS_UNKNOWN1_SHIFT;
        pkt_data |= obstruction << STATUS_OBSTRUCTION_SHIFT;
        pkt_data |= lock << STATUS_LOCK_STATE_SHIFT;
        pkt_data |= light << STATUS_LIGHT_STATE_SHIFT;
        pkt_data |= unknown2 << STATUS_UNKNOWN2_SHIFT;
        return pkt_data;
    };

    void to_string(char *buf, size_t buflen)
    {
        const char *d = "invalid door state";
        switch (door)
        {
        case DoorState::Unknown:
            d = "Unknown";
            break;
        case DoorState::Open:
            d = "Open";
            break;
        case DoorState::Closed:
            d = "Closed";
            break;
        case DoorState::Stopped:
            d = "Stopped";
            break;
        case DoorState::Opening:
            d = "Opening";
            break;
        case DoorState::Closing:
            d = "Closing";
            break;
        }

        snprintf(buf, buflen, "DoorState %s, Parity 0x%X, Obs %d, Lock %d, Light %d", d, parity, obstruction, lock, light);
    };
};

const uint8_t GET_OPENINGS_LO_BYTE_MASK = 0xFF;
const uint8_t GET_OPENINGS_LO_BYTE_SHIFT = 24;
const uint8_t GET_OPENINGS_HI_BYTE_MASK = 0xFF;
const uint8_t GET_OPENINGS_HI_BYTE_SHIFT = 16;
const uint8_t GET_OPENINGS_FLAG_MASK = 0x0F;
const uint8_t GET_OPENINGS_FLAG_SHIFT = 8;
struct OpeningsCommandData
{
    uint16_t count;
    uint8_t flags;
    uint8_t parity;

    OpeningsCommandData() = default;
    OpeningsCommandData(uint32_t pkt_data)
    {
        uint8_t lo = ((pkt_data >> GET_OPENINGS_LO_BYTE_SHIFT) & GET_OPENINGS_LO_BYTE_MASK);
        uint8_t hi = ((pkt_data >> GET_OPENINGS_HI_BYTE_SHIFT) & GET_OPENINGS_HI_BYTE_MASK);
        flags = ((pkt_data >> GET_OPENINGS_FLAG_SHIFT) & GET_OPENINGS_FLAG_MASK);
        parity = ((pkt_data >> COMMAND_PARITY_SHIFT) & COMMAND_PARITY_MASK);

        count = hi << 8 | lo;
    };

    uint32_t to_data(void)
    {
        uint32_t pkt_data = 0;
        uint8_t lo = count & 0xFF;
        uint8_t hi = count >> 8;
        pkt_data |= lo << GET_OPENINGS_LO_BYTE_SHIFT;
        pkt_data |= hi << GET_OPENINGS_HI_BYTE_SHIFT;
        pkt_data |= parity << COMMAND_PARITY_SHIFT;
        return pkt_data;
    };

    void to_string(char *buf, size_t buflen)
    {
        snprintf(buf, buflen, "Openings %02d, Flags 0x%02X, Parity 0x%X", count, flags, parity);
    };
};

enum class BatteryState : uint8_t
{
    Unknown = 0,
    Charging = 6,
    Full = 8,
};

const uint8_t BATTERY_DATA_MASK = 0xFF;
const uint8_t BATTERY_DATA_SHIFT = 16;
struct BatteryCommandData
{
    BatteryState state;
    uint8_t parity;

    BatteryCommandData() = default;
    BatteryCommandData(uint32_t pkt_data)
    {
        state = static_cast<BatteryState>((pkt_data >> BATTERY_DATA_SHIFT) & BATTERY_DATA_MASK);
        parity = ((pkt_data >> COMMAND_PARITY_SHIFT) & COMMAND_PARITY_MASK);
    };

    uint32_t to_data(void)
    {
        uint32_t pkt_data = 0;
        pkt_data |= static_cast<uint32_t>(state) << BATTERY_DATA_SHIFT;
        pkt_data |= parity << COMMAND_PARITY_SHIFT;
        return pkt_data;
    };

    void to_string(char *buf, size_t buflen)
    {
        const char *s = "Invalid command";
        switch (state)
        {
        case BatteryState::Unknown:
            s = "Unknown";
            break;
        case BatteryState::Charging:
            s = "Charging";
            break;
        case BatteryState::Full:
            s = "Full";
            break;
        }
        snprintf(buf, buflen, "BatteryState %s, Parity 0x%X", s, parity);
    };
};

// okay, so this is a weird one. for some messages, no bits except the parity bits are expected to
// be set. we want to preserve the parity bits, however, for round-trip testing (and possible future
// validation). the other bits _should_ always be zero.
struct NoData
{
    uint32_t no_bits_set;
    uint8_t parity;

    NoData() = default;
    NoData(uint32_t pkt_data)
    {
        no_bits_set = pkt_data & ~(COMMAND_PARITY_MASK << COMMAND_PARITY_SHIFT);
        no_bits_set = no_bits_set & ~0xFF; // skip cmd byte
        parity = ((pkt_data >> COMMAND_PARITY_SHIFT) & COMMAND_PARITY_MASK);
    };

    uint32_t to_data(void)
    {
        return no_bits_set | (parity << COMMAND_PARITY_SHIFT);
    };

    void to_string(char *buf, size_t buflen)
    {
        snprintf(buf, buflen, "Zero: 0x%08uX, Parity: 0x%X", no_bits_set, parity);
    };
};

struct PacketData
{
    PacketDataType type;
    union
    {
        NoData no_data;
        StatusCommandData status;
        LockCommandData lock;
        LightCommandData light;
        DoorActionCommandData door_action;
        OpeningsCommandData openings;
        BatteryCommandData battery;
        uint32_t cmd;
    } value;

    void to_string(char *buf, size_t buflen)
    {
        size_t subbuflen = 128;
        char subbuf[subbuflen];
        switch (type)
        {
        case PacketDataType::NoData:
            value.no_data.to_string(subbuf, subbuflen);
            snprintf(buf, buflen, "NoData: [%s]", subbuf);
            break;
        case PacketDataType::Status:
            value.status.to_string(subbuf, subbuflen);
            snprintf(buf, buflen, "Status: [%s]", subbuf);
            break;
        case PacketDataType::Lock:
            value.lock.to_string(subbuf, subbuflen);
            snprintf(buf, buflen, "Lock: [%s]", subbuf);
            break;
        case PacketDataType::Light:
            value.light.to_string(subbuf, subbuflen);
            snprintf(buf, buflen, "Light: [%s]", subbuf);
            break;
        case PacketDataType::DoorAction:
            value.door_action.to_string(subbuf, subbuflen);
            snprintf(buf, buflen, "DoorAction: [%s]", subbuf);
            break;
        case PacketDataType::Openings:
            value.openings.to_string(subbuf, subbuflen);
            snprintf(buf, buflen, "Openings: [%s]", subbuf);
            break;
        case PacketDataType::Battery:
            value.battery.to_string(subbuf, subbuflen);
            snprintf(buf, buflen, "Battery: [%s]", subbuf);
            break;
        case PacketDataType::Unknown:
            snprintf(buf, buflen, "Unknown: [%03uX]", value.cmd);
            break;
        }
    };
};

class PacketCommand
{
public:
    enum PacketCommandValue : uint16_t
    {
        Unknown = 0x000,
        GetStatus = 0x080,
        Status = 0x081,
        Obst1 = 0x084, // sent when an obstruction happens?
        Obst2 = 0x085, // sent when an obstruction happens?
        Battery = 0x09d,
        Pair3 = 0x0a0,
        Pair3Resp = 0x0a1,
        Learn2 = 0x181,
        Lock = 0x18c,
        DoorAction = 0x280,
        Light = 0x281,
        MotorOn = 0x284,
        Motion = 0x285,
        Learn1 = 0x391,
        Ping = 0x392,
        PingResp = 0x393,
        Pair2 = 0x400,
        Pair2Resp = 0x401,
        SetTtc = 0x402,    // ttc_in_seconds = (byte1<<8)+byte2
        CancelTtc = 0x408, // ?
        Ttc = 0x40a,       // Time to close
        GetOpenings = 0x48b,
        Openings = 0x48c, // openings = (byte1<<8)+byte2
    };

    PacketCommand() = default;
    constexpr PacketCommand(PacketCommandValue value) : m_value(value) {};

    constexpr operator PacketCommandValue() const { return m_value; };
    explicit operator bool() const = delete;

    static const char *to_string(PacketCommand cmd)
    {
        switch (cmd)
        {
        case PacketCommandValue::Unknown:
            return "UNKNOWN";
        case PacketCommandValue::GetStatus:
            return "GetStatus";
        case PacketCommandValue::Status:
            return "Status";
        case PacketCommandValue::Obst1:
            return "Obst1";
        case PacketCommandValue::Obst2:
            return "Obst2";
        case PacketCommandValue::Battery:
            return "Battery";
        case PacketCommandValue::Pair3:
            return "Pair3";
        case PacketCommandValue::Pair3Resp:
            return "Pair3Resp";
        case PacketCommandValue::Learn2:
            return "Learn2";
        case PacketCommandValue::Lock:
            return "Lock";
        case PacketCommandValue::DoorAction:
            return "DoorAction";
        case PacketCommandValue::Light:
            return "Light";
        case PacketCommandValue::MotorOn:
            return "MotorOn";
        case PacketCommandValue::Motion:
            return "Motion";
        case PacketCommandValue::Learn1:
            return "Learn1";
        case PacketCommandValue::Ping:
            return "Ping";
        case PacketCommandValue::PingResp:
            return "PingResp";
        case PacketCommandValue::Pair2:
            return "Pair2";
        case PacketCommandValue::Pair2Resp:
            return "Pair2Resp";
        case PacketCommandValue::SetTtc:
            return "SetTtc";
        case PacketCommandValue::CancelTtc:
            return "CancelTtc";
        case PacketCommandValue::Ttc:
            return "Ttc";
        case PacketCommandValue::GetOpenings:
            return "GetOpenings";
        case PacketCommandValue::Openings:
            return "Openings";
        }
        return "Invalid PacketCommandValue";
    }

    static PacketCommand from_word(uint16_t raw)
    {
        switch (raw)
        {
        case PacketCommand::GetStatus:
            return PacketCommand::GetStatus;
        case PacketCommandValue::Status:
            return PacketCommandValue::Status;
        case PacketCommandValue::Obst1:
            return PacketCommandValue::Obst1;
        case PacketCommandValue::Obst2:
            return PacketCommandValue::Obst2;
        case PacketCommandValue::Battery:
            return PacketCommandValue::Battery;
        case PacketCommandValue::Pair3:
            return PacketCommandValue::Pair3;
        case PacketCommandValue::Pair3Resp:
            return PacketCommandValue::Pair3Resp;
        case PacketCommandValue::Learn2:
            return PacketCommandValue::Learn2;
        case PacketCommandValue::Lock:
            return PacketCommandValue::Lock;
        case PacketCommandValue::DoorAction:
            return PacketCommandValue::DoorAction;
        case PacketCommandValue::Light:
            return PacketCommandValue::Light;
        case PacketCommandValue::MotorOn:
            return PacketCommandValue::MotorOn;
        case PacketCommandValue::Motion:
            return PacketCommandValue::Motion;
        case PacketCommandValue::Learn1:
            return PacketCommandValue::Learn1;
        case PacketCommandValue::Ping:
            return PacketCommandValue::Ping;
        case PacketCommandValue::PingResp:
            return PacketCommandValue::PingResp;
        case PacketCommandValue::Pair2:
            return PacketCommandValue::Pair2;
        case PacketCommandValue::Pair2Resp:
            return PacketCommandValue::Pair2Resp;
        case PacketCommandValue::SetTtc:
            return PacketCommandValue::SetTtc;
        case PacketCommandValue::CancelTtc:
            return PacketCommandValue::CancelTtc;
        case PacketCommandValue::Ttc:
            return PacketCommandValue::Ttc;
        case PacketCommandValue::GetOpenings:
            return PacketCommandValue::GetOpenings;
        case PacketCommandValue::Openings:
            return PacketCommandValue::Openings;
        default:
            return PacketCommandValue::Unknown;
        }
    }

private:
    PacketCommandValue m_value;
};

struct Packet
{
    const char *TAG = "ratgdo-packet";

    Packet() = default;
    Packet(PacketCommand cmd, PacketData data, uint32_t remote_id) : m_pkt_cmd(cmd), m_data(data), m_remote_id(remote_id), m_rolling(0) {};

    Packet(const uint8_t pktbuf[SECPLUS2_CODE_LEN])
    {
        uint32_t pkt_rolling = 0;   // three bytes
        uint64_t pkt_remote_id = 0; // three bytes
        uint32_t pkt_data = 0;

        int8_t ret = decode_wireline(pktbuf, &pkt_rolling, &pkt_remote_id, &pkt_data);
        if (ret < 0)
        {
            ESP_LOGE(TAG, "Failed to decode packet");
        }
        ESP_LOGI(TAG, "DECODED  %08lX %016" PRIX64 " %08lX", pkt_rolling, pkt_remote_id, pkt_data);

        uint16_t cmd = ((pkt_remote_id >> 24) & 0xF00) | (pkt_data & 0xFF);

        m_pkt_cmd = PacketCommand::from_word(cmd);
        m_rolling = pkt_rolling;
        m_remote_id = (pkt_remote_id & 0xFFffff);

        switch (m_pkt_cmd)
        {

        case PacketCommand::Unknown:
        {
            m_data.type = PacketDataType::Unknown;
            m_data.value.cmd = cmd;
            break;
        }

        case PacketCommand::GetStatus:
        {
            m_data.type = PacketDataType::NoData;
            m_data.value.no_data = NoData(pkt_data);
            break;
        }

        case PacketCommand::Status:
        {
            m_data.type = PacketDataType::Status;
            m_data.value.status = StatusCommandData(pkt_data);
            break;
        }

        case PacketCommand::Obst1:
        case PacketCommand::Obst2:
        case PacketCommand::Pair3:
        case PacketCommand::Pair3Resp:
        case PacketCommand::Learn2:
            // no data or unimplemented
            {
                m_data.type = PacketDataType::NoData;
                m_data.value.no_data = NoData(pkt_data);
                break;
            }

        case PacketCommand::Lock:
        {
            m_data.type = PacketDataType::Lock;
            m_data.value.lock = LockCommandData(pkt_data);
            break;
        }

        case PacketCommand::DoorAction:
        {
            m_data.type = PacketDataType::DoorAction;
            m_data.value.door_action = DoorActionCommandData(pkt_data);
            break;
        }

        case PacketCommand::Light:
        {
            m_data.type = PacketDataType::Light;
            m_data.value.light = LightCommandData(pkt_data);
            break;
        }

        case PacketCommand::MotorOn:
        case PacketCommand::Motion:
        case PacketCommand::Learn1:
        case PacketCommand::Ping:
        case PacketCommand::PingResp:
        case PacketCommand::Pair2:
        case PacketCommand::Pair2Resp:
        case PacketCommand::SetTtc:
        case PacketCommand::CancelTtc:
        case PacketCommand::Ttc:
        case PacketCommand::GetOpenings:
            // no data or unimplemented
            {
                m_data.type = PacketDataType::NoData;
                m_data.value.no_data = NoData(pkt_data);
                break;
            }

        case PacketCommand::Openings:
        {
            m_data.type = PacketDataType::Openings;
            m_data.value.openings = OpeningsCommandData(pkt_data);
            break;
        }

        case PacketCommand::Battery:
        {
            m_data.type = PacketDataType::Battery;
            m_data.value.battery = BatteryCommandData(pkt_data);
            break;
        }
        }
    }

    int8_t encode(uint32_t rolling, uint8_t *out_pktbuf)
    {
        m_rolling = rolling;

        uint32_t pkt_data = 0;
        auto cmd = static_cast<uint64_t>(m_pkt_cmd);
        uint64_t fixed = ((cmd & ~0xff) << 24) | static_cast<uint64_t>(m_remote_id & 0xFFffff);

        switch (m_pkt_cmd)
        {
        case PacketCommand::Unknown:
            // nothing to do?
            break;

        case PacketCommand::GetStatus:
            // no data
            break;

        case PacketCommand::Status:
        {
            pkt_data = m_data.value.status.to_data();
            break;
        }

        case PacketCommand::Obst1:
        case PacketCommand::Obst2:
        case PacketCommand::Pair3:
        case PacketCommand::Pair3Resp:
        case PacketCommand::Learn2:
            // no data or unimplemented
            break;

        case PacketCommand::Lock:
        {
            pkt_data = m_data.value.lock.to_data();
            break;
        }
        break;

        case PacketCommand::DoorAction:
        {
            pkt_data = m_data.value.door_action.to_data();
            break;
        }

        case PacketCommand::Light:
        {
            pkt_data = m_data.value.light.to_data();
            break;
        }

        case PacketCommand::MotorOn:
        case PacketCommand::Motion:
        case PacketCommand::Learn1:
        case PacketCommand::Ping:
        case PacketCommand::PingResp:
        case PacketCommand::Pair2:
        case PacketCommand::Pair2Resp:
        case PacketCommand::SetTtc:
        case PacketCommand::CancelTtc:
        case PacketCommand::Ttc:
        case PacketCommand::GetOpenings:
            // no data or unimplemented
            break;

        case PacketCommand::Openings:
        {
            pkt_data = m_data.value.openings.to_data();
            break;
        }

        case PacketCommand::Battery:
        {
            pkt_data = m_data.value.battery.to_data();
            break;
        }
        }

        pkt_data |= (m_pkt_cmd & 0xFF);

        ESP_LOGI(TAG, "ENCODING %08lX %016" PRIX64 " %08lX", m_rolling, fixed, pkt_data);
        return encode_wireline(m_rolling, fixed, pkt_data, out_pktbuf);
    }

    /*
    PacketCommand cmd(void) { return m_pkt_cmd; }
    PacketData data(void) { return m_data; }
    uint32_t rolling(void) { return m_rolling; }
    uint64_t remote_id(void) { return m_remote_id; }
    */

    void print(void)
    {
        size_t buflen = 128;
        char buf[buflen];
        m_data.to_string(buf, buflen);

        ESP_LOGI(TAG, "PACKET(0x%lX @ 0x%lX) %s - %s", m_remote_id, m_rolling, PacketCommand::to_string(m_pkt_cmd), buf);
    };

    PacketCommand m_pkt_cmd;
    PacketData m_data;
    uint32_t m_remote_id; // 3 bytes
    uint32_t m_rolling;
};