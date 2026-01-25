#pragma once
#include "double_buffer.hpp"
#include "joybus_protocol.hpp"
#include <span>

namespace ConvertGcInput {

struct ConsoleState {
    Joybus::PollMode poll_mode = Joybus::PollMode::Default;
    Joybus::RumbleMode rumble_mode = Joybus::RumbleMode::Off;
    uint16_t reset_count = 0;

    // テスト用
    std::array<uint8_t, Joybus::kOriginResponseSize> last_send_origin{};
    uint32_t origin_send_count = 0;
    std::array<uint8_t, Joybus::kRecalibrateResponseSize> last_send_recalibrate{};
    uint32_t recalibrate_send_count = 0;
};

class SharedConsole {
    // テスト用
  public:
    void __isr publish_sent_origin(const std::array<uint8_t, Joybus::kOriginResponseSize> &data) {
        shadow_.last_send_origin = data;
        shadow_.origin_send_count++;
        db_.publish(shadow_);
    }

    void __isr
    publish_sent_recalibrate(const std::array<uint8_t, Joybus::kRecalibrateResponseSize> &data) {
        shadow_.last_send_recalibrate = data;
        shadow_.recalibrate_send_count++;
        db_.publish(shadow_);
    }

  public:
    ConsoleState load() const { return db_.load(); }

    void on_request_isr(std::span<const uint8_t> rx) {
        if (rx.empty()) {
            return;
        }

        bool updated = false;
        Joybus::Command command = static_cast<Joybus::Command>(rx[0]);
        switch (command) {
        case Joybus::Command::Status:
            if (rx.size() >= 3) {
                const auto poll = sanitize_poll_mode(rx[1]);
                const auto rumble = sanitize_rumble_mode(rx[2]);
                if (poll != shadow_.poll_mode || rumble != shadow_.rumble_mode) {
                    shadow_.poll_mode = poll;
                    shadow_.rumble_mode = rumble;
                    updated = true;
                }
            }
            break;
        case Joybus::Command::Reset:
            shadow_.reset_count++;
            updated = true;
            break;
        case Joybus::Command::Id:
        case Joybus::Command::Origin:
        case Joybus::Command::Recalibrate:
        default:
            break;
        }

        if (updated) {
            db_.publish(shadow_);
        }
    }

  private:
    static Joybus::PollMode sanitize_poll_mode(uint8_t v) {
        return (v <= static_cast<uint8_t>(Joybus::PollMode::Mode4))
                   ? static_cast<Joybus::PollMode>(v)
                   : Joybus::PollMode::Default;
    }

    static Joybus::RumbleMode sanitize_rumble_mode(uint8_t v) {
        return (v <= static_cast<uint8_t>(Joybus::RumbleMode::Brake))
                   ? static_cast<Joybus::RumbleMode>(v)
                   : Joybus::RumbleMode::Off;
    }

    ConsoleState shadow_{};
    DoubleBuffer<ConsoleState> db_{};
};

} // namespace ConvertGcInput
