#pragma once
#include "codec/joybus/report.hpp"
#include "core/report.hpp"
#include "core/state.hpp"
#include "joybus_protocol.hpp"
#include "joybus_reply.hpp"
#include <array>
#include <span>

// https://jefflongo.dev/posts/gc-controller-reverse-engineering-part-1/#poll-mode
// コントローラ入力をJoybus形式に変換、復元するための処理群
namespace ConvertGcInput::Joybus::state {

// Status, Origin, Recalibrateレスポンスの共通形式。PollModeに依存しない。
struct DecodedStatus {
    core::PadReport report{};
    core::PadInput input{};
};

// 下位4ビットを取り出して8ビットに拡張。PollMode0..2のレスポンスには4bit幅のアナログ値が入る
// これを内部のアナログ値0..255に変換。中点0x8(8)が0x80(128)に対応するよう<<4する
inline constexpr uint8_t expand4bitTo8bit(uint8_t value4bit) {
    return static_cast<uint8_t>((value4bit & 0x0Fu) << 4);
}

// 8ビット値を4ビット値に縮小。内部のアナログ値0..255を4bit幅に変換
// PollMode0..2のレスポンスに使う
inline constexpr uint8_t shrink8bitTo4bit(uint8_t value8bit) {
    return static_cast<uint8_t>(value8bit >> 4);
}

// 2つの4ビット値を1バイトにまとめる
// PollMode0..2のレスポンスに使う
inline constexpr uint8_t pack4bitsToByte(uint8_t high4bits, uint8_t low4bits) {
    return static_cast<uint8_t>(((high4bits & 0x0Fu) << 4) | (low4bits & 0x0Fu));
}

// Status wordから共通形式のボタン入力を抽出
inline constexpr core::ButtonInput
decode_buttons_from_status_word(std::span<const uint8_t, 2> byte2) {
    core::ButtonInput buttons{};

    const uint16_t status_word = static_cast<uint16_t>((static_cast<uint16_t>(byte2[0]) << 8) |
                                                       static_cast<uint16_t>(byte2[1]));

    buttons.a = (status_word & core::to_mask(core::PadButton::A)) != 0;
    buttons.b = (status_word & core::to_mask(core::PadButton::B)) != 0;
    buttons.x = (status_word & core::to_mask(core::PadButton::X)) != 0;
    buttons.y = (status_word & core::to_mask(core::PadButton::Y)) != 0;
    buttons.start = (status_word & core::to_mask(core::PadButton::Start)) != 0;
    buttons.dpad_left = (status_word & core::to_mask(core::PadButton::DpadLeft)) != 0;
    buttons.dpad_right = (status_word & core::to_mask(core::PadButton::DpadRight)) != 0;
    buttons.dpad_down = (status_word & core::to_mask(core::PadButton::DpadDown)) != 0;
    buttons.dpad_up = (status_word & core::to_mask(core::PadButton::DpadUp)) != 0;
    buttons.z = (status_word & core::to_mask(core::PadButton::Z)) != 0;
    buttons.r = (status_word & core::to_mask(core::PadButton::R)) != 0;
    buttons.l = (status_word & core::to_mask(core::PadButton::L)) != 0;

    return buttons;
}

// 共通形式の実行時レポートをStatus wordに変換
inline constexpr void encode_to_status_word(const core::ButtonInput &buttons,
                                            const core::PadReport &report,
                                            std::span<uint8_t, 2> status_word_bytes) {
    using namespace ConvertGcInput::core;
    uint16_t status_word = 0;

    // ボタン情報をStatus wordにセット
    status_word |= (static_cast<uint16_t>(buttons.a) ? core::to_mask(core::PadButton::A) : 0u);
    status_word |= (static_cast<uint16_t>(buttons.b) ? core::to_mask(core::PadButton::B) : 0u);
    status_word |= (static_cast<uint16_t>(buttons.x) ? core::to_mask(core::PadButton::X) : 0u);
    status_word |= (static_cast<uint16_t>(buttons.y) ? core::to_mask(core::PadButton::Y) : 0u);
    status_word |=
        (static_cast<uint16_t>(buttons.start) ? core::to_mask(core::PadButton::Start) : 0u);
    status_word |=
        (static_cast<uint16_t>(buttons.dpad_left) ? core::to_mask(core::PadButton::DpadLeft) : 0u);
    status_word |=
        (static_cast<uint16_t>(buttons.dpad_right) ? core::to_mask(core::PadButton::DpadRight)
                                                   : 0u);
    status_word |=
        (static_cast<uint16_t>(buttons.dpad_down) ? core::to_mask(core::PadButton::DpadDown) : 0u);
    status_word |=
        (static_cast<uint16_t>(buttons.dpad_up) ? core::to_mask(core::PadButton::DpadUp) : 0u);
    status_word |= (static_cast<uint16_t>(buttons.z) ? core::to_mask(core::PadButton::Z) : 0u);
    status_word |= (static_cast<uint16_t>(buttons.r) ? core::to_mask(core::PadButton::R) : 0u);
    status_word |= (static_cast<uint16_t>(buttons.l) ? core::to_mask(core::PadButton::L) : 0u);

    // レポートフラグをStatus wordにセット
    // OriginNotSentはビットが立っているとOrigin未送信を意味するので反転
    status_word |=
        report.origin_sent ? 0 : static_cast<uint16_t>(report::StatusWordBits::OriginNotSent);
    status_word |=
        report.error_latched ? static_cast<uint16_t>(report::StatusWordBits::ErrorLatched) : 0;
    status_word |= report.error_last ? static_cast<uint16_t>(report::StatusWordBits::ErrorLast) : 0;
    status_word |= report.use_controller_origin
                       ? static_cast<uint16_t>(report::StatusWordBits::UseControllerOrigin)
                       : 0;

    status_word_bytes[0] = static_cast<uint8_t>((status_word >> 8) & 0xFFu);
    status_word_bytes[1] = static_cast<uint8_t>(status_word & 0xFFu);
}

// JoybusのStatusレスポンスを共通形式に変換
inline constexpr DecodedStatus
decode_status(std::span<const uint8_t, Joybus::kStatusResponseSize> rx,
              Joybus::PollMode poll_mode) {
    DecodedStatus out{};

    // 先頭2バイト（Status word）を共通形式のレポートに変換
    out.report = report::decode_report_from_status_word(rx.first<2>());
    // Status wordを共通形式のボタン入力に変換
    out.input.buttons = decode_buttons_from_status_word(rx.first<2>());

    auto &analog_input = out.input.analog;
    analog_input.stick_x = rx[2];
    analog_input.stick_y = rx[3];

    // https://jefflongo.dev/posts/gc-controller-reverse-engineering-part-1/#poll-mode
    switch (poll_mode) {
    case Joybus::PollMode::Mode0:
        analog_input.c_stick_x = rx[4];
        analog_input.c_stick_y = rx[5];
        analog_input.l_analog = expand4bitTo8bit((rx[6] >> 4) & 0x0Fu);
        analog_input.r_analog = expand4bitTo8bit(rx[6] & 0x0Fu);
        analog_input.a_analog = expand4bitTo8bit((rx[7] >> 4) & 0x0Fu);
        analog_input.b_analog = expand4bitTo8bit(rx[7] & 0x0Fu);
        break;
    case Joybus::PollMode::Mode1:
        analog_input.c_stick_x = expand4bitTo8bit((rx[4] >> 4) & 0x0Fu);
        analog_input.c_stick_y = expand4bitTo8bit(rx[4] & 0x0Fu);
        analog_input.l_analog = rx[5];
        analog_input.r_analog = rx[6];
        analog_input.a_analog = expand4bitTo8bit((rx[7] >> 4) & 0x0Fu);
        analog_input.b_analog = expand4bitTo8bit(rx[7] & 0x0Fu);
        break;
    case Joybus::PollMode::Mode2:
        analog_input.c_stick_x = expand4bitTo8bit((rx[4] >> 4) & 0x0Fu);
        analog_input.c_stick_y = expand4bitTo8bit(rx[4] & 0x0Fu);
        analog_input.l_analog = expand4bitTo8bit((rx[5] >> 4) & 0x0Fu);
        analog_input.r_analog = expand4bitTo8bit(rx[5] & 0x0Fu);
        analog_input.a_analog = rx[6];
        analog_input.b_analog = rx[7];
        break;
    case Joybus::PollMode::Mode3:
        analog_input.c_stick_x = rx[4];
        analog_input.c_stick_y = rx[5];
        analog_input.l_analog = rx[6];
        analog_input.r_analog = rx[7];
        break;
    case Joybus::PollMode::Mode4:
        analog_input.c_stick_x = rx[4];
        analog_input.c_stick_y = rx[5];
        analog_input.a_analog = rx[6];
        analog_input.b_analog = rx[7];
        break;
    default:
        break;
    }

    return out;
}

// 共通形式のStatus情報をJoybusレスポンス形式に変換
inline JoybusReply encode_status(const core::PadInput &input, const core::PadReport &report,
                                 Joybus::PollMode poll_mode) {
    std::array<uint8_t, Joybus::kStatusResponseSize> out{};

    // out[0], out[1]: Status word
    encode_to_status_word(input.buttons, report, std::span<uint8_t, 2>{out.data(), 2});

    const auto &analog_input = input.analog;

    out[2] = analog_input.stick_x;
    out[3] = analog_input.stick_y;

    // https://jefflongo.dev/posts/gc-controller-reverse-engineering-part-1/#poll-mode
    switch (poll_mode) {
    case Joybus::PollMode::Mode0:
        out[4] = analog_input.c_stick_x;
        out[5] = analog_input.c_stick_y;
        out[6] = pack4bitsToByte(shrink8bitTo4bit(analog_input.l_analog),
                                 shrink8bitTo4bit(analog_input.r_analog));
        out[7] = pack4bitsToByte(shrink8bitTo4bit(analog_input.a_analog),
                                 shrink8bitTo4bit(analog_input.b_analog));
        break;
    case Joybus::PollMode::Mode1:
        out[4] = pack4bitsToByte(shrink8bitTo4bit(analog_input.c_stick_x),
                                 shrink8bitTo4bit(analog_input.c_stick_y));
        out[5] = analog_input.l_analog;
        out[6] = analog_input.r_analog;
        out[7] = pack4bitsToByte(shrink8bitTo4bit(analog_input.a_analog),
                                 shrink8bitTo4bit(analog_input.b_analog));
        break;
    case Joybus::PollMode::Mode2:
        out[4] = pack4bitsToByte(shrink8bitTo4bit(analog_input.c_stick_x),
                                 shrink8bitTo4bit(analog_input.c_stick_y));
        out[5] = pack4bitsToByte(shrink8bitTo4bit(analog_input.l_analog),
                                 shrink8bitTo4bit(analog_input.r_analog));
        out[6] = analog_input.a_analog;
        out[7] = analog_input.b_analog;
        break;
    case Joybus::PollMode::Mode3:
        out[4] = analog_input.c_stick_x;
        out[5] = analog_input.c_stick_y;
        out[6] = analog_input.l_analog;
        out[7] = analog_input.r_analog;
        break;
    case Joybus::PollMode::Mode4:
        out[4] = analog_input.c_stick_x;
        out[5] = analog_input.c_stick_y;
        out[6] = analog_input.a_analog;
        out[7] = analog_input.b_analog;
        break;
    default:
        break;
    }

    return JoybusReply(Joybus::Command::Status, out);
}

inline constexpr DecodedStatus
decode_origin(std::span<const uint8_t, Joybus::kOriginResponseSize> rx) {
    DecodedStatus out{};

    out.report = report::decode_report_from_status_word(rx.first<2>());
    out.input.buttons = decode_buttons_from_status_word(rx.first<2>());
    auto &analog_input = out.input.analog;
    analog_input.stick_x = rx[2];
    analog_input.stick_y = rx[3];
    analog_input.c_stick_x = rx[4];
    analog_input.c_stick_y = rx[5];
    analog_input.l_analog = rx[6];
    analog_input.r_analog = rx[7];
    analog_input.a_analog = rx[8];
    analog_input.b_analog = rx[9];
    return out;
}

inline constexpr DecodedStatus
decode_recalibrate(std::span<const uint8_t, Joybus::kRecalibrateResponseSize> rx) {
    return decode_origin(rx);
}

inline constexpr std::array<uint8_t, Joybus::kOriginResponseSize>
encode_origin_byte(const core::PadInput &input, const core::PadReport &report) {
    std::array<uint8_t, Joybus::kOriginResponseSize> out{};

    // out[0], out[1]: Status word
    encode_to_status_word(input.buttons, report, std::span<uint8_t, 2>{out.data(), 2});

    const auto &analog_input = input.analog;

    out[2] = analog_input.stick_x;
    out[3] = analog_input.stick_y;
    out[4] = analog_input.c_stick_x;
    out[5] = analog_input.c_stick_y;
    out[6] = analog_input.l_analog;
    out[7] = analog_input.r_analog;
    out[8] = analog_input.a_analog;
    out[9] = analog_input.b_analog;

    return out;
}

inline JoybusReply encode_origin(const core::PadInput &input, const core::PadReport &report) {
    return JoybusReply(Joybus::Command::Origin, encode_origin_byte(input, report));
}

inline JoybusReply encode_recalibrate(const core::PadInput &input, const core::PadReport &report) {
    return JoybusReply(Joybus::Command::Recalibrate, encode_origin_byte(input, report));
}

static_assert(shrink8bitTo4bit(expand4bitTo8bit(0x0)) == 0x0);
static_assert(shrink8bitTo4bit(expand4bitTo8bit(0x8)) == 0x8);
static_assert(shrink8bitTo4bit(expand4bitTo8bit(0xF)) == 0xF);
static_assert(pack4bitsToByte(0xA, 0x5) == 0xA5);
} // namespace ConvertGcInput::Joybus::state
