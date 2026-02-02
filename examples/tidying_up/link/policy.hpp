#pragma once
#include "joybus/protocol/protocol.hpp"

// このプロジェクト固有の方針を定義する名前空間
namespace ConvertGcInput::policy {
// Pico -> Padでのポーリングに使用するモードはMode3に固定
// Mode3なら未使用のAとBのアナログ入力が犠牲になるだけなので都合がいい
constexpr Joybus::PollMode kPadPollModeForQuery = Joybus::PollMode::Mode3;
} // namespace ConvertGcInput::policy
