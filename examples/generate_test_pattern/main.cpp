#include "console_client.hpp"
#include "core/state.hpp"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "joybus_console.pio.h"
#include "joybus_pad.pio.h"
#include "joybus_pio_sm.hpp"
#include "pad_client.hpp"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "shared_pad_hub.hpp"
#include "test_pad_client.hpp"
#include "transforms/presets.hpp"
#include <array>
#include <span>
#include <stdio.h>

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

struct TestPatternStorage {
    std::array<ConvertGcInput::JoybusReply, 256> buf{};
    size_t size = 0;
};

void create_test_pattern(ConvertGcInput::Test::TestInputFrames &pattern,
                         TestPatternStorage &storage) {
    storage.size = 0;

    pattern.loop = true;
    pattern.send_interval_frames_ = 0;

    const uint8_t y = 128;

    for (int x = 0; x < 256; ++x) {
        const uint8_t xu = static_cast<uint8_t>(x);

        std::array<uint8_t, ConvertGcInput::Joybus::kStatusResponseSize> payload{
            0x00, 0x80, xu, xu, 0x80, 0x80, 0x00, 0x00,
        };

        storage.buf[storage.size++] =
            ConvertGcInput::JoybusReply{ConvertGcInput::Joybus::Command::Status, payload};
    }

    pattern.frames = std::span<const ConvertGcInput::JoybusReply>(storage.buf.data(), storage.size);
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
    const uint sm_host_to_pad = pio_claim_unused_sm(host_to_pad_pio, true);
    const uint sm_device_to_host = pio_claim_unused_sm(device_to_console_pio, true);

    ConvertGcInput::JoybusPioSm::Config host_to_pad_config{
        .pio = host_to_pad_pio,
        .state_machine = sm_host_to_pad,
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
        .state_machine = sm_device_to_host,
        .pin = PIN_TO_REAL_CONSOLE,
        .program = &joybus_pad_program,
        .get_default_config = &joybus_pad_program_get_default_config,
        .rx_start_offset = joybus_pad_offset_rx_start,
        .tx_start_offset = joybus_pad_offset_tx_start,
        .pio_hz = 4'000'000,
        .irq_base = 0,
    };

    ConvertGcInput::PadConsoleLink client_link{};

    // テスト用に原点を中央に固定
    ConvertGcInput::Presets::install_fix_origin_and_recalibrate_to_center(
        client_link.transform_pipeline());

    ConvertGcInput::PadClient pad_client(host_to_pad_config, client_link);

    static TestPatternStorage storage;
    ConvertGcInput::Test::TestInputFrames pattern{};
    create_test_pattern(pattern, storage);
    ConvertGcInput::Test::TestPadClient test_pad_client(client_link, pattern);

    ConvertGcInput::ConsoleClient console_client(device_to_console_config, client_link);

    printf("JoybusPioSm ready.\n");
    printf("host_to_pad: PIO%d SM%u pin GP%u\n", pio_get_index(host_to_pad_config.pio),
           host_to_pad_config.state_machine, PIN_TO_REAL_PAD);
    printf("device_to_console: PIO%d SM%u pin GP%u\n", pio_get_index(device_to_console_config.pio),
           device_to_console_config.state_machine, PIN_TO_REAL_CONSOLE);

    bool is_pad_connected = false;

    uint32_t last_tx_publish_count = client_link.active_pad_hub().load_last_tx().publish_count;

    uint32_t last_test_epoch = client_link.load_test_epoch();

    while (true) {
        pad_client.tick(time_us_32(), client_link.shared_console().load());
        test_pad_client.tick(time_us_32(), client_link.shared_console().load());

        const auto real_pad_snapshot = client_link.real_pad_hub().load_original_snapshot();
        if (real_pad_snapshot.last_rx_command == ConvertGcInput::Joybus::Command::Status) {
            const bool test_enable =
                real_pad_snapshot.status.input.pressed(ConvertGcInput::core::PadButton::Z);
            const bool test_disable =
                real_pad_snapshot.status.input.pressed(ConvertGcInput::core::PadButton::DpadUp);

            if (test_enable && !client_link.is_test_enabled()) {
                client_link.enable_test_from_main();
            } else if (test_disable && client_link.is_test_enabled()) {
                client_link.disable_test_from_main();
            }
        }

        if (client_link.consume_test_epoch(last_test_epoch)) {
            last_tx_publish_count = client_link.active_pad_hub().load_last_tx().publish_count;
            printf("TestPadClient: test mode %s.\n",
                   client_link.is_test_enabled() ? "enabled" : "disabled");
        }

        ConvertGcInput::TxPair last_tx = client_link.active_pad_hub().load_last_tx();
        if (client_link.active_pad_hub().consume_tx_if_new(last_tx_publish_count, last_tx)) {
            last_tx_publish_count = last_tx.publish_count;
            const auto raw = last_tx.raw;
            const auto modified = last_tx.modified;

            if (raw.command() != modified.command()) {
                printf("Command is not matched: raw=%02X modified=%02X\n",
                       static_cast<uint8_t>(raw.command()),
                       static_cast<uint8_t>(modified.command()));
                continue;
            }

            const auto command = raw.command();

            if (command == ConvertGcInput::Joybus::Command::Origin ||
                command == ConvertGcInput::Joybus::Command::Recalibrate ||
                command == ConvertGcInput::Joybus::Command::Status) {
                const auto raw_input = raw.view();
                const auto modified_input = modified.view();
                printf("%s [0x%02X] (%3u,%3u) -> (%3u,%3u)\n",
                       client_link.is_test_enabled() ? "[TEST]" : "[REAL]",
                       static_cast<uint8_t>(command), raw_input[2], raw_input[3], modified_input[2],
                       modified_input[3]);
            }
        }

        const bool ready = client_link.is_pad_ready();
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
