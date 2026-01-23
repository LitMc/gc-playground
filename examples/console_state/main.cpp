#include "hardware/pio.h"
#include "hardware/sync.h"
#include "joybus_console.pio.h"
#include "joybus_pad.pio.h"
#include "joybus_pio_sm.hpp"
#include "joybus_protocol.hpp"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "shared_console.hpp"
#include "shared_pad.hpp"
#include <algorithm>
#include <atomic>
#include <stdio.h>
#include <vector>

namespace {
// 通電確認用のオンボードLED
constexpr uint ONBOARD_LED_PIN = PICO_DEFAULT_LED_PIN;
// BOOTSELに入るためのボタン入力
constexpr uint BOOT_BTN_PIN = 26; // GP26
// JoyBus
constexpr uint PIN_TO_REAL_PAD = 15;
constexpr uint PIN_TO_REAL_CONSOLE = 16;

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

namespace jb = ConvertGcInput::Joybus;

static ConvertGcInput::SharedPad g_shared_pad{};
static ConvertGcInput::SharedConsole g_shared_console{};

// Padへ投げたコマンド種別（Padの応答解釈に用いる）
static std::atomic<uint8_t> g_expected_command{
    static_cast<uint8_t>(ConvertGcInput::Joybus::Command::Id)};

// Consoleへ応答する準備ができたか（Pad側のID/Origin/Recalibrateが揃ったらtrue）
static std::atomic<bool> g_console_ready{false};

// Reset検知用
static uint16_t prev_reset_count = 0;

void dump_pad_snapshot(const ConvertGcInput::PadSnapshot &snapshot) {
    printf("PadSnapshot:\n");
    if (snapshot.has_id) {
        printf("  ID: ");
        for (const auto &b : snapshot.id) {
            printf("%02X ", b);
        }
        printf("\n");
    } else {
        printf("  ID: <none>\n");
    }

    if (snapshot.has_origin) {
        printf("  Origin: ");
        for (const auto &b : snapshot.origin) {
            printf("%02X ", b);
        }
        printf("\n");
    } else {
        printf("  Origin: <none>\n");
    }

    if (snapshot.has_status) {
        printf("  Status: ");
        for (const auto &b : snapshot.status) {
            printf("%02X ", b);
        }
        printf("\n");
    } else {
        printf("  Status: <none>\n");
    }

    if (snapshot.has_recalibrate) {
        printf("  Recalibrate: ");
        for (const auto &b : snapshot.recalibrate) {
            printf("%02X ", b);
        }
        printf("\n");
    } else {
        printf("  Recalibrate: <none>\n");
    }

    if (snapshot.has_reset) {
        printf("  Reset: ");
        for (const auto &b : snapshot.reset) {
            printf("%02X ", b);
        }
        printf("\n");
    } else {
        printf("  Reset: <none>\n");
    }
}

std::size_t __time_critical_func(to_pad_callback)(void *user, const uint8_t *rx, std::size_t rx_len,
                                                  uint8_t *tx, std::size_t tx_max) {
    auto command = static_cast<ConvertGcInput::Joybus::Command>(
        g_expected_command.load(std::memory_order_relaxed));
    g_shared_pad.on_response_isr(command, std::span<const uint8_t>(rx, rx_len));
    return 0;
}

std::size_t __time_critical_func(to_console_callback)(void *user, const uint8_t *rx,
                                                      std::size_t rx_len, uint8_t *tx,
                                                      std::size_t tx_max) {
    if (rx_len < 1) {
        return 0;
    }

    // 最新のPollModeとRumbleModeを記録
    g_shared_console.on_request_isr(std::span<const uint8_t>(rx, rx_len));

    // Padの状態が揃うまでConsoleへは応答しない
    if (!g_console_ready.load(std::memory_order_relaxed)) {
        return 0;
    }

    const auto cmd = static_cast<jb::Command>(rx[0]);
    const auto snapshot = g_shared_pad.load();

    auto copy = [&](auto const &arr) -> std::size_t {
        if (tx_max < arr.size()) {
            return 0;
        }
        std::copy_n(arr.data(), arr.size(), tx);
        return arr.size();
    };

    switch (cmd) {
    case jb::Command::Status:
        if (!snapshot.has_status) {
            return 0;
        }
        return copy(snapshot.status);
    case jb::Command::Id:
        if (!snapshot.has_id) {
            return 0;
        }
        return copy(snapshot.id);
    case jb::Command::Origin:
        if (!snapshot.has_origin) {
            return 0;
        }
        return copy(snapshot.origin);
    case jb::Command::Recalibrate:
        if (!snapshot.has_recalibrate) {
            return 0;
        }
        return copy(snapshot.recalibrate);
    case jb::Command::Reset:
        // ResetはID相当で返す。reset応答が取れていればそれ、なければID応答を返す
        if (!snapshot.has_id && !snapshot.has_reset) {
            return 0;
        }
        if (snapshot.has_reset) {
            return copy(snapshot.reset);
        }
        return copy(snapshot.id);
    default:
        return 0;
    }
}

template <std::size_t N>
static inline bool send_request_to_pad(ConvertGcInput::JoybusPioSm &device, const char *name,
                                       const jb::Request<N> &request, bool dump_tx = true) {
    const auto bytes = request.bytes();
    if (dump_tx) {
        printf("[%s] TX (%zu):", name, bytes.size());
        for (const auto &b : bytes) {
            printf(" %02X", b);
        }
        printf("\n");
    }
    g_expected_command.store(static_cast<uint8_t>(request.command()), std::memory_order_relaxed);
    bool ok = device.send_now(bytes.data(), bytes.size());
    if (!ok) {
        printf("[%s] TX dropped (busy)\n", name);
    }
    return ok;
}

template <std::size_t N, class Pred>
static bool send_request_to_pad_and_wait(ConvertGcInput::JoybusPioSm &host_to_pad, const char *name,
                                         const jb::Request<N> &request, Pred pred,
                                         uint32_t timeout_us) {
    const auto start = time_us_32();
    const auto before_publish_count = g_shared_pad.load().publish_count;
    send_request_to_pad(host_to_pad, name, request, false);
    while ((uint32_t)(time_us_32() - start) < timeout_us) {
        const auto snapshot = g_shared_pad.load();
        if (snapshot.publish_count != before_publish_count && pred(snapshot)) {
            return true;
        }
        sleep_us(200);
    }
    return false;
}

static inline bool pad_bootstrap(ConvertGcInput::JoybusPioSm &host_to_pad) {
    // Consoleへの応答をいったん止める
    g_console_ready.store(false, std::memory_order_relaxed);

    if (!send_request_to_pad_and_wait(
            host_to_pad, "Host->Pad", jb::Id, [](const auto &snapshot) { return snapshot.has_id; },
            30'000)) {
        return false;
    }

    if (!send_request_to_pad_and_wait(
            host_to_pad, "Host->Pad", jb::Origin,
            [](const auto &snapshot) { return snapshot.has_origin; }, 30'000)) {
        return false;
    }

    if (!send_request_to_pad_and_wait(
            host_to_pad, "Host->Pad", jb::Recalibrate,
            [](const auto &snapshot) { return snapshot.has_recalibrate; }, 30'000)) {
        return false;
    }

    // ConsoleのStatusに即応できるよう1回とっておく
    if (!send_request_to_pad_and_wait(
            host_to_pad, "Host->Pad", jb::Status(),
            [](const auto &snapshot) { return snapshot.has_status; }, 30'000)) {
        return false;
    }

    g_console_ready.store(true, std::memory_order_relaxed);
    return true;
}

static inline bool pad_reinit_on_console_reset(ConvertGcInput::JoybusPioSm &host_to_pad) {
    g_console_ready.store(false, std::memory_order_relaxed);
    if (!send_request_to_pad_and_wait(
            host_to_pad, "Host->Pad", jb::Reset,
            [](const auto &snapshot) { return snapshot.has_reset || snapshot.has_id; }, 30'000)) {
        return false;
    }
    return pad_bootstrap(host_to_pad);
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

    ConvertGcInput::JoybusPioSm::Config host_to_pad_config{
        .pio = console_pio,
        .state_machine = sm_console,
        .pin = PIN_TO_REAL_PAD,
        .program = &joybus_console_program,
        .get_default_config = &joybus_console_program_get_default_config,
        .rx_start_offset = joybus_console_offset_rx_start,
        .tx_start_offset = joybus_console_offset_tx_start,
        .pio_hz = 4'000'000,
        .irq_base = 0,
    };

    ConvertGcInput::JoybusPioSm::Config device_to_console_config{
        .pio = pad_pio,
        .state_machine = sm_pad,
        .pin = PIN_TO_REAL_CONSOLE,
        .program = &joybus_pad_program,
        .get_default_config = &joybus_pad_program_get_default_config,
        .rx_start_offset = joybus_pad_offset_rx_start,
        .tx_start_offset = joybus_pad_offset_tx_start,
        .pio_hz = 4'000'000,
        .irq_base = 0,
    };

    ConvertGcInput::JoybusPioSm host_to_pad(host_to_pad_config);
    ConvertGcInput::JoybusPioSm device_to_console(device_to_console_config);

    host_to_pad.set_callback(&to_pad_callback, nullptr);
    device_to_console.set_callback(&to_console_callback, nullptr);

    printf("JoybusPioSm ready.\n");
    printf("host_to_pad: PIO%d SM%u pin GP%u\n", pio_get_index(host_to_pad_config.pio),
           host_to_pad_config.state_machine, PIN_TO_REAL_PAD);
    printf("device_to_console: PIO%d SM%u pin GP%u\n", pio_get_index(device_to_console_config.pio),
           device_to_console_config.state_machine, PIN_TO_REAL_CONSOLE);

    uint32_t last_pad_publish_count = g_shared_pad.load().publish_count;
    uint32_t last_pad_seen_us = time_us_32();
    bool last_pad_alive = false;
    constexpr uint32_t PAD_TIMEOUT_US = 100'000; // 100ms

    while (true) {
        const auto pad_snapshot = g_shared_pad.load();
        // Padからの応答があったなら最後に見た姿を記録
        if (pad_snapshot.publish_count != last_pad_publish_count) {
            last_pad_publish_count = pad_snapshot.publish_count;
            last_pad_seen_us = time_us_32();
            last_pad_alive = true;
        }

        // 一定時間Padから応答がなければ切断とみなして接続確立からやり直す
        const bool pad_alive = (time_us_32() - last_pad_seen_us) < PAD_TIMEOUT_US;
        if (!pad_alive) {
            if (last_pad_alive) {
                printf("Pad connection lost.\n");
            }
            last_pad_alive = false;
            g_console_ready.store(false, std::memory_order_relaxed);
        }

        const auto console_state = g_shared_console.load();
        // コントローラとの接続が確立していないならID, Origin, Recalibrateを投げる
        if (!g_console_ready.load(std::memory_order_relaxed)) {
            if (pad_bootstrap(host_to_pad)) {
                printf("Pad bootstrap succeeded.\n");
                dump_pad_snapshot(g_shared_pad.load());
            }
            prev_reset_count = console_state.reset_count;
            sleep_ms(10);
            continue;
        }

        // ConsoleからResetが来ていたらPadにもResetを投げて再初期化
        if (console_state.reset_count != prev_reset_count) {
            prev_reset_count = console_state.reset_count;
            if (pad_reinit_on_console_reset(host_to_pad)) {
                printf("Pad re-initialization after Console Reset succeeded.\n");
                dump_pad_snapshot(g_shared_pad.load());
            }
            sleep_ms(10);
            continue;
        }

        // PicoからPadへのStatusには本体からのPollMode/RumbleModeを反映
        send_request_to_pad(host_to_pad, "Host->Pad",
                            jb::Status(console_state.poll_mode, console_state.rumble_mode), false);
        sleep_us(500);
    }
}
