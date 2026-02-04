#include "test_pad_client.hpp"

namespace ConvertGcInput::Test {
void TestPadClient::tick(uint32_t now_us, const ConsoleState &console) {
    auto &hub = link_.test_pad_hub();

    const uint32_t served = hub.load_last_tx().publish_count;
    if (link_.consume_measure_epoch(last_test_epoch_)) {
        // テストの有効/無効が切り替わった
        reset_test_state_();
        return;
    }

    // テスト中でなければ送らない
    if (!link_.is_test_enabled()) {
        return;
    }

    // 送るものがなければ送らない
    if (test_pattern_.frames.empty()) {
        return;
    }

    // インターバルが0だと送信していないのに次を送ろうとしてしまうので1以上にまるめる
    const uint32_t interval = test_pattern_.send_interval_frames == 0
                                  ? 1
                                  : static_cast<uint32_t>(test_pattern_.send_interval_frames);

    const uint32_t elapsed = served - last_publish_count_;

    if (elapsed < interval) {
        // まだ送る間隔に達していない
        return;
    } else if (elapsed > interval) {
        // 予定していたフレーム送信に間に合わなかった
        send_delayed_ = true;
    }
    last_publish_count_ = served;

    if (next_index_to_send_ >= test_pattern_.frames.size()) {
        if (test_pattern_.loop) {
            next_index_to_send_ = 0;
        } else {
            // もう送るものがない
            return;
        }
    }

    const JoybusReply &reply = test_pattern_.frames[next_index_to_send_];
    hub.on_pad_response_isr(reply.command(), reply.view());
    next_index_to_send_++;
}
} // namespace ConvertGcInput::Test
