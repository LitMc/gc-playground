#pragma once
#include "joybus_protocol.hpp"
#include "transforms/transform_api.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace ConvertGcInput::Builtins {
struct JoystickLutContext {
    std::array<uint8_t, 256> lut_x{}; // 0..255 -> 0..255
    std::array<uint8_t, 256> lut_y{}; // 0..255 -> 0..255
};

// コントロールスティック入力にLUTを適用
inline void apply_lut_to_joystick(JoystickLutContext &context, Joybus::Command command,
                                  std::span<uint8_t> reply) {
    switch (command) {
    case Joybus::Command::Invalid:
        return;
    case Joybus::Command::Id:
        return;
    case Joybus::Command::Reset:
        return;
    // Status, Origin, Recalibrateはいずれも3, 4バイト目がスティックのXY軸
    case Joybus::Command::Status:
    case Joybus::Command::Origin:
    case Joybus::Command::Recalibrate:
        break;
    default:
        return;
    }

    if (reply.size() < Joybus::kStatusResponseSize) {
        return;
    }

    reply[2] = context.lut_x[reply[2]];
    reply[3] = context.lut_y[reply[3]];
}

inline void apply_fix_origin_and_recalibrate_to_center(void *, Joybus::Command command,
                                                       std::span<uint8_t> reply) {
    if (command != Joybus::Command::Origin && command != Joybus::Command::Recalibrate) {
        return;
    }

    if (reply.size() < 4)
        return;

    reply[2] = 128;
    reply[3] = 128;
}

} // namespace ConvertGcInput::Builtins
