#pragma once
#include <type_traits>

namespace ConvertGcInput::gc {

// 意味上のボタン集合
// 値はStatusレスポンス先頭2バイト（Status word）の最下位ビットから数えた位置に対応
// https://jefflongo.dev/posts/gc-controller-reverse-engineering-part-1/?utm_source=chatgpt.com#status-response-bytes-1-and-2
enum class GcButton : uint16_t {
    A = (1u << 0),
    B = (1u << 1),
    X = (1u << 2),
    Y = (1u << 3),
    Start = (1u << 4),

    // 5~7はボタン以外のビット

    DpadLeft = (1u << 8),
    DpadRight = (1u << 9),
    DpadDown = (1u << 10),
    DpadUp = (1u << 11),

    Z = (1u << 12),
    R = (1u << 13),
    L = (1u << 14),
};

constexpr inline uint16_t to_mask(GcButton button) { return static_cast<uint16_t>(button); }

struct GcState {
    // スティックの中心
    static constexpr uint8_t kAxisCenter{0x80};

    // ボタン群
    uint16_t buttons{0};

    // スティック（0..255, center=128）
    uint8_t stick_x{kAxisCenter};
    uint8_t stick_y{kAxisCenter};
    uint8_t c_stick_x{kAxisCenter};
    uint8_t c_stick_y{kAxisCenter};

    // アナログトリガー（0..255）
    uint8_t l_analog{0};
    uint8_t r_analog{0};
    // アナログボタン（未使用）
    uint8_t a_analog{0};
    uint8_t b_analog{0};

    constexpr bool pressed(GcButton button) const { return (buttons & to_mask(button)) != 0; }

    constexpr void set(GcButton button, bool on = true) {
        const uint16_t mask = to_mask(button);
        if (on) {
            buttons |= mask;
        } else {
            buttons &= ~mask;
        }
    }

    constexpr void clear(GcButton button) { set(button, false); }

    constexpr void clear_buttons() { buttons = 0; }

    constexpr void set_neutral_axes() {
        stick_x = kAxisCenter;
        stick_y = kAxisCenter;
        c_stick_x = kAxisCenter;
        c_stick_y = kAxisCenter;
        l_analog = 0;
        r_analog = 0;
        a_analog = 0;
        b_analog = 0;
    }
};

// ISRやダブルバッファでコピーしても安全
static_assert(std::is_trivially_copyable_v<GcState>);
static_assert(std::is_standard_layout_v<GcState>);
} // namespace ConvertGcInput::gc
