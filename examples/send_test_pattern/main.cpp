#include "console_client.hpp"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "joybus_console.pio.h"
#include "joybus_pad.pio.h"
#include "joybus_pio_sm.hpp"
#include "pad_client.hpp"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "test_suites.hpp"
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

void print_status(const ConvertGcInput::PadSnapshot &snapshot, const char *descriptions[]) {
    if (snapshot.last_rx_command != ConvertGcInput::Joybus::Command::Status) {
        return;
    }
    for (size_t i = 0; i < ConvertGcInput::Joybus::kStatusResponseSize; ++i) {
        const auto value = snapshot.status[i];
        printf("%s=0x%02X(%3d) ", descriptions[i], value, value);
    }
    printf("\n");
}

namespace jb = ConvertGcInput::Joybus;

ConvertGcInput::Test::Status test_status_pattern{
    0x00, // byte1
    0x80, // byte2
    0x80, // Joystick X
    0x80, // Joystick Y
    0x80, // C-Stick X
    0x80, // C-Stick Y
    0x00, // L Analog
    0x00, // R Analog
};

ConvertGcInput::Test::Origin test_origin_pattern{
    0x00, // byte1
    0x80, // byte2
    0x80, // Joystick X
    0x80, // Joystick Y
    0x80, // C-Stick X
    0x80, // C-Stick Y
    0x00, // L Analog
    0x00, // R Analog
    0x00, // A Analog
    0x00, // B Analog
};

ConvertGcInput::Test::Recalibrate test_recalibrate_pattern{
    0x00, // byte1
    0x80, // byte2
    0x80, // Joystick X
    0x80, // Joystick Y
    0x80, // C-Stick X
    0x80, // C-Stick Y
    0x00, // L Analog
    0x00, // R Analog
};

std::vector<ConvertGcInput::Test::Status>
generate_analog_test_pattern(std::size_t byte_index, uint8_t from, uint8_t to, uint8_t step) {
    // アナログ入力じゃない
    if (byte_index < 2 || byte_index > 7) {
        return {};
    }
    std::vector<ConvertGcInput::Test::Status> patterns;
    for (uint16_t x = from; x <= to; x += step) {
        if (x > 0xFF) {
            break;
        }
        ConvertGcInput::Test::Status pattern{test_status_pattern};
        pattern[byte_index] = static_cast<uint8_t>(x);
        patterns.push_back(pattern);
    }
    return patterns;
}

const auto stick_x_patterns = generate_analog_test_pattern(2, 0x00, 0xFF, 1);
const auto stick_y_patterns = generate_analog_test_pattern(3, 0x00, 0xFF, 1);

ConvertGcInput::Test::Status
get_next_pattern(const std::vector<ConvertGcInput::Test::Status> &patterns, std::size_t &index) {
    if (patterns.empty()) {
        return test_status_pattern;
    }
    if (index >= patterns.size()) {
        index = 0;
    }
    return patterns[index++];
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
    client_link.test().fix_origin_and_recalibrate_from_main(test_origin_pattern,
                                                            test_recalibrate_pattern);
    uint32_t last_origin_epoch = client_link.load_origin_epoch();
    uint32_t last_recalibrate_epoch = client_link.load_recalibrate_epoch();

    ConvertGcInput::PadClient pad_client(host_to_pad_config, client_link);
    ConvertGcInput::ConsoleClient console_client(device_to_console_config, client_link);

    printf("JoybusPioSm ready.\n");
    printf("host_to_pad: PIO%d SM%u pin GP%u\n", pio_get_index(host_to_pad_config.pio),
           host_to_pad_config.state_machine, PIN_TO_REAL_PAD);
    printf("device_to_console: PIO%d SM%u pin GP%u\n", pio_get_index(device_to_console_config.pio),
           device_to_console_config.state_machine, PIN_TO_REAL_CONSOLE);

    const char *descriptions[] = {
        "byte1",     "byte2",     "Joystick X", "Joystick Y",
        "C-Stick X", "C-Stick Y", "L Analog",   "R Analog",
    };

    bool is_pad_connected = false;
    bool last_on = false;
    std::size_t test_pattern_index = 0;
    while (true) {
        if (client_link.consume_sent_origin(last_origin_epoch)) {
            const auto sent_origin = client_link.shared_console().load().last_send_origin;
            printf("Sent Origin: ");
            for (const auto b : sent_origin) {
                printf("%02X ", b);
            }
            printf("\n");
        }
        if (client_link.consume_sent_recalibrate(last_recalibrate_epoch)) {
            const auto sent_recalibrate = client_link.shared_console().load().last_send_recalibrate;
            printf("Sent Recalibrate: ");
            for (const auto b : sent_recalibrate) {
                printf("%02X ", b);
            }
            printf("\n");
        }
        const auto pad_snapshot = client_link.shared_pad().load();
        if (pad_snapshot.last_rx_command == ConvertGcInput::Joybus::Command::Status) {
            // print_status(pad_snapshot, descriptions);
            const uint8_t on_mask = 0x10u;                             // Zボタン
            const uint8_t off_mask = 0x08u;                            // 十字キー上
            const bool on = (pad_snapshot.status[1] & on_mask) != 0;   // 開始
            const bool off = (pad_snapshot.status[1] & off_mask) != 0; // 終了
            if (on) {
                if (!client_link.test().enabled()) {
                    printf("Test pattern mode enabled.\n");
                    client_link.test().begin_from_main(
                        test_origin_pattern, test_recalibrate_pattern, test_status_pattern);
                    test_pattern_index = 0;
                }
                if (!last_on) {
                    last_on = on;
                    // テストパターン取得
                    auto status = get_next_pattern(stick_x_patterns, test_pattern_index);
                    client_link.test().publish_status_from_main(status);
                    printf("Send stick X: %02X(%3d)\n", status[2], status[2]);
                }
            } else if (off) {
                if (client_link.test().enabled()) {
                    printf("Test pattern mode disabled.\n");
                    client_link.test().end_from_main();
                }
            }
            last_on = on;
        }
        pad_client.tick(time_us_32(), client_link.shared_console().load());
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
