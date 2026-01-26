#pragma once
#include "double_buffer.hpp"
#include "joybus_reply.hpp"
#include "shared_pad.hpp"

namespace ConvertGcInput {
struct TxPair {
    uint32_t publish_count{0};
    uint32_t raw_publish_count{0};
    JoybusReply raw{};
    JoybusReply modified{};
};

class SharedPadHub {
  public:
    // 受信したパッド応答を書き込む: Padクライアント向け
    void on_pad_response_isr(Joybus::Command command, std::span<const uint8_t> rx) {
        rx_.on_response_isr(command, rx);
    }

    // 受信済みパッド応答を読み取る
    PadSnapshot load_raw_snapshot() const { return rx_.load(); }

    // コンソールへ送信する変換済みパッド応答を書き込む: Consoleクライアント向け
    void publish_tx_from_isr(uint32_t raw_publish_count, const JoybusReply &raw,
                             const JoybusReply &modified) {
        TxPair p{
            .publish_count{++tx_publish_count_},
            .raw_publish_count{raw_publish_count},
            .raw{raw},
            .modified{modified},
        };
        tx_.publish(p);
    }

    // コンソールへ送信した変換済みパッド応答を読み取る: main, Consoleクライアント向け
    TxPair load_last_tx() const { return tx_.load(); }

  private:
    SharedPad rx_;
    DoubleBuffer<TxPair> tx_;
    uint32_t tx_publish_count_{0};
};
} // namespace ConvertGcInput
