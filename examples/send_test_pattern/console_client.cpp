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
        // テスト用
        if (self->link_.test().enabled()) {
            const auto test_status = self->link_.test().load_status_from_isr();
            return copy(test_status);
        } else if (self->link_.test().origin_and_recalibrate_fixed()) {
            // Statusも実質Originのように機能しているらしいので
            // テストするときはパターン送信時以外常にニュートラルにする
            return copy(self->link_.test().make_stick_status_neutral_from_isr(snapshot.status));
        }

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
        // テスト用
        if (self->link_.test().enabled() || self->link_.test().origin_and_recalibrate_fixed()) {
            self->link_.publish_sent_origin_request_from_isr();
            const auto test_origin = self->link_.test().load_fixed_origin_from_isr();
            self->link_.shared_console().publish_sent_origin(test_origin);
            return copy(test_origin);
        }

        if (!snapshot.has_origin) {
            return 0;
        }
        return copy(snapshot.origin);
    case Joybus::Command::Recalibrate:
        // テスト用
        if (self->link_.test().enabled() || self->link_.test().origin_and_recalibrate_fixed()) {
            self->link_.publish_sent_recalibrate_request_from_isr();
            const auto test_recalibrate = self->link_.test().load_fixed_recalibrate_from_isr();
            self->link_.shared_console().publish_sent_recalibrate(test_recalibrate);
            return copy(test_recalibrate);
        }

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
