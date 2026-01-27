#pragma once
#include "double_buffer.hpp"
#include "joybus_protocol.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace ConvertGcInput {
struct PadSnapshot {
    uint32_t publish_count = 0;
    Joybus::Command last_rx_command = Joybus::Command::Id;
    std::array<uint8_t, Joybus::kIdResponseSize> id{0x09, 0x00, 0x00};
    std::array<uint8_t, Joybus::kOriginResponseSize> origin{};
    std::array<uint8_t, Joybus::kStatusResponseSize> status{};
    std::array<uint8_t, Joybus::kRecalibrateResponseSize> recalibrate{};
    std::array<uint8_t, Joybus::kResetResponseSize> reset{0x09, 0x00, 0x00};
    bool has_reset = false;
};

class SharedPad {
  public:
    // パッドの最新スナップショットを得る
    PadSnapshot load() const { return db_.load(); }

    // パッドからの応答を記録
    void on_response_isr(Joybus::Command command, std::span<const uint8_t> rx) {
        bool updated = false;
        switch (command) {
        case Joybus::Command::Id:
            updated = write_fixed(shadow_.id, rx);
            break;
        case Joybus::Command::Origin:
            updated = write_fixed(shadow_.origin, rx);
            break;
        case Joybus::Command::Status:
            updated = write_fixed(shadow_.status, rx);
            break;
        case Joybus::Command::Recalibrate:
            updated = write_fixed(shadow_.recalibrate, rx);
            break;
        case Joybus::Command::Reset:
            updated = write_fixed(shadow_.reset, rx);
            shadow_.has_reset = true;
            break;
        default:
            break;
        }

        if (updated) {
            shadow_.last_rx_command = command;
            shadow_.publish_count++;
            db_.publish(shadow_);
        }
    }

  private:
    PadSnapshot shadow_{};           // IRQでの書き込み専用
    DoubleBuffer<PadSnapshot> db_{}; // 外部から読み取る用

    template <std::size_t N>
    static bool write_fixed(std::array<uint8_t, N> &dst, std::span<const uint8_t> rx) {
        if (rx.size() < N) {
            return false;
        }
        std::copy_n(rx.data(), N, dst.begin());
        return true;
    }
};

} // namespace ConvertGcInput
