#pragma once
#include "joybus_reply.hpp"
#include "pad_console_link.hpp"
#include "shared_console.hpp"
#include <array>

namespace ConvertGcInput::Test {

struct TestInputFrames {
    // 送信する入力フレームのリスト
    std::span<const JoybusReply> frames{};
    // 繰り返すかどうか
    bool loop{false};
    // 何フレームごとに送信するか
    std::size_t send_interval_frames{1};
};

struct InitialPadState {
    JoybusReply id{Joybus::Command::Id,
                   std::array<uint8_t, Joybus::kIdResponseSize>{0x09, 0x00, 0x00}};
    JoybusReply status{Joybus::Command::Status, std::array<uint8_t, Joybus::kStatusResponseSize>{
                                                    0x00,
                                                    0x80, // 先頭に常に1のビットがあり0x80
                                                    0x80, // Joystick X
                                                    0x80, // Joystick Y
                                                    0x80, // C Stick X
                                                    0x80, // C Stick Y
                                                    0x00, // L Analog
                                                    0x00, // R Analog
                                                }};
    JoybusReply origin{Joybus::Command::Origin, std::array<uint8_t, Joybus::kOriginResponseSize>{
                                                    0x00,
                                                    0x80, // 先頭に常に1のビットがあり0x80
                                                    0x80, // Joystick X
                                                    0x80, // Joystick Y
                                                    0x80, // C Stick X
                                                    0x80, // C Stick Y
                                                    0x00, // L Analog
                                                    0x00, // R Analog
                                                    0x00, // A Analog
                                                    0x00, // B Analog
                                                }};
    JoybusReply recalibrate{Joybus::Command::Recalibrate,
                            std::array<uint8_t, Joybus::kRecalibrateResponseSize>{
                                0x00,
                                0x80, // 先頭に常に1のビットがあり0x80
                                0x80, // Joystick X
                                0x80, // Joystick Y
                                0x80, // C Stick X
                                0x80, // C Stick Y
                                0x00, // L Analog
                                0x00, // R Analog
                                0x00, // A Analog
                                0x00, // B Analog
                            }};
    JoybusReply reset{Joybus::Command::Reset,
                      std::array<uint8_t, Joybus::kResetResponseSize>{0x09, 0x00, 0x00}};
};

class TestPadClient {
  public:
    TestPadClient(PadConsoleLink &link, TestInputFrames test_pattern)
        : link_{link}, test_pattern_{test_pattern} {
        last_test_epoch_ = link_.load_measure_epoch();
        seed_test_snapshot_(InitialPadState{});
    }

    // mainループから呼ぶ（非ブロッキング）
    void tick(uint32_t now_us, const ConsoleState &console);

    // 予定していたフレームに送信できたか
    bool sent_ontime() const { return !send_delayed_; }

    // テスト開始直後のOriginで困らないように初期状態をセットしておく
    void seed_test_snapshot_(const InitialPadState &s) {
        auto &test = link_.test_pad_hub();
        auto feed = [&](const JoybusReply &r) {
            if (r.command() == Joybus::Command::Invalid)
                return;
            if (r.view().empty())
                return;
            test.on_pad_response_isr(r.command(), r.view());
        };
        feed(s.status);
        feed(s.id);
        feed(s.origin);
        feed(s.recalibrate);
        feed(s.reset);
    }

  private:
    void reset_test_state_() {
        next_index_to_send_ = 0;
        last_publish_count_ = 0;
        send_delayed_ = false;
    }

  private:
    PadConsoleLink &link_;
    TestInputFrames test_pattern_{};

    // 次に送信する入力フレームのインデックス
    std::size_t next_index_to_send_{0};

    // 最後に送信したときのエポック
    uint32_t last_publish_count_{0};

    // 最後に実行したテストのエポック（テスト開始検知用）
    uint32_t last_test_epoch_{0};

    bool send_delayed_{false};
};
} // namespace ConvertGcInput::Test
