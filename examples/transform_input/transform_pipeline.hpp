#pragma once
#include "joybus_protocol.hpp"
#include "joybus_reply.hpp"
#include <array>
#include <atomic>
#include <span>

namespace ConvertGcInput {

// コマンド応答変換パイプライン
class TransformPipeline {
  public:
    static constexpr uint8_t command_bit(Joybus::Command cmd) {
        switch (cmd) {
        case Joybus::Command::Status:
            return 1 << 0;
        case Joybus::Command::Id:
            return 1 << 1;
        case Joybus::Command::Origin:
            return 1 << 2;
        case Joybus::Command::Recalibrate:
            return 1 << 3;
        case Joybus::Command::Reset:
            return 1 << 4;
        default:
            return 0;
        }
    }

    static constexpr uint8_t kCommandAll = 0xFF;

    using TransformFunction = void (*)(void *user, Joybus::Command command,
                                       std::span<uint8_t> reply);

    // パイプライン1段分の処理
    struct Stage {
        // コマンド応答に対して適用する変換関数
        TransformFunction function_to_apply{nullptr};
        // 変換処理に渡すコンテキスト
        void *user{nullptr};
        // このステージの変換を行うコマンドのビットマスク
        uint8_t command_selector_mask{kCommandAll};
    };

    // 変換ステージをパイプラインに追加できる最大段数
    static constexpr std::size_t kMaxStages = 4;

    // 変換ステージをパイプラインに追加
    bool add_stage(Stage stage) {
        if (!stage.function_to_apply) {
            return false;
        }

        if (stage_count_ >= kMaxStages) {
            return false;
        }
        stages_[stage_count_++] = stage;
        enable_stage_mask_.fetch_or(1u << stage_count_, std::memory_order_relaxed);
        ++stage_count_;
        return true;
    }

    // パイプラインの変換ステージ数
    std::size_t size() const { return stage_count_; }

    // i段目の変換ステージを有効(enable=true)または無効(enable=false)にする
    void set_stage_enabled(std::size_t index, bool enable) {
        if (index >= stage_count_) {
            return;
        }
        const uint32_t bit = 1u << index;
        if (enable) {
            enable_stage_mask_.fetch_or(bit, std::memory_order_relaxed);
        } else {
            enable_stage_mask_.fetch_and(~bit, std::memory_order_relaxed);
        }
    }

    // パイプラインの変換を適用: ISRから呼ぶ
    void apply_from_isr(Joybus::Command command, JoybusReply &reply) const {
        const uint32_t enabled_stage_mask = enable_stage_mask_.load(std::memory_order_acquire);
        const uint8_t command_bit_mask = command_bit(command);
        auto view = reply.view();

        for (std::size_t i = 0; i < stage_count_; ++i) {
            if ((enabled_stage_mask & (1u << i)) == 0) {
                // 無効なステージ
                continue;
            }
            const Stage &stage = stages_[i];
            if ((stage.command_selector_mask & command_bit_mask) == 0) {
                // コマンドが変換対象外
                continue;
            }
            stage.function_to_apply(stage.user, command, view);
        }
    }

  private:
    std::array<Stage, kMaxStages> stages_{};
    std::size_t stage_count_{0};

    // 変換を有効にするコマンドのビットマスク: ISRから読むのでatomic
    std::atomic<uint32_t> enable_stage_mask_{0};
};

} // namespace ConvertGcInput
