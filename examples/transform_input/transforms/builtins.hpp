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

inline void apply_lut_to_joystick(JoystickLutContext &context, Joybus::Command command,
                                  std::span<uint8_t> reply) {
    if (command != Joybus::Command::Status)
        return;
    if (reply.size() < Joybus::kStatusResponseSize)
        return;

    reply[2] = context.lut_x[reply[2]];
    reply[3] = context.lut_y[reply[3]];
}

} // namespace ConvertGcInput::Builtins
