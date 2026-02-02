#pragma once
#include "core/state.hpp"

namespace ConvertGcInput::core::transform::builtins {
// アナログ入力をすべて正確なニュートラルポジションに固定
inline void fix_origin_to_neutral(void *, core::PadState &state) {
    auto &analog = state.input.analog;
    analog.stick_x = core::AnalogInput::kAxisCenter;
    analog.stick_y = core::AnalogInput::kAxisCenter;
    analog.c_stick_x = core::AnalogInput::kAxisCenter;
    analog.c_stick_y = core::AnalogInput::kAxisCenter;
    analog.l_analog = core::AnalogInput::kTriggerReleased;
    analog.r_analog = core::AnalogInput::kTriggerReleased;
}

} // namespace ConvertGcInput::core::transform::builtins
