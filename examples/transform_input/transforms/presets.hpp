#pragma once
#include "transforms/builtins.hpp"
#include "transforms/transform_api.hpp"
#include "transforms/transform_pipeline.hpp"

namespace ConvertGcInput::Presets {
// 恒等変換ステージを追加
void install_identity(TransformPipeline &pipeline, Builtins::JoystickLutContext &context) {
    for (std::size_t i = 0; i < 256; ++i) {
        context.lut_x[i] = static_cast<uint8_t>(i);
        context.lut_y[i] = static_cast<uint8_t>(i);
    }

    pipeline.add_stage(make_stage<Builtins::JoystickLutContext, &Builtins::apply_lut_to_joystick>(
        context, command_bit(Joybus::Command::Status)));
}

// スティック入力を半減させるステージを追加
void install_half_joystick(TransformPipeline &pipeline, Builtins::JoystickLutContext &context) {
    for (std::size_t i = 0; i < 256; ++i) {
        int16_t relative = static_cast<int16_t>(i) - 128;
        if (relative >= 0) {
            relative = (relative + 1) / 2;
        } else {
            relative = (relative - 1) / 2;
        }
        context.lut_x[i] = static_cast<uint8_t>(relative + 128);
        context.lut_y[i] = static_cast<uint8_t>(relative + 128);
    }

    pipeline.add_stage(make_stage<Builtins::JoystickLutContext, &Builtins::apply_lut_to_joystick>(
        context, command_bit(Joybus::Command::Status)));
}
} // namespace ConvertGcInput::Presets
