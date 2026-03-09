#pragma once
#include "core/identity.hpp"

namespace ConvertGcInput::Joybus::common {

inline constexpr core::PollMode to_core_poll_mode(Joybus::PollMode joybus_poll_mode) {
    return static_cast<core::PollMode>(static_cast<uint8_t>(joybus_poll_mode));
}

inline constexpr core::RumbleMode to_core_rumble_mode(Joybus::RumbleMode joybus_rumble_mode) {
    return static_cast<core::RumbleMode>(
        Joybus::clamp_rumble_mode(static_cast<uint8_t>(joybus_rumble_mode)));
}

inline constexpr Joybus::PollMode to_joybus_poll_mode(core::PollMode core_poll_mode) {
    return static_cast<Joybus::PollMode>(
        Joybus::clamp_poll_mode(static_cast<uint8_t>(core_poll_mode)));
}

inline constexpr Joybus::RumbleMode to_joybus_rumble_mode(core::RumbleMode core_rumble_mode) {
    return static_cast<Joybus::RumbleMode>(
        Joybus::clamp_rumble_mode(static_cast<uint8_t>(core_rumble_mode)));
}

// Little-endianで2バイトを読み取る
// Longo氏の資料ではStatus word部分のbitがLittle-endianで書かれているため
inline constexpr uint16_t read_u16_le(std::span<const uint8_t, 2> b) {
    return static_cast<uint16_t>(static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8));
}

// Little-endianで2バイトを書き込む
// Longo氏の資料ではStatus word部分のbitがLittle-endianで書かれているため
inline constexpr void write_u16_le(uint16_t v, std::span<uint8_t, 2> b) {
    b[0] = static_cast<uint8_t>(v & 0xFFu);        // low byte first
    b[1] = static_cast<uint8_t>((v >> 8) & 0xFFu); // high byte second
}
// Big-endianで2バイトを読み取る
// Id応答のdevice_capabilitiesはBig-endianで送受信される
inline constexpr uint16_t read_u16_be(std::span<const uint8_t, 2> b) {
    return static_cast<uint16_t>((static_cast<uint16_t>(b[0]) << 8) | static_cast<uint16_t>(b[1]));
}

// Big-endianで2バイトを書き込む
// Id応答のdevice_capabilitiesはBig-endianで送受信される
inline constexpr void write_u16_be(uint16_t v, std::span<uint8_t, 2> b) {
    b[0] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    b[1] = static_cast<uint8_t>(v & 0xFFu);
}
} // namespace ConvertGcInput::Joybus::common
