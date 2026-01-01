#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "joy_rx5.pio.h"
#include "joy_tx5.pio.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <vector>

// TX FIFOに積める最大バイト数
static constexpr size_t DEFAULT_MAX_FIFO_BYTES = 4 * 8;

// BOOTSELに入るためのボタン入力
static constexpr uint BOOT_BTN_PIN = 26; // GP26
static constexpr uint DEBOUNCE_MS = 300;

constexpr uint ONBOARD_LED_PIN = PICO_DEFAULT_LED_PIN;
constexpr uint TX_PIN = 15; // GP15
constexpr uint RX_PIN = 16; // GP16

static void boot_btn_irq(uint gpio, uint32_t events) {
    // ちょいデバウンス（押しっぱなし連打対策）
    busy_wait_ms(100);
    if (gpio_get(BOOT_BTN_PIN) == 0) {
        printf("BOOTSEL button pressed. Entering USB boot mode...\n");
        reset_usb_boot(0, 0);
    }
}

static void bootsel_button_init() {
    gpio_init(BOOT_BTN_PIN);
    gpio_set_dir(BOOT_BTN_PIN, GPIO_IN);
    gpio_pull_up(BOOT_BTN_PIN);
    gpio_set_irq_enabled_with_callback(BOOT_BTN_PIN, GPIO_IRQ_EDGE_FALL, true, &boot_btn_irq);
}

static void init_bus_pins_safe() {
    // バスへ接続するピンをHi-Zに設定
    gpio_init(TX_PIN);
    gpio_put(TX_PIN, 0);
    gpio_set_dir(TX_PIN, GPIO_IN);

    gpio_init(RX_PIN);
    gpio_set_dir(RX_PIN, GPIO_IN);
}

static void init_led() {
    gpio_init(ONBOARD_LED_PIN);
    gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);
    gpio_put(ONBOARD_LED_PIN, 1);
}

static inline uint8_t decode_3sample_msbfirst(uint32_t w) {
    // wは24ビットのサンプルデータ
    // あるbitの受信結果について3つのサンプルが8組並んでいる
    // 00000000 | s0, s1, s2 | s0, s1, s2 | ... | s0, s1, s2
    // 最も古いサンプルがビット23、最新がビット0

    uint8_t out = 0;
    // 上位iビット目の3サンプルを抽出し多数決をとる
    for (int i = 0; i < 8; ++i) {
        int base = 23 - 3 * i;
        uint32_t s0 = (w >> base) & 1u;
        uint32_t s1 = (w >> (base - 1)) & 1u;
        uint32_t s2 = (w >> (base - 2)) & 1u;
        uint32_t majority = (s0 & s1) | (s1 & s2) | (s2 & s0);
        out = (uint8_t)((out << 1) | (majority & 1u));
    }
    return out;
}

static bool joybus_rx_read_bytes(PIO pio, uint sm, uint8_t *out, size_t nbytes, int timeout_us) {
    absolute_time_t start = get_absolute_time();

    for (size_t i = 0; i < nbytes; ++i) {
        while (pio_sm_is_rx_fifo_empty(pio, sm)) {
            if (absolute_time_diff_us(start, get_absolute_time()) > timeout_us) {
                return false;
            }
            tight_loop_contents();
        }
        uint32_t raw = pio_sm_get_blocking(pio, sm);
        uint32_t lo24 = raw & 0x00FFFFFFu;
        out[i] = decode_3sample_msbfirst(lo24);
    }
    return true;
}

static void joybus_tx_send(PIO pio, uint sm, const uint8_t *data, size_t nbytes) {
    if (nbytes == 0) {
        return;
    }
    // TX FIFOに積みきれるバイト数の上限を超えたらエラー
    // JoyBusは最大10バイト程度なので超えないはず
    if (nbytes > DEFAULT_MAX_FIFO_BYTES) {
        printf("Error: joybus_tx_send: nbytes=%zu exceeds max=%zu\n", nbytes,
               DEFAULT_MAX_FIFO_BYTES);
        return;
    }

    printf("Starting to send ");
    for (size_t i = 0; i < nbytes; ++i) {
        printf("0x%02X ", data[i]);
    }
    printf("(%zu bytes)\n", nbytes);

    printf("Waiting for previous TX complete...\n");
    // 万が一同期が崩れてもautopullされないように前の送信が完了しないうちはFIFOに積まない
    while (!pio_interrupt_get(pio, 1)) {
        // 送信が完了するのを待つ
        tight_loop_contents();
    }
    printf("Previous TX complete.\n");
    pio_interrupt_clear(pio, 1);

    // 期待する送信ビット数-1
    uint32_t bits_to_send_minus1 = (uint32_t)(nbytes * 8 - 1);
    pio_sm_put_blocking(pio, sm, bits_to_send_minus1);
    // PIOには1ワードずつ渡す
    // 1ワードは32ビット(4バイト)なので、4バイトずつ処理
    // 送るデータをMSB-firstにするため、先頭のデータを上位バイトに詰める
    // b0, b1, b2, b3 -> word = b0<<24 | b1<<16 | b2<<8 | b3
    // 不足分は0埋め
    const size_t words_to_send = (nbytes + 3) / 4;
    printf("Sending %zu words to TX FIFO...\n", words_to_send);
    for (size_t w = 0; w < words_to_send; ++w) {
        uint32_t word = 0;
        for (size_t b = 0; b < 4; ++b) {
            size_t byte_index = w * 4 + b;
            uint8_t byte = (byte_index < nbytes) ? data[byte_index] : 0;
            word |= ((uint32_t)byte) << (8 * (3 - b));
            printf("  Added byte 0x%02X to word %08lX\n", byte, (unsigned long)word);
        }
        pio_sm_put_blocking(pio, sm, word); // TODO: あとでもどす
        printf("  Sent word %zu: 0x%08lX\n", w, (unsigned long)word);
    }
    // IRQ0を1にして送信開始を通知
    pio->irq_force = (1u << 0);
    printf("TX start notified.\n");
}

int main() {
    stdio_init_all();
    bootsel_button_init();

    // 動作開始の確認用にオンボードLEDを光らせる
    init_led();

    init_bus_pins_safe();

    PIO pio_tx = pio0;
    uint sm_tx = 0;
    PIO pio_rx = pio1;
    uint sm_rx = 0;

    // PIOプログラムをロード
    printf("Loading PIO programs...\n");
    printf("tx len=%u origin=%d\n", joy_tx5_program.length, joy_tx5_program.origin);
    printf("rx len=%u origin=%d\n", joy_rx5_program.length, joy_rx5_program.origin);
    printf("can add tx=%d rx=%d\n", pio_can_add_program(pio0, &joy_tx5_program),
           pio_can_add_program(pio0, &joy_rx5_program));

    uint off_tx = pio_add_program(pio_tx, &joy_tx5_program);
    uint off_rx = pio_add_program(pio_rx, &joy_rx5_program);
    printf("Added PIO programs at off_tx=%u off_rx=%u\n", off_tx, off_rx);

    // --- TXステートマシン設定 ---
    pio_sm_config c_tx = joy_tx5_program_get_default_config(off_tx);
    // TXはSETとPINDIRSでラインを制御するので、ベースピンをTX_PINに設定
    sm_config_set_set_pins(&c_tx, TX_PIN, 1);
    // CPUからは32ビット単位で渡すが実際には上位8ビットのみ使用
    // 何バイト送るかを動的に決めるためTXのPIOは1バイトずつ勝手にpullして送信する
    sm_config_set_out_shift(&c_tx,
                            /*shift_right=*/false,
                            /*autopull=*/true,
                            /*pull_thresh=*/32);

    // --- RXステートマシン設定 ---
    pio_sm_config c_rx = joy_rx5_program_get_default_config(off_rx);
    // RXはRX_PINからサンプリング
    sm_config_set_in_pins(&c_rx, RX_PIN);
    // 3点でサンプリングするため3 * 8 = 24ビットずつ受信
    sm_config_set_in_shift(&c_rx,
                           /*shift_right=*/false,
                           /*autopush=*/true,
                           /*push_thresh=*/24);
    sm_config_set_jmp_pin(&c_rx, RX_PIN);

    // クロック分周設定
    const float pio_hz = 4'000'000; // 4MHz
    float div = (float)clock_get_hz(clk_sys) / pio_hz;
    sm_config_set_clkdiv(&c_tx, div);
    sm_config_set_clkdiv(&c_rx, div);

    // ステートマシン初期化
    pio_gpio_init(pio_tx, TX_PIN);
    pio_gpio_init(pio_rx, RX_PIN);
    gpio_pull_up(TX_PIN); // open-drainのHigh維持の補助（外付けがあるなら無くてもOK）
    gpio_pull_up(RX_PIN); // 必須寄り
    // TXを開放状態に設定
    pio_sm_set_consecutive_pindirs(pio_tx, sm_tx, TX_PIN, 1, false);
    pio_sm_set_pins_with_mask(pio_tx, sm_tx, 0u, 1u << TX_PIN);
    // RXを入力に設定
    pio_sm_set_consecutive_pindirs(pio_rx, sm_rx, RX_PIN, 1, false);

    // ステートマシン初期化
    pio_sm_init(pio_tx, sm_tx, off_tx, &c_tx);
    pio_sm_init(pio_rx, sm_rx, off_rx, &c_rx);

    // RXステートマシンを先に起動
    pio_sm_set_enabled(pio_rx, sm_rx, true);
    sleep_ms(200);                           // 安全のため少し待つ
    pio_sm_set_enabled(pio_tx, sm_tx, true); // RXが受信待ち状態になってからTXを起動
    printf("Loopback test ready.\n");

    const std::vector<std::vector<uint8_t>> test_frames = {
        {0xA5},
        {0xFF},
        {0x00},
        {0xA5, 0x5A},                   // 2バイト
        {0x78, 0x56, 0x34, 0x12},       // 4バイト
        {0x12, 0x34, 0x56, 0x78},       // 4バイト
        {0x89, 0xAB, 0xCD, 0xEF},       // 4バイト
        {0x12, 0x34, 0x56, 0x78, 0x9A}, // 5バイト（RX FIFOがあふれる！）
    };

    while (true) {
        for (const auto &frame : test_frames) {
            const uint32_t expected_bytes = (uint32_t)frame.size();
            if (expected_bytes == 0) {
                continue;
            }
            const uint32_t bits_to_receive_minus1 = expected_bytes * 8 - 1;
            pio_sm_put_blocking(pio_rx, sm_rx, bits_to_receive_minus1);
            joybus_tx_send(pio_tx, sm_tx, frame.data(), expected_bytes);
            std::vector<uint8_t> rx_buffer(expected_bytes, 0);
            if (!joybus_rx_read_bytes(pio_rx, sm_rx, rx_buffer.data(), expected_bytes, 200000)) {
                printf("RX timeout (expected %lu bytes)\n", (unsigned long)expected_bytes);
                continue;
            }
            printf("TX(%lu bytes): ", (unsigned long)expected_bytes);
            for (size_t i = 0; i < expected_bytes; ++i) {
                printf(" 0x%02X ", frame[i]);
            }
            printf(" => RX(%lu bytes): ", (unsigned long)expected_bytes);
            for (size_t i = 0; i < expected_bytes; ++i) {
                printf(" 0x%02X ", rx_buffer[i]);
            }
            printf("\n");
        }
        sleep_ms(5000);
    }
}
