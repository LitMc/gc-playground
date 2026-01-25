#include "console_client.hpp"

namespace ConvertGcInput {

std::size_t ConsoleClient::callback(void *user, const uint8_t *rx, std::size_t rx_len, uint8_t *tx,
                                    std::size_t tx_max) {
    if (rx_len < 1) {
        return 0;
    }

    auto *self = static_cast<ConsoleClient *>(user);
    self->shared_console_.on_request_isr(std::span<const uint8_t>(rx, rx_len));

    const bool pad_ready = self->link_.is_pad_ready();
    const auto snapshot = self->shared_pad_.load();

    if (!pad_ready) {
        return 0;
    }

    const auto cmd = static_cast<Joybus::Command>(rx[0]);

    auto copy = [&](auto const &arr) -> std::size_t {
        if (tx_max < arr.size()) {
            return 0;
        }
        std::copy_n(arr.data(), arr.size(), tx);
        return arr.size();
    };

    switch (cmd) {
    case Joybus::Command::Status:
        if (!snapshot.has_status) {
            return 0;
        }
        return copy(snapshot.status);
    case Joybus::Command::Id:
        if (!snapshot.has_id) {
            return 0;
        }
        return copy(snapshot.id);
    case Joybus::Command::Origin:
        if (!snapshot.has_origin) {
            return 0;
        }
        return copy(snapshot.origin);
    case Joybus::Command::Recalibrate:
        if (!snapshot.has_recalibrate) {
            return 0;
        }
        return copy(snapshot.recalibrate);
    case Joybus::Command::Reset:
        // パッドにResetを伝える
        self->link_.publish_pad_reset_request_from_isr();

        // ResetはID相当で返す。reset応答が取れていればそれ、なければID応答を返す
        if (snapshot.has_reset) {
            return copy(snapshot.reset);
        }
        if (snapshot.has_id) {
            return copy(snapshot.id);
        }
        return 0;
    default:
        return 0;
    }
    return 0;
}
} // namespace ConvertGcInput
