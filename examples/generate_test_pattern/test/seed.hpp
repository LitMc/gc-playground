#pragma once
#include "codec/joybus/common.hpp"
#include "codec/joybus/identity.hpp"
#include "codec/joybus/state.hpp"
#include "pad_console_link.hpp"

// テスト用に初期応答を流し込む
namespace ConvertGcInput::test {

struct SeedOptions {
    bool status{true};
    bool origin{true};
    bool recalibrate{true};
    bool id{true};
    bool reset{true};
};

inline core::PadState make_neutral_pad_state() {
    // 初期値と変わらないが明示
    core::PadState state{};
    state.input.clear_buttons();
    state.input.set_analog_neutral();
    state.report = core::PadReport{};
    return state;
}

inline core::PadIdentity make_default_pad_identity_from_console(const ConsoleState &console) {
    core::PadIdentity id{};
    id.runtime.poll_mode = Joybus::common::to_core_poll_mode(console.poll_mode);
    id.runtime.rumble_mode = Joybus::common::to_core_rumble_mode(console.rumble_mode);
    return id;
}

// テスト用PadHubにコントローラ応答を流す
inline void feed_reply_to_test_hub(SharedPadHub &hub, const JoybusReply &reply) {
    if (reply.command() == Joybus::Command::Invalid) {
        return;
    }

    const auto view = reply.view();
    if (view.empty()) {
        return;
    }
    hub.on_pad_response_isr(reply.command(), view);
}

// テスト開始直後のOriginで困らないよう初期応答をセットする
inline void seed_test_initial_responses(PadConsoleLink &link, const ConsoleState &console,
                                        SeedOptions options = {}) {
    auto &hub = link.test_pad_hub();

    const core::PadState neutral = make_neutral_pad_state();

    if (options.status) {
        const auto reply = Joybus::state::encode_status(neutral, console.poll_mode);
        feed_reply_to_test_hub(hub, reply);
    }
    if (options.origin) {
        const auto reply = Joybus::state::encode_origin(neutral);
        feed_reply_to_test_hub(hub, reply);
    }
    if (options.recalibrate) {
        const auto reply = Joybus::state::encode_recalibrate(neutral);
        feed_reply_to_test_hub(hub, reply);
    }
    if (options.id || options.reset) {
        const auto id = make_default_pad_identity_from_console(console);
        if (options.id) {
            const auto reply = Joybus::identity::encode_identity(id);
            feed_reply_to_test_hub(hub, reply);
        }
        if (options.reset) {
            const auto reply = Joybus::identity::encode_reset_as_id(id);
            feed_reply_to_test_hub(hub, reply);
        }
    }
}

} // namespace ConvertGcInput::test
