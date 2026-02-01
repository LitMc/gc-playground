#pragma once
#include "codec/joybus/report.hpp"
#include "core/identity.hpp"
#include "core/report.hpp"
#include "joybus_protocol.hpp"
#include "joybus_reply.hpp"
#include <array>
#include <cstdint>
#include <span>

// コントローラのサポートする機能や実行時レポートをJoybusレスポンス形式に変換、復元するための処理群
namespace ConvertGcInput::Joybus::identity {
// --- ID bytes1..2 (16bit) のビット定義（Joybus固有） ---
static constexpr uint16_t kIsWireless = (1u << 15);
static constexpr uint16_t kSupportsWirelessReceive = (1u << 14);
static constexpr uint16_t kRumbleNotAvailable = (1u << 13);
static constexpr uint16_t kIsGamecube = (1u << 11);     // 1=GC, 0=N64
static constexpr uint16_t kWirelessTypeRf = (1u << 10); // 1=RF, 0=IF
static constexpr uint16_t kWirelessStateFixed = (1u << 9);
static constexpr uint16_t kIsStandardController = (1u << 8);

// --- ID byte3 (8bit) のビット定義（Joybus固有） ---
static constexpr uint8_t kPollMask = 0x07u;                                        // bits[2:0]
static constexpr uint8_t kRumbleMask = 0x18u;                                      // bits[4:3]
inline constexpr uint8_t clamp_poll_mode(uint8_t v) { return (v <= 4) ? v : 3; }   // fallback Mode3
inline constexpr uint8_t clamp_rumble_mode(uint8_t v) { return (v <= 2) ? v : 0; } // fallback Off

inline constexpr std::array<uint8_t, kIdResponseSize>
encode_identity_bytes(const core::PadIdentity &id) {
    uint16_t device_capabilities = 0;

    const auto &capabilities = id.capabilities;

    if (capabilities.is_wireless) {
        device_capabilities |= kIsWireless;
    }
    if (capabilities.supports_wireless_receive) {
        device_capabilities |= kSupportsWirelessReceive;
    }
    if (!capabilities.rumble_available) {
        device_capabilities |= kRumbleNotAvailable;
    }
    if (capabilities.is_gamecube) {
        device_capabilities |= kIsGamecube;
    }
    if (capabilities.wireless_is_rf) {
        device_capabilities |= kWirelessTypeRf;
    }
    if (capabilities.wireless_state_fixed) {
        device_capabilities |= kWirelessStateFixed;
    }
    if (capabilities.is_standard_controller) {
        device_capabilities |= kIsStandardController;
    }

    uint8_t runtime_flags = 0;
    const auto &runtime = id.runtime;
    if (runtime.report.error_last) {
        runtime_flags |= report::to_mask(report::IdByte3Bits::ErrorLast);
    }
    if (runtime.report.error_latched) {
        runtime_flags |= report::to_mask(report::IdByte3Bits::ErrorLatched);
    }
    if (!runtime.report.origin_sent) {
        runtime_flags |= report::to_mask(report::IdByte3Bits::OriginNotSent);
    }
    const uint8_t poll_mode = clamp_poll_mode(static_cast<uint8_t>(runtime.poll_mode));
    const uint8_t rumble_mode = clamp_rumble_mode(static_cast<uint8_t>(runtime.rumble_mode));
    runtime_flags |= (rumble_mode << 3) & kRumbleMask;
    runtime_flags |= poll_mode & kPollMask;

    return std::array<uint8_t, kIdResponseSize>{
        static_cast<uint8_t>((device_capabilities >> 8) & 0xFFu),
        static_cast<uint8_t>(device_capabilities & 0xFFu), runtime_flags};
}

inline JoybusReply encode_identity(const core::PadIdentity &id) {
    return JoybusReply{Command::Id, encode_identity_bytes(id)};
}

inline JoybusReply encode_reset_as_id(const core::PadIdentity &id) {
    // ResetはIDと同じ形式
    return JoybusReply{Command::Reset, encode_identity_bytes(id)};
}

inline core::PadIdentity decode_identity(std::span<const uint8_t, kIdResponseSize> rx) {
    core::PadIdentity out{};

    const uint16_t device_capabilities =
        (static_cast<uint16_t>(rx[0]) << 8) | static_cast<uint16_t>(rx[1]);
    auto &capabilities = out.capabilities;
    capabilities.is_wireless = (device_capabilities & kIsWireless) != 0;
    capabilities.supports_wireless_receive = (device_capabilities & kSupportsWirelessReceive) != 0;
    capabilities.rumble_available = (device_capabilities & kRumbleNotAvailable) == 0;
    capabilities.is_gamecube = (device_capabilities & kIsGamecube) != 0;
    capabilities.wireless_is_rf = (device_capabilities & kWirelessTypeRf) != 0;
    capabilities.wireless_state_fixed = (device_capabilities & kWirelessStateFixed) != 0;
    capabilities.is_standard_controller = (device_capabilities & kIsStandardController) != 0;

    auto &runtime = out.runtime;
    const uint8_t runtime_flags = rx[2];
    report::update_report_from_id_byte3(runtime.report, runtime_flags);
    runtime.poll_mode = static_cast<core::PollMode>(clamp_poll_mode(runtime_flags & kPollMask));
    runtime.rumble_mode =
        static_cast<core::RumbleMode>(clamp_rumble_mode((runtime_flags & kRumbleMask) >> 3));
    return out;
}

inline core::PadIdentity decode_reset_as_identity(std::span<const uint8_t, kResetResponseSize> rx) {
    // ResetはIDと同じ形式
    return decode_identity(rx);
}
} // namespace ConvertGcInput::Joybus::identity
