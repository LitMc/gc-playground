#pragma once
#include "gc_state.hpp"
#include "joybus_protocol.hpp"
#include "joybus_reply.hpp"
#include <array>
#include <span>

// https://jefflongo.dev/posts/gc-controller-reverse-engineering-part-1/#poll-mode
// JoybusのStatusレスポンスを共通の内部用形式に変換、復元するための処理群
namespace ConvertGcInput::gc {
// Status wordのうちボタンに対応する部分だけ差し替えるためのマスク
// 1になるビット位置: 0..4(A,B,X,Y,Start), 8..11(Dpad), 12(Z), 13(R), 14(L)
inline constexpr uint16_t kStatusWordButtonsMask = 0x7F1Fu;

// MSB-firstで届いた2バイトをuint16_tに変換
inline constexpr uint16_t load_big_endian_uint16(std::span<const uint8_t, 2> byte2) {
    return static_cast<uint16_t>((static_cast<uint16_t>(byte2[0]) << 8) | byte2[1]);
}

// 2バイト値をMSB-firstで格納
inline constexpr void store_big_endian_uint16(std::span<uint8_t, 2> byte2, uint16_t value) {
    byte2[0] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    byte2[1] = static_cast<uint8_t>(value & 0xFFu);
}

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
inline constexpr uint8_t pack4bitsToByte(uint8_t high4bits, uint8_t low4bits) {
    return static_cast<uint8_t>(((high4bits & 0x0Fu) << 4) | (low4bits & 0x0Fu));
}

// Statusレスポンスの共通形式。PollModeに依存しない。
struct DecodedStatus {
    uint16_t status_word{0};
    GcState state{};
};

// JoybusのStatusレスポンスを共通形式に変換
inline DecodedStatus decode_status(std::span<const uint8_t, Joybus::kStatusResponseSize> rx,
                                   Joybus::PollMode poll_mode) {
    DecodedStatus out{};

    // 先頭2バイト（Status word）を15, 14, ..., 0ビット目の順に並べる
    out.status_word = load_big_endian_uint16(rx.first<2>());
    // Status wordからボタン情報を抽出
    out.state.buttons = static_cast<uint16_t>(out.status_word & kStatusWordButtonsMask);

    out.state.stick_x = rx[2];
    out.state.stick_y = rx[3];

    // https://jefflongo.dev/posts/gc-controller-reverse-engineering-part-1/#poll-mode
    switch (poll_mode) {
    case Joybus::PollMode::Mode0:
        out.state.c_stick_x = rx[4];
        out.state.c_stick_y = rx[5];
        out.state.l_analog = expand4bitTo8bit((rx[6] >> 4) & 0x0Fu);
        out.state.r_analog = expand4bitTo8bit(rx[6] & 0x0Fu);
        out.state.a_analog = expand4bitTo8bit((rx[7] >> 4) & 0x0Fu);
        out.state.b_analog = expand4bitTo8bit(rx[7] & 0x0Fu);
        break;
    case Joybus::PollMode::Mode1:
        out.state.c_stick_x = expand4bitTo8bit((rx[4] >> 4) & 0x0Fu);
        out.state.c_stick_y = expand4bitTo8bit(rx[4] & 0x0Fu);
        out.state.l_analog = rx[5];
        out.state.r_analog = rx[6];
        out.state.a_analog = expand4bitTo8bit((rx[7] >> 4) & 0x0Fu);
        out.state.b_analog = expand4bitTo8bit(rx[7] & 0x0Fu);
        break;
    case Joybus::PollMode::Mode2:
        out.state.c_stick_x = expand4bitTo8bit((rx[4] >> 4) & 0x0Fu);
        out.state.c_stick_y = expand4bitTo8bit(rx[4] & 0x0Fu);
        out.state.l_analog = expand4bitTo8bit((rx[5] >> 4) & 0x0Fu);
        out.state.r_analog = expand4bitTo8bit(rx[5] & 0x0Fu);
        out.state.a_analog = rx[6];
        out.state.b_analog = rx[7];
        break;
    case Joybus::PollMode::Mode3:
        out.state.c_stick_x = rx[4];
        out.state.c_stick_y = rx[5];
        out.state.l_analog = rx[6];
        out.state.r_analog = rx[7];
        break;
    case Joybus::PollMode::Mode4:
        out.state.c_stick_x = rx[4];
        out.state.c_stick_y = rx[5];
        out.state.a_analog = rx[6];
        out.state.b_analog = rx[7];
        break;
    default:
        break;
    }

    return out;
}

// 共通形式のStatus情報をJoybusレスポンス形式に変換
inline JoybusReply encode_status(const GcState &state, Joybus::PollMode poll_mode,
                                 uint16_t base_status_word) {
    std::array<uint8_t, Joybus::kStatusResponseSize> out{};

    // Status wordのボタン部分だけ差し替え
    const uint16_t status_word = static_cast<uint16_t>(
        (base_status_word & ~kStatusWordButtonsMask) | (state.buttons & kStatusWordButtonsMask));
    store_big_endian_uint16(std::span<uint8_t, 2>(out.data(), 2), status_word);

    out[2] = state.stick_x;
    out[3] = state.stick_y;

    // https://jefflongo.dev/posts/gc-controller-reverse-engineering-part-1/#poll-mode
    switch (poll_mode) {
    case Joybus::PollMode::Mode0:
        out[4] = state.c_stick_x;
        out[5] = state.c_stick_y;
        out[6] =
            pack4bitsToByte(shrink8bitTo4bit(state.l_analog), shrink8bitTo4bit(state.r_analog));
        out[7] =
            pack4bitsToByte(shrink8bitTo4bit(state.a_analog), shrink8bitTo4bit(state.b_analog));
        break;
    case Joybus::PollMode::Mode1:
        out[4] =
            pack4bitsToByte(shrink8bitTo4bit(state.c_stick_x), shrink8bitTo4bit(state.c_stick_y));
        out[5] = state.l_analog;
        out[6] = state.r_analog;
        out[7] =
            pack4bitsToByte(shrink8bitTo4bit(state.a_analog), shrink8bitTo4bit(state.b_analog));
        break;
    case Joybus::PollMode::Mode2:
        out[4] =
            pack4bitsToByte(shrink8bitTo4bit(state.c_stick_x), shrink8bitTo4bit(state.c_stick_y));
        out[5] =
            pack4bitsToByte(shrink8bitTo4bit(state.l_analog), shrink8bitTo4bit(state.r_analog));
        out[6] = state.a_analog;
        out[7] = state.b_analog;
        break;
    case Joybus::PollMode::Mode3:
        out[4] = state.c_stick_x;
        out[5] = state.c_stick_y;
        out[6] = state.l_analog;
        out[7] = state.r_analog;
        break;
    case Joybus::PollMode::Mode4:
        out[4] = state.c_stick_x;
        out[5] = state.c_stick_y;
        out[6] = state.a_analog;
        out[7] = state.b_analog;
        break;
    default:
        break;
    }

    return JoybusReply(Joybus::Command::Status, out);
}

static_assert(shrink8bitTo4bit(expand4bitTo8bit(0x0)) == 0x0);
static_assert(shrink8bitTo4bit(expand4bitTo8bit(0x8)) == 0x8);
static_assert(shrink8bitTo4bit(expand4bitTo8bit(0xF)) == 0xF);
static_assert(pack4bitsToByte(0xA, 0x5) == 0xA5);

} // namespace ConvertGcInput::gc
