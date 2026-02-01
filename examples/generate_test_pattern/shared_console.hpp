#pragma once
#include "double_buffer.hpp"
#include "joybus_protocol.hpp"
#include <span>

namespace ConvertGcInput {

struct ConsoleState {
    Joybus::PollMode poll_mode = Joybus::PollMode::Default;
    Joybus::RumbleMode rumble_mode = Joybus::RumbleMode::Off;
    uint16_t reset_count = 0;
};

class SharedConsole {
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
                const auto poll = Joybus::sanitize_poll_mode(rx[1]);
                const auto rumble = Joybus::sanitize_rumble_mode(rx[2]);
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
    ConsoleState shadow_{};
    DoubleBuffer<ConsoleState> db_{};
};

} // namespace ConvertGcInput
