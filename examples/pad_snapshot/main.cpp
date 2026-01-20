#include "hardware/pio.h"
#include "hardware/sync.h"
#include "joybus_console.pio.h"
#include "joybus_pad.pio.h"
#include "joybus_pio_sm.hpp"
#include "joybus_protocol.hpp"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
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
static std::atomic<uint8_t> g_expected_command{
    static_cast<uint8_t>(ConvertGcInput::Joybus::Command::Id)};

static std::array<uint8_t, jb::kStatusResponseSize> last_status{};
static bool last_valid = false;

void dump_status_if_changed() {
    const auto snapshot = g_shared_pad.load();
    if (!snapshot.has_status) {
        return;
    }
    if (last_valid &&
        std::equal(snapshot.status.begin(), snapshot.status.end(), last_status.begin())) {
        return;
    }
    last_status = snapshot.status;
    last_valid = true;
    printf("Status:");
    for (const auto &b : snapshot.status) {
        printf(" %02X", b);
    }
    printf("\n");
}

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

static std::atomic<uint32_t> g_seen_mask{0};
enum : uint32_t {
    SEEN_ID = 1u << 0,
    SEEN_ORG = 1u << 1,
    SEEN_STA = 1u << 2,
    SEEN_REC = 1u << 3,
    SEEN_RST = 1u << 4,
};

std::size_t __time_critical_func(to_console_callback)(void *user, const uint8_t *rx,
                                                      std::size_t rx_len, uint8_t *tx,
                                                      std::size_t tx_max) {
    if (rx_len < 1) {
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
        g_seen_mask.fetch_or(SEEN_STA, std::memory_order_relaxed);
        return copy(snapshot.status);
    case jb::Command::Id:
        if (!snapshot.has_id) {
            return 0;
        }
        g_seen_mask.fetch_or(SEEN_ID, std::memory_order_relaxed);
        return copy(snapshot.id);
    case jb::Command::Origin:
        if (!snapshot.has_origin) {
            return 0;
        }
        g_seen_mask.fetch_or(SEEN_ORG, std::memory_order_relaxed);
        return copy(snapshot.origin);
    case jb::Command::Recalibrate:
        if (!snapshot.has_recalibrate) {
            return 0;
        }
        g_seen_mask.fetch_or(SEEN_REC, std::memory_order_relaxed);
        return copy(snapshot.recalibrate);
    case jb::Command::Reset:
        if (!snapshot.has_reset) {
            return 0;
        }
        g_seen_mask.fetch_or(SEEN_RST, std::memory_order_relaxed);
        return copy(snapshot.reset);
    default:
        return 0;
    }
}

template <std::size_t N>
static inline void send_request_to_pad(ConvertGcInput::JoybusPioSm &device, const char *name,
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

    send_request_to_pad(host_to_pad, "Host->Pad", jb::Id);
    sleep_us(200);
    dump_pad_snapshot(g_shared_pad.load());

    send_request_to_pad(host_to_pad, "Host->Pad", jb::Origin);
    sleep_us(500);
    dump_pad_snapshot(g_shared_pad.load());

    send_request_to_pad(host_to_pad, "Host->Pad", jb::Reset);
    sleep_us(500);
    dump_pad_snapshot(g_shared_pad.load());

    send_request_to_pad(host_to_pad, "Host->Pad", jb::Recalibrate);
    sleep_us(500);
    dump_pad_snapshot(g_shared_pad.load());

    uint64_t print_interval_us = 1'000'000;
    uint64_t next_print_time = time_us_64() + print_interval_us;
    while (true) {
        send_request_to_pad(host_to_pad, "Host->Pad",
                            jb::Status(jb::PollMode::Default, jb::RumbleMode::Off), false);
        sleep_us(500);
        if (time_us_64() >= next_print_time) {
            printf("Pad snapshot status - ID:%d ORG:%d STA:%d REC:%d RST:%d\n",
                   (g_seen_mask.load(std::memory_order_relaxed) & SEEN_ID) ? 1 : 0,
                   (g_seen_mask.load(std::memory_order_relaxed) & SEEN_ORG) ? 1 : 0,
                   (g_seen_mask.load(std::memory_order_relaxed) & SEEN_STA) ? 1 : 0,
                   (g_seen_mask.load(std::memory_order_relaxed) & SEEN_REC) ? 1 : 0,
                   (g_seen_mask.load(std::memory_order_relaxed) & SEEN_RST) ? 1 : 0);
            next_print_time += print_interval_us;
        }
    }
}
