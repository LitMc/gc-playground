#include "hardware/pio.h"
#include "hardware/sync.h"
#include "joybus_console.pio.h"
#include "joybus_pad.pio.h"
#include "joybus_pio_sm.hpp"
#include "joybus_protocol.hpp"
#include "pad_client.hpp"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "shared_console.hpp"
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

static ConvertGcInput::SharedConsole g_shared_console{};

std::size_t __time_critical_func(to_console_callback)(void *user, const uint8_t *rx,
                                                      std::size_t rx_len, uint8_t *tx,
                                                      std::size_t tx_max, uint32_t context) {
    if (rx_len < 1) {
        return 0;
    }

    // 最新のPollModeとRumbleModeを記録
    g_shared_console.on_request_isr(std::span<const uint8_t>(rx, rx_len));

    // 最新のパッド状態を取得
    auto *pad_client = static_cast<ConvertGcInput::PadClient *>(user);
    const bool status_ready = pad_client->console_ready();
    const auto snapshot = pad_client->snapshot();

    // Padの状態が揃うまでConsoleへは応答しない
    if (!status_ready) {
        return 0;
    }

    const auto cmd = static_cast<jb::Command>(rx[0]);

    // 本体からResetが来たらパッドへResetを指示
    if (cmd == jb::Command::Reset) {
        pad_client->on_console_reset();
    }

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
        if (snapshot.has_reset) {
            return copy(snapshot.reset);
        }
        if (!snapshot.has_id) {
            return copy(snapshot.id);
        }
        return 0;
    default:
        return 0;
    }
}

int main() {
    stdio_init_all();

    // ボタンを押すだけでBOOTSELに入るようにする
    bootsel_button_init();

    // 動作開始の確認用にオンボードLEDを光らせる
    init_led();

    // コンソールとパッドそれぞれのステートマシンを確保
    PIO host_to_pad_pio = pio0;
    PIO device_to_console_pio = pio1;
    const uint sm_console = pio_claim_unused_sm(host_to_pad_pio, true);
    const uint sm_pad = pio_claim_unused_sm(device_to_console_pio, true);

    ConvertGcInput::JoybusPioSm::Config host_to_pad_config{
        .pio = host_to_pad_pio,
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
        .pio = device_to_console_pio,
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

    ConvertGcInput::PadClient pad_client{host_to_pad};
    host_to_pad.set_callback(&ConvertGcInput::PadClient::callback, &pad_client);
    device_to_console.set_callback(&to_console_callback, &pad_client);

    printf("JoybusPioSm ready.\n");
    printf("host_to_pad: PIO%d SM%u pin GP%u\n", pio_get_index(host_to_pad_config.pio),
           host_to_pad_config.state_machine, PIN_TO_REAL_PAD);
    printf("device_to_console: PIO%d SM%u pin GP%u\n", pio_get_index(device_to_console_config.pio),
           device_to_console_config.state_machine, PIN_TO_REAL_CONSOLE);

    bool is_pad_connected = false;
    while (true) {
        pad_client.tick(time_us_32(), g_shared_console.load());
        const bool ready = pad_client.console_ready();
        if (!is_pad_connected && ready) {
            printf("PadClient: console responses enabled.\n");
            is_pad_connected = true;
        } else if (is_pad_connected && !ready) {
            printf("PadClient: console responses disabled.\n");
            is_pad_connected = false;
        }
        tight_loop_contents();
    }
}
