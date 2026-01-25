#pragma once
#include "double_buffer.hpp"
#include "joybus_protocol.hpp"
#include <array>
#include <atomic>

namespace ConvertGcInput {
namespace Test {
using Status = std::array<uint8_t, Joybus::kStatusResponseSize>;
using Origin = std::array<uint8_t, Joybus::kOriginResponseSize>;
using Recalibrate = std::array<uint8_t, Joybus::kRecalibrateResponseSize>;

class TestState {
  public:
    void begin_from_main(const Origin &origin, const Recalibrate &recalibrate,
                         const Status &initial_status) {
        test_enabled_.store(false, std::memory_order_release);
        fixed_origin_ = origin;
        fixed_recalibrate_ = recalibrate;
        status_db_.publish(initial_status);
        test_enabled_.store(true, std::memory_order_release);
    }

    void end_from_main() { test_enabled_.store(false, std::memory_order_release); }

    void enable_from_main() { test_enabled_.store(true, std::memory_order_release); }

    bool enabled() const { return test_enabled_.load(std::memory_order_acquire); }

    bool origin_and_recalibrate_fixed() const {
        return origin_and_recalibrate_fixed_.load(std::memory_order_acquire);
    }

    void fix_origin_and_recalibrate_from_main(const Origin &origin,
                                              const Recalibrate &recalibrate) {
        fixed_origin_ = origin;
        fixed_recalibrate_ = recalibrate;
        origin_and_recalibrate_fixed_.store(true, std::memory_order_release);
    }

    void publish_status_from_main(const Status &status) { status_db_.publish(status); }

    Status __isr load_status_from_isr() const { return status_db_.load(); }

    const Origin &__isr load_fixed_origin_from_isr() const { return fixed_origin_; }
    const Status __isr make_stick_status_neutral_from_isr(Status status) const {
        status[2] = 0x80; // Joystick X
        status[3] = 0x80; // Joystick Y
        return status;
    }
    const Recalibrate &__isr load_fixed_recalibrate_from_isr() const { return fixed_recalibrate_; }

  private:
    std::atomic<bool> test_enabled_{false};
    std::atomic<bool> origin_and_recalibrate_fixed_{false};
    DoubleBuffer<Status> status_db_{};
    Origin fixed_origin_{};
    Recalibrate fixed_recalibrate_{};
};

} // namespace Test
} // namespace ConvertGcInput
