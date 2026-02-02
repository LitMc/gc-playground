#pragma once
#include "codec/joybus/state.hpp"
#include "measure/pattern.hpp"
#include "measure/scheduler.hpp"
#include "measure/seed.hpp"
#include "pad_console_link.hpp"
#include "shared_console.hpp"

namespace ConvertGcInput::measure {

template <TestPattern P> class PadInjector {
  public:
    PadInjector(PadConsoleLink &link, Schedule schedule, P pattern)
        : link_{link}, schedule_{schedule}, pattern_{pattern} {
        last_test_epoch_ = link_.load_test_epoch();
    }

    // mainループから呼ぶ（非ブロッキング）
    void tick(uint32_t now_us) {
        if (link_.consume_test_epoch(last_test_epoch_)) {
            // テストモードの切り替えを検知したらリセット
            reset_();
            // テスト開始前に初期応答をセット
            if (link_.is_test_enabled()) {
                const auto console = link_.shared_console().load();
                seed_test_initial_responses(link_, console);
            }
            // 切り替え直後は送らず次のtickにまかせる
            return;
        }

        if (!link_.is_test_enabled()) {
            return;
        }

        const uint32_t steps = schedule_.poll_steps(now_us);
        if (steps == 0) {
            return;
        }

        domain::PadState state{};
        if (!pattern_.sample_and_advance(state, steps)) {
            return;
        }

        auto &hub = link_.test_pad_hub();
        const auto console = link_.shared_console().load();
        // 対PadのポーリングなのでMode3で問い合わせたことにしておく
        const auto reply = Joybus::state::encode_status(state, Joybus::PollMode::Mode3);
        hub.on_pad_response_isr(reply.command(), reply.view());
    }

  private:
    void reset_() {
        schedule_.reset();
        pattern_.reset();
    }

  private:
    PadConsoleLink &link_;
    Schedule schedule_;
    P pattern_;

    // 最後に実行したテストのエポック（テスト開始検知用）
    uint32_t last_test_epoch_{0};
};
} // namespace ConvertGcInput::measure
