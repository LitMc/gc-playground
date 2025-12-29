#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "joy_rx5.pio.h"
#include "joy_tx5.pio.h"
#include "pico/stdlib.h"
#include <stdio.h>

constexpr uint ONBOARD_LED_PIN = PICO_DEFAULT_LED_PIN;
constexpr uint TX_PIN = 16; // GP16
constexpr uint RX_PIN = 17; // GP17

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

static void joybus_tx_send(PIO pio, uint sm, const uint8_t *data, size_t n) {
    if (n == 0) {
        return;
    }

    // 期待する送信ビット数-1
    uint32_t bits_to_send_minus1 = (uint32_t)(n * 8 - 1);
    pio_sm_put_blocking(pio, sm, bits_to_send_minus1);
    // 送信データを1バイトずつ送信
    for (size_t i = 0; i < n; ++i) {
        pio_sm_put_blocking(pio, sm, ((uint32_t)data[i]) << 24); // MSB-firstなので上位8ビットに配置
    }
}

static inline void tx_send_frame(PIO pio, uint sm, uint off, const pio_sm_config *cfg,
                                 const uint8_t *data, size_t n) {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_restart(pio, sm);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_init(pio, sm, off, cfg); // PCも先頭へ戻る

    // ★このフレームの分だけ積む
    pio_sm_put_blocking(pio, sm, (uint32_t)(n * 8 - 1));
    for (size_t i = 0; i < n; ++i) {
        pio_sm_put_blocking(pio, sm, ((uint32_t)data[i]) << 24);
    }

    pio_sm_set_enabled(pio, sm, true);

    // 必要なら止めておく（次フレームも同様に送るならこのままでもOK）
    pio_sm_set_enabled(pio, sm, false);
}

int main() {
    stdio_init_all();

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
                            /*autopull=*/false,
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
    pio_sm_put_blocking(pio_tx, sm_tx, 0xAA << 24);

    printf("Loopback test ready.\n");

    const uint8_t test_data[] = {0x00, 0xFF, 0x55, 0xAA, 0xA5, 0x3C, 0xC3};

    while (true) {
        for (uint8_t tx_byte : test_data) {
            const uint32_t expected_rx_bytes = 1;
            const uint32_t expected_tx_bytes = 1;

            pio_sm_put_blocking(pio_rx, sm_rx, expected_rx_bytes - 1);
            // TXへ送信データを渡す

            // 受信データを取得
            absolute_time_t start_time = get_absolute_time();
            while (pio_sm_is_rx_fifo_empty(pio_rx, sm_rx)) {
                // タイムアウト
                if (absolute_time_diff_us(start_time, get_absolute_time()) > 200000) {
                    uint pc_tx_abs = pio_sm_get_pc(pio_tx, sm_tx);
                    uint pc_rx_abs = pio_sm_get_pc(pio_rx, sm_rx);
                    printf("off_tx=%u off_rx=%u pc_tx_abs=%u pc_rx_abs=%u RXpin=%d\n", off_tx,
                           off_rx, pc_tx_abs, pc_rx_abs, gpio_get(RX_PIN));
                    printf("RX timeout!\n");
                    break;
                }
            }

            if (!pio_sm_is_rx_fifo_empty(pio_rx, sm_rx)) {
                // uint32_t rx_sampled = pio_sm_get_blocking(pio_rx, sm_rx);
                // uint8_t rx_byte = decode_3sample_msbfirst(rx_sampled & 0x00FFFFFFu);
                // printf("TX: 0x%02X -> RX: 0x%02X\n", tx_byte, rx_byte);
                uint32_t raw = pio_sm_get_blocking(pio_rx, sm_rx);

                uint32_t lo24 = raw & 0x00FFFFFFu;
                uint32_t hi24 = (raw >> 8) & 0x00FFFFFFu;

                uint8_t d_lo = decode_3sample_msbfirst(lo24);
                uint8_t d_hi = decode_3sample_msbfirst(hi24);

                printf("TX:%02X raw:%08lX lo24:%06lX d_lo:%02X hi24:%06lX d_hi:%02X\n", tx_byte,
                       (unsigned long)raw, (unsigned long)lo24, d_lo, (unsigned long)hi24, d_hi);

            } else {
                printf("No data received.\n");
            }
        }
        sleep_ms(5000);
    }
}
