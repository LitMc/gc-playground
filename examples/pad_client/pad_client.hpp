#pragma once
#include "joybus_pio_sm.hpp"
#include "joybus_protocol.hpp"
#include "shared_console.hpp"
#include "shared_pad.hpp"
#include <atomic>
#include <span>

namespace ConvertGcInput {
class PadClient {
  public:
    explicit PadClient(JoybusPioSm &host_to_pad) : host_to_pad_(host_to_pad) {};

    // mainループから呼ぶ（非ブロッキング）
    void tick(uint32_t now_us, const ConsoleState &console);

    // 本体へ応答できるだけのパッド情報が揃っているか
    bool console_ready() const { return console_ready_.load(std::memory_order_relaxed); }

    // 本体からのResetが来たときに呼んで要リセットフラグを立てる
    void on_console_reset();

    // コントローラの最新スナップショット
    PadSnapshot snapshot() const;

    // Padからの応答を処理するISRコールバック
    void on_pad_response_isr(Joybus::Command command, std::span<const uint8_t> rx);

    // パッドからの応答を受信したときに呼ぶコールバック
    static std::size_t callback(void *user, const uint8_t *rx, std::size_t rx_len, uint8_t *tx,
                                std::size_t tx_max, uint32_t context);

    // パッドの状態
    enum class State : uint8_t {
        Disconnected,
        Resetting,       // 本体からのResetをコントローラに伝えて再初期化
        BootId,          // 初回のID取得待ち
        BootOrigin,      // 初回のOrigin取得待ち
        BootRecalibrate, // 初回のRecalibrate取得待ち
        WarmStatus,      // 初回のStatus取得待ち
        Ready,           // Statusのポーリング開始済み
    };

  private:
    template <std::size_t N>
    bool send_request_(const Joybus::Request<N> &request, uint32_t now_us, uint32_t timeout_us) {
        if (waiting_response_) {
            return false;
        }
        if (request.bytes().empty()) {
            return false;
        }

        const auto before_publish_count = shared_pad_.load().publish_count;

        const auto bytes = request.bytes();
        bool send_ok = host_to_pad_.send_now(bytes.data(), bytes.size(),
                                             static_cast<uint32_t>(request.command()));
        if (!send_ok) {
            return false;
        }

        await_publish_count_ = before_publish_count;
        waiting_response_ = true;
        response_deadline_us_ = now_us + timeout_us;
        return true;
    }

    // 次の状態へ遷移
    void enter_state_(State next);

    // 応答待ち状態を強制解除して再送できるようにする
    void abort_wait_();

    // コマンド送信後の応答待ち時間が経過したか
    static bool is_timeout_reached(uint32_t now_us, uint32_t deadline_us) {
        // deadline_us = deadline設定時のnow + timeout_us
        // timeout_usが2^31未満なら判定時点のnow_usがラップしていても正しく判定できる
        // reach前の差分 = ラップ直前の大きな値をとるnow_us - ラップ直後の小さな値をとるdeadline_us
        // この差分の最上位ビットに1が立っていれば符号付き整数の負の値になる
        // 実際0xFFFFFFB0 - 0x00000020 = 0xFFFFFF90 (負の値)のような感じで負になる
        // 差分が0x80000000以上だとこの判定は壊れるがtimeout_usを2^31未満にすれば起こらない
        // 2^31usも待たないので大丈夫
        return (int32_t)(now_us - deadline_us) >= 0;
    }

    // 本体へ応答できる状態かを更新
    void publish_ready_() {
        console_ready_.store(state_ == State::Ready, std::memory_order_relaxed);
    }

  private:
    JoybusPioSm &host_to_pad_;
    SharedPad shared_pad_{};

    State state_{State::Disconnected};

    // 本体へ応答できるだけのパッド情報が揃っているか
    std::atomic<bool> console_ready_{false};

    // この送信以降にpublish_countが増えたら応答が来たと判断する基準
    uint32_t await_publish_count_{0};
    // 応答待ちのタイムアウト時間
    uint32_t response_deadline_us_{0};

    // 応答を待っているコマンド
    Joybus::Command await_command_{Joybus::Command::Id};

    // alive判定
    uint32_t last_seen_us_{0};
    uint32_t last_publish_count_{0};

    // 本体からのReset要求
    std::atomic<bool> pending_console_reset_{false};

    // Ready中のStatus送信間隔
    uint32_t next_status_due_us_{0};
    // 受信待ちか否か
    bool waiting_response_ = false;

    // 最後の応答からこれ以上経過するとパッド切断とみなす時間
    static constexpr uint32_t PAD_TIMEOUT_US = 100'000;

    // 初回接続時の応答待ちタイムアウト時間
    static constexpr uint32_t BOOT_TIMEOUT_US = 30'000;

    // 二重送信の心配はないので送信可能になるまで最速でポーリングをかけ続ける（入力遅延を減らすため）
    static constexpr uint32_t STATUS_PERIOD_US = 0;
    static constexpr uint32_t RETRY_DELAY_US = 0;
};
} // namespace ConvertGcInput
