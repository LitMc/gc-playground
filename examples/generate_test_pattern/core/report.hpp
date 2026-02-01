#pragma once
#include <cstdint>
#include <type_traits>

namespace ConvertGcInput::core {

// 入力以外のパッドからコンソールへ伝える状態。エラーの有無やOrigin送信済みなど
struct PadReport {
    bool origin_sent{false};           // 本体へOriginコマンド送信済み
    bool error_latched{false};         // これまでの通信のどこかでエラーがあった
    bool error_last{false};            // 直近の送信でエラーがあった
    bool use_controller_origin{false}; // コントローラのOriginを使う（用途不明）
};

static_assert(std::is_trivially_copyable_v<PadReport>);
static_assert(std::is_standard_layout_v<PadReport>);
} // namespace ConvertGcInput::core
