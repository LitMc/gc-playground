#pragma once
#include "domain/identity.hpp"
#include "domain/report.hpp"
#include "domain/state.hpp"
#include "joybus/codec/identity_wire.hpp"
#include "joybus/codec/report_wire.hpp"
#include "joybus/codec/state_wire.hpp"
#include "joybus/protocol/protocol.hpp"
#include "link/policy.hpp"
#include "util/latch.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace ConvertGcInput {
struct PadSnapshot {
    uint32_t publish_count{0};
    Joybus::Command last_rx_command{Joybus::Command::Id};

    domain::PadIdentity identity{};
    domain::PadState status{};
    domain::PadState origin{};
};

class SharedPad {
  public:
    // パッドの最新スナップショットを得る
    PadSnapshot load() const { return db_.load(); }

    // パッドからの応答を記録
    void on_response_isr(Joybus::Command command, std::span<const uint8_t> rx) {
        bool got_valid_frame = false;
        switch (command) {
        case Joybus::Command::Status: {
            if (rx.size() != Joybus::kStatusResponseSize) {
                break;
            }
            auto view = std::span<const uint8_t, Joybus::kStatusResponseSize>(rx);

            auto decoded =
                ConvertGcInput::Joybus::state::decode_status(view, policy::kPadPollModeForQuery);
            shadow_.status.report = decoded.report;
            shadow_.status.input = decoded.input;
            got_valid_frame = true;
            break;
        }
        // OriginとRecalibrateは同じフォーマット
        case Joybus::Command::Origin:
        case Joybus::Command::Recalibrate: {
            if (rx.size() != Joybus::kOriginResponseSize) {
                break;
            }
            auto view = std::span<const uint8_t, Joybus::kOriginResponseSize>(rx);
            auto decoded = ConvertGcInput::Joybus::state::decode_origin(view);
            shadow_.origin.report = decoded.report;
            shadow_.origin.input = decoded.input;
            got_valid_frame = true;
            break;
        }
        case Joybus::Command::Id:
        case Joybus::Command::Reset: {
            if (rx.size() != Joybus::kIdResponseSize) {
                break;
            }
            auto view = std::span<const uint8_t, Joybus::kIdResponseSize>(rx);
            Joybus::identity::update_identity_from_id_bytes(shadow_.identity, view);
            got_valid_frame = true;
            break;
        }
        default:
            break;
        }

        if (got_valid_frame) {
            shadow_.publish_count++;
            shadow_.last_rx_command = command;
            db_.publish(shadow_);
        }
    }

  private:
    PadSnapshot shadow_{};    // IRQでの書き込み専用
    Latch<PadSnapshot> db_{}; // 外部から読み取る用
};

} // namespace ConvertGcInput
