#pragma once
#include "double_buffer.hpp"
#include "joybus_protocol.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace ConvertGcInput {
struct PadSnapshot {
    std::array<uint8_t, Joybus::kIdResponseSize> id{0x09, 0x00, 0x00};
    bool has_id = false;
    std::array<uint8_t, Joybus::kOriginResponseSize> origin{};
    bool has_origin = false;
    std::array<uint8_t, Joybus::kStatusResponseSize> status{};
    bool has_status = false;
    std::array<uint8_t, Joybus::kRecalibrateResponseSize> recalibrate{};
    bool has_recalibrate = false;
    std::array<uint8_t, Joybus::kResetResponseSize> reset{0x09, 0x00, 0x00};
    bool has_reset = false;
};

class SharedPad {
  public:
    PadSnapshot load() const { return db_.load(); }

    void on_response_isr(Joybus::Command cmd, std::span<const uint8_t> rx) {
        bool updated = false;
        switch (cmd) {
        case Joybus::Command::Id:
            updated = write_fixed(shadow_.id, shadow_.has_id, rx);
            break;
        case Joybus::Command::Origin:
            updated = write_fixed(shadow_.origin, shadow_.has_origin, rx);
            break;
        case Joybus::Command::Status:
            updated = write_fixed(shadow_.status, shadow_.has_status, rx);
            break;
        case Joybus::Command::Recalibrate:
            updated = write_fixed(shadow_.recalibrate, shadow_.has_recalibrate, rx);
            break;
        case Joybus::Command::Reset:
            updated = write_fixed(shadow_.reset, shadow_.has_reset, rx);
            break;
        default:
            break;
        }

        if (updated) {
            db_.publish(shadow_);
        }
    }

  private:
    PadSnapshot shadow_{};           // IRQでの書き込み専用
    DoubleBuffer<PadSnapshot> db_{}; // 外部から読み取る用

    template <std::size_t N>
    static bool write_fixed(std::array<uint8_t, N> &dst, bool &has, std::span<const uint8_t> rx) {
        if (rx.size() < N) {
            return false;
        }
        std::copy_n(rx.data(), N, dst.begin());
        has = true;
        return true;
    }
};

} // namespace ConvertGcInput
