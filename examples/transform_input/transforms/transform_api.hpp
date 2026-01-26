#pragma once
#include "joybus_protocol.hpp"
#include <span>

namespace ConvertGcInput {

using TransformFunction = void (*)(void *user, Joybus::Command command, std::span<uint8_t> reply);

static constexpr uint8_t kCommandAll = 0xFF;

// パイプライン1段分の処理
struct Stage {
    // コマンド応答に対して適用する変換関数
    TransformFunction function_to_apply{nullptr};
    // 変換処理に渡すコンテキスト
    void *user{nullptr};
    // このステージの変換を行うコマンドのビットマスク
    uint8_t command_selector_mask{kCommandAll};
};

template <class Context, void (*TransformFunction)(Context &, Joybus::Command, std::span<uint8_t>)>
inline void thunk(void *user, Joybus::Command command, std::span<uint8_t> reply) {
    auto *context = static_cast<Context *>(user);
    TransformFunction(*context, command, reply);
}

template <class Context, void (*TransformFunction)(Context &, Joybus::Command, std::span<uint8_t>)>
inline Stage make_stage(Context &context, uint8_t command_selector_mask = kCommandAll) {
    return Stage{
        .function_to_apply{&thunk<Context, TransformFunction>},
        .user{&context},
        .command_selector_mask{command_selector_mask},
    };
}

// ステージで変換対象とするコマンドを表すビットを得る
static constexpr uint8_t command_bit(Joybus::Command cmd) {
    switch (cmd) {
    case Joybus::Command::Status:
        return 1 << 0;
    case Joybus::Command::Id:
        return 1 << 1;
    case Joybus::Command::Origin:
        return 1 << 2;
    case Joybus::Command::Recalibrate:
        return 1 << 3;
    case Joybus::Command::Reset:
        return 1 << 4;
    default:
        return 0;
    }
}

class StatusView {
  public:
    uint8_t &joy_x() { return response_[2]; }
    uint8_t &joy_y() { return response_[3]; }
    uint8_t &c_x() { return response_[4]; }
    uint8_t &c_y() { return response_[5]; }
    uint8_t &l_analog() { return response_[6]; }
    uint8_t &r_analog() { return response_[7]; }

    StatusView(std::span<uint8_t, Joybus::kStatusResponseSize> response) : response_{response} {};

  private:
    std::span<uint8_t, Joybus::kStatusResponseSize> response_;
};

} // namespace ConvertGcInput
