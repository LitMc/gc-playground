#pragma once
#include "joybus_protocol.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace ConvertGcInput {

class JoybusReply {
  public:
    JoybusReply() = default;
    template <std::size_t N>
    JoybusReply(Joybus::Command cmd, const std::array<uint8_t, N> &src)
        : command_{cmd}, length_{N} {
        static_assert(N <= Joybus::kMaxResponseSize);
        std::copy_n(src.data(), N, bytes_.begin());
    }

    JoybusReply(Joybus::Command cmd, std::span<const uint8_t> src)
        : command_{cmd}, length_{static_cast<uint8_t>(
                             std::min<std::size_t>(src.size(), Joybus::kMaxResponseSize))} {
        std::copy_n(src.data(), length_, bytes_.begin());
    }

    // 応答のコマンド種別
    Joybus::Command command() const { return command_; }

    // 応答内容: modifiable
    std::span<uint8_t> view() { return {bytes_.data(), length_}; }

    // 応答内容: read-only
    std::span<const uint8_t> view() const { return {bytes_.data(), length_}; }

  private:
    Joybus::Command command_{Joybus::Command::Invalid};
    uint8_t length_{0};
    std::array<uint8_t, Joybus::kMaxResponseSize> bytes_{};
};

} // namespace ConvertGcInput
