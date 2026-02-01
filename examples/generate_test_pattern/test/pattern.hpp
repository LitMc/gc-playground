#pragma once

#include "core/state.hpp"
#include <concepts>
#include <cstdint>

namespace ConvertGcInput::test {
// テストパターンのコンセプト
template <class P>
concept TestPattern = requires(P p, core::PadState &state, uint32_t steps) {
    // リセットできること
    { p.reset() } -> std::same_as<void>;
    // 現在のパッド状態とステップ数から次の状態へ進められること
    { p.sample_and_advance(state, steps) } -> std::same_as<bool>;
};
} // namespace ConvertGcInput::test
