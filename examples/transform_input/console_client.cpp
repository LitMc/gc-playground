#include "console_client.hpp"

namespace ConvertGcInput {

std::size_t ConsoleClient::write_tx(const JoybusReply &reply, uint8_t *tx, std::size_t tx_max) {
    const auto view = reply.view();
    const auto length = view.size();
    if (length == 0 || tx_max < length) {
        return 0;
    }
    std::copy_n(view.data(), length, tx);
    return length;
}

std::size_t ConsoleClient::callback(void *user, const uint8_t *rx, std::size_t rx_len, uint8_t *tx,
                                    std::size_t tx_max) {
    if (rx_len < 1) {
        return 0;
    }

    auto *self = static_cast<ConsoleClient *>(user);
    self->shared_console_.on_request_isr(std::span<const uint8_t>(rx, rx_len));

    if (!self->link_.is_pad_ready()) {
        return 0;
    }

    auto &pad_hub = self->shared_pad_hub_;
    const auto raw_snapshot = pad_hub.load_raw_snapshot();

    const auto cmd = static_cast<Joybus::Command>(rx[0]);

    // こう書くと空のオブジェクトに詰めなくてすむ
    JoybusReply raw_reply = [&] {
        switch (cmd) {
        // PadがReadyの時点でReset以外は全て応答できる
        case Joybus::Command::Status:
            return JoybusReply{cmd, raw_snapshot.status};
        case Joybus::Command::Id:
            return JoybusReply{cmd, raw_snapshot.id};
        case Joybus::Command::Origin:
            return JoybusReply{cmd, raw_snapshot.origin};
        case Joybus::Command::Recalibrate:
            return JoybusReply{cmd, raw_snapshot.recalibrate};
        case Joybus::Command::Reset:
            // パッドにResetを伝える
            self->link_.publish_pad_reset_request_from_isr();

            // ResetはID相当で返す。reset応答が取れていればそれ、なければID応答を返す
            if (raw_snapshot.has_reset) {
                return JoybusReply{cmd, raw_snapshot.reset};
            } else {
                return JoybusReply{cmd, raw_snapshot.id};
            }
        default:
            return JoybusReply{};
        }
    }();

    if (raw_reply.view().empty()) {
        return 0;
    }

    JoybusReply modified_reply = raw_reply;
    // パッドの応答をパイプラインで変換
    self->link_.transform_pipeline().apply_from_isr(cmd, modified_reply);

    const std::size_t tx_len = write_tx(modified_reply, tx, tx_max);
    if (tx_len == 0) {
        return 0;
    }

    pad_hub.publish_tx_from_isr(raw_snapshot.publish_count, raw_reply, modified_reply);

    return tx_len;
}
} // namespace ConvertGcInput
