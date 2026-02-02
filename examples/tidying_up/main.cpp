#include "console_client.hpp"
#include "domain/state.hpp"
#include "domain/transform/builtins.hpp"
#include "domain/transform/pipeline.hpp"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "joybus_console.pio.h"
#include "joybus_pad.pio.h"
#include "joybus_pio_sm.hpp"
#include "measure/patterns/stick_grid_sweep.hpp"
#include "measure/test_pad_client.hpp"
#include "pad_client.hpp"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "shared_pad_hub.hpp"
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

    // 入力変換処理を差し込む
    auto &pipelines = client_link.transform_pipelines();
    const auto &fix_origin_to_neutral =
        ConvertGcInput::domain::transform::builtins::fix_origin_to_neutral;
    pipelines.origin.add_stage(
        ConvertGcInput::domain::transform::make_stage(&fix_origin_to_neutral));
    pipelines.recalibrate.add_stage(
        ConvertGcInput::domain::transform::make_stage(&fix_origin_to_neutral));
    pipelines.status.add_stage(
        ConvertGcInput::domain::transform::make_stage(&fix_origin_to_neutral));

    ConvertGcInput::PadClient pad_client(host_to_pad_config, client_link);

    // テストパターン送信の準備
    ConvertGcInput::measure::Schedule schedule{ConvertGcInput::measure::ScheduleConfig{
        .interval_us = 5'000'000,
        .catch_up = false,
    }};

    ConvertGcInput::measure::StickGridSweep pattern{ConvertGcInput::measure::StickGridSweep::Config{
        .x = {.begin = 0, .end = 240, .step = 16},
        .y = {.begin = 0, .end = 240, .step = 16},
        .loop = true,
        .target = ConvertGcInput::measure::StickGridSweep::Target::Joystick,
    }};

    ConvertGcInput::measure::TestPadClient test_pad_client(client_link, schedule, pattern);

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
        test_pad_client.tick(time_us_32());

        const auto real_pad_snapshot = client_link.real_pad_hub().load_original_snapshot();
        if (real_pad_snapshot.last_rx_command == ConvertGcInput::Joybus::Command::Status) {
            const bool test_enable =
                real_pad_snapshot.status.input.pressed(ConvertGcInput::domain::PadButton::Z);
            const bool test_disable =
                real_pad_snapshot.status.input.pressed(ConvertGcInput::domain::PadButton::DpadUp);

            if (test_enable && !client_link.is_test_enabled()) {
                client_link.enable_test_from_main();
            } else if (test_disable && client_link.is_test_enabled()) {
                client_link.disable_test_from_main();
            }
        }

        if (client_link.consume_test_epoch(last_test_epoch)) {
            last_tx_publish_count = client_link.active_pad_hub().load_last_tx().publish_count;
            printf("TestPadClient: measure mode %s.\n",
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
                command == ConvertGcInput::Joybus::Command::Recalibrate) {
                const auto raw_input = raw.view();
                const auto modified_input = modified.view();
                printf("%s %s [0x%02X]: ", client_link.is_test_enabled() ? "[TEST]" : "[REAL]",
                       "raw", static_cast<uint8_t>(command));
                for (size_t i = 0; i < raw_input.size(); ++i) {
                    printf("%02X ", raw_input[i]);
                }
                printf("\n");
                printf("%s %s [0x%02X]: ", client_link.is_test_enabled() ? "[TEST]" : "[REAL]",
                       "mod", static_cast<uint8_t>(command));
                for (size_t i = 0; i < modified_input.size(); ++i) {
                    printf("%02X ", modified_input[i]);
                }
                printf("\n");
            }

            if (command == ConvertGcInput::Joybus::Command::Status &&
                client_link.is_test_enabled()) {
                const auto status = modified.view();
                printf("(X, Y): (%3d, %3d)\n", (int)status[2], (int)status[3]);
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
