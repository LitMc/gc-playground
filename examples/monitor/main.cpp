#include "hardware/pio.h"
#include "hardware/sync.h"
#include "joybus_console.pio.h"
#include "joybus_pad.pio.h"
#include "joybus_pio_sm.hpp"
#include "joybus_protocol.hpp"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include <algorithm>
#include <stdio.h>
#include <vector>

namespace {
// 通電確認用のオンボードLED
constexpr uint ONBOARD_LED_PIN = PICO_DEFAULT_LED_PIN;
// BOOTSELに入るためのボタン入力
constexpr uint BOOT_BTN_PIN = 26; // GP26
// JoyBus
constexpr uint CONSOLE_PIN = 15;
constexpr uint PAD_PIN = 16;

void boot_btn_irq(uint gpio, uint32_t events) {
    // ちょいデバウンス（押しっぱなし連打対策）
    busy_wait_ms(100);
    if (gpio_get(BOOT_BTN_PIN) == 0) {
        printf("BOOTSEL button pressed. Entering USB boot mode...\n");
        reset_usb_boot(0, 0);
    }
}

void bootsel_button_init() {
    gpio_init(BOOT_BTN_PIN);
    gpio_set_dir(BOOT_BTN_PIN, GPIO_IN);
    gpio_pull_up(BOOT_BTN_PIN);
    gpio_set_irq_enabled_with_callback(BOOT_BTN_PIN, GPIO_IRQ_EDGE_FALL, true, &boot_btn_irq);
}

void init_led() {
    gpio_init(ONBOARD_LED_PIN);
    gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);
    gpio_put(ONBOARD_LED_PIN, 1);
}
} // namespace

struct RxSnapshot {
    std::array<uint8_t, ConvertGcInput::JoybusPioSm::JOYBUS_MAX_FRAME_BYTES> rx{};
    std::size_t rx_len = 0;

    void set_from(const uint8_t *data, std::size_t len) {
        rx.fill(0);
        rx_len = std::min<std::size_t>(len, rx.size());
        std::copy_n(data, rx_len, rx.data());
    }

    bool equals_prefix(const RxSnapshot &other) const {
        return (rx_len == other.rx_len) &&
               std::equal(rx.begin(), rx.begin() + rx_len, other.rx.begin());
    }
};

RxSnapshot rx_snapshot_console{};

void dump_if_rx(ConvertGcInput::JoybusPioSm &device, const char *name) {
    if (!device.rx_ready() && !device.rx_bad()) {
        printf("No data on %s\n", name);
        return;
    }
    std::array<uint8_t, ConvertGcInput::JoybusPioSm::JOYBUS_MAX_FRAME_BYTES> buffer{};
    uint32_t length = 0;
    bool bad = false;

    uint32_t s = save_and_disable_interrupts();
    bad = device.rx_bad();
    length = device.rx_length();
    if (!bad && length > 0) {
        if (length > ConvertGcInput::JoybusPioSm::JOYBUS_MAX_FRAME_BYTES) {
            length = ConvertGcInput::JoybusPioSm::JOYBUS_MAX_FRAME_BYTES;
        }
        std::copy_n(device.rx_data(), length, buffer.data());
    }
    device.clear_rx_status();
    restore_interrupts(s);

    if (bad) {
        printf("[%s] RX bad (missing/invalid stop)\n", name);
        return;
    }

    RxSnapshot current_snapshot{};
    current_snapshot.set_from(buffer.data(), length);

    if (current_snapshot.equals_prefix(rx_snapshot_console)) {
        // 前回と同じ内容なら表示しない
        return;
    }
    rx_snapshot_console = current_snapshot;

    printf("[%s] RX (%lu):", name, (unsigned long)length);
    for (uint32_t i = 0; i < length; ++i) {
        printf(" %02X", current_snapshot.rx[i]);
    }
    printf("\n");
}

std::size_t __time_critical_func(console_callback)(void *user, const uint8_t *rx,
                                                   std::size_t rx_len, uint8_t *tx,
                                                   std::size_t tx_max) {
    return 0;
}

std::size_t __time_critical_func(pad_callback)(void *user, const uint8_t *rx, std::size_t rx_len,
                                               uint8_t *tx, std::size_t tx_max) {
    return 0;
}

namespace jb = ConvertGcInput::Joybus;

template <std::size_t N>
static inline void send_request_from(ConvertGcInput::JoybusPioSm &device, const char *name,
                                     const jb::Request<N> &request, bool dump_tx = true) {
    const auto bytes = request.bytes();
    if (dump_tx) {
        printf("[%s] TX (%zu):", name, bytes.size());
        for (const auto &b : bytes) {
            printf(" %02X", b);
        }
        printf("\n");
    }
    device.send_now(bytes.data(), bytes.size());
}

int main() {
    stdio_init_all();

    // ボタンを押すだけでBOOTSELに入るようにする
    bootsel_button_init();

    // 動作開始の確認用にオンボードLEDを光らせる
    init_led();

    // コンソールとパッドそれぞれのステートマシンを確保
    PIO console_pio = pio0;
    PIO pad_pio = pio1;
    const uint sm_console = pio_claim_unused_sm(console_pio, true);
    const uint sm_pad = pio_claim_unused_sm(pad_pio, true);

    ConvertGcInput::JoybusPioSm::Config console_config{
        .pio = console_pio,
        .state_machine = sm_console,
        .pin = CONSOLE_PIN,
        .program = &joybus_console_program,
        .get_default_config = &joybus_console_program_get_default_config,
        .rx_start_offset = joybus_console_offset_rx_start,
        .tx_start_offset = joybus_console_offset_tx_start,
        .pio_hz = 4'000'000,
        .irq_base = 0,
    };

    ConvertGcInput::JoybusPioSm::Config pad_config{
        .pio = pad_pio,
        .state_machine = sm_pad,
        .pin = PAD_PIN,
        .program = &joybus_pad_program,
        .get_default_config = &joybus_pad_program_get_default_config,
        .rx_start_offset = joybus_pad_offset_rx_start,
        .tx_start_offset = joybus_pad_offset_tx_start,
        .pio_hz = 4'000'000,
        .irq_base = 0,
    };

    ConvertGcInput::JoybusPioSm console(console_config);
    ConvertGcInput::JoybusPioSm pad(pad_config);

    console.set_callback(&console_callback, nullptr);
    pad.set_callback(&pad_callback, nullptr);

    printf("JoybusPioSm ready.\n");
    printf("console: PIO%d SM%u pin GP%u\n", pio_get_index(console_config.pio),
           console_config.state_machine, CONSOLE_PIN);
    printf("pad    : PIO%d SM%u pin GP%u\n", pio_get_index(pad_config.pio),
           pad_config.state_machine, PAD_PIN);

    send_request_from(console, "console", jb::Id);
    sleep_us(200);
    dump_if_rx(console, "console");

    send_request_from(console, "console", jb::Origin);
    sleep_us(500);
    dump_if_rx(console, "console");

    while (true) {
        send_request_from(console, "console",
                          jb::Status(jb::PollMode::Default, jb::RumbleMode::Off), false);
        sleep_us(500);
        dump_if_rx(console, "console");
        sleep_ms(16);
    }
}
