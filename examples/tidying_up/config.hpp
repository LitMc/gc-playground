#pragma once
#include "joybus/protocol/protocol.hpp"

namespace ConvertGcInput::config {
// Pico -> Padでのポーリングに使用するモードはMode3に固定
// Mode3だと、未使用のAとBのアナログ入力が犠牲になり都合がいい
constexpr Joybus::PollMode kPadQueryPollMode = Joybus::PollMode::Mode3;
} // namespace ConvertGcInput::config
