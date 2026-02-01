#include "console_client.hpp"
#include "codec/joybus/identity.hpp"
#include "codec/joybus/state.hpp"

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
    self->link_.shared_console().on_request_isr(std::span<const uint8_t>(rx, rx_len));

    if (!self->link_.is_pad_ready()) {
        return 0;
    }

    auto &pad_hub = self->link_.active_pad_hub();
    const auto original_snapshot = pad_hub.load_original_snapshot();

    const auto cmd = static_cast<Joybus::Command>(rx[0]);

    // コンソールに指定されたPollModeとRumbleModeを応答に使う
    Joybus::PollMode host_poll_mode = self->link_.shared_console().load().poll_mode;
    Joybus::RumbleMode host_rumble_mode = self->link_.shared_console().load().rumble_mode;

    JoybusReply original_reply;
    JoybusReply modified_reply;

    switch (cmd) {
    case Joybus::Command::Status: {
        const core::PadState original_state = original_snapshot.status;
        original_reply = Joybus::state::encode_status(original_state, host_poll_mode);
        // TODO: あとで変換パイプラインを通す
        core::PadState modified_state = original_state;

        modified_reply = Joybus::state::encode_status(modified_state, host_poll_mode);
        break;
    }
    case Joybus::Command::Origin: {
        const core::PadState original_state = original_snapshot.origin;
        original_reply = Joybus::state::encode_origin(original_state);
        core::PadState modified_state = original_state;
        modified_reply = Joybus::state::encode_origin(modified_state);
        break;
    }
    case Joybus::Command::Recalibrate: {
        const core::PadState original_state = original_snapshot.origin;
        original_reply = Joybus::state::encode_recalibrate(original_state);
        core::PadState modified_state = original_state;
        modified_reply = Joybus::state::encode_recalibrate(modified_state);
        break;
    }
    case Joybus::Command::Id: {
        core::PadIdentity identity = original_snapshot.identity;
        // パッドからのID応答そのままではなく直近のコンソールから指定されたPollModeとRumbleModeを反映する
        // パッドへのポーリングはMode3固定でコンソールへの応答はコンソールからの指示に従う仕様のため
        identity.runtime.poll_mode = Joybus::common::to_core_poll_mode(host_poll_mode);
        identity.runtime.rumble_mode = Joybus::common::to_core_rumble_mode(host_rumble_mode);
        original_reply = Joybus::identity::encode_identity(identity);
        modified_reply = original_reply;
        break;
    }
    case Joybus::Command::Reset: {
        // パッドへリセットを要求
        self->link_.publish_pad_reset_request_from_isr();

        core::PadIdentity identity = original_snapshot.identity;
        identity.runtime.poll_mode = Joybus::common::to_core_poll_mode(host_poll_mode);
        identity.runtime.rumble_mode = Joybus::common::to_core_rumble_mode(host_rumble_mode);
        original_reply = Joybus::identity::encode_reset_as_id(identity);
        modified_reply = original_reply;
        break;
    }
    default:
        return 0;
    }

    if (original_reply.view().empty()) {
        return 0;
    }

    const std::size_t tx_len = self->write_tx(modified_reply, tx, tx_max);
    if (tx_len == 0) {
        return 0;
    }

    pad_hub.publish_tx_from_isr(original_snapshot.publish_count, original_reply, modified_reply);
    return tx_len;
}

} // namespace ConvertGcInput
