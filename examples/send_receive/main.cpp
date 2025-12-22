#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "joy_rx4.pio.h"
#include "joy_tx4.pio.h"
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

int main() {
    stdio_init_all();

    // 動作開始の確認用にオンボードLEDを光らせる
    init_led();

    init_bus_pins_safe();

    // PIO0を使用
    PIO pio = pio0;
    uint sm_tx = 0;
    uint sm_rx = 1;

    // PIOプログラムをロード
    uint off_tx = pio_add_program(pio, &joy_tx4_program);
    uint off_rx = pio_add_program(pio, &joy_rx4_program);

    // --- TXステートマシン設定 ---
    pio_sm_config c_tx = joy_tx4_program_get_default_config(off_tx);
    // TXはSETとPINDIRSでラインを制御するので、ベースピンをTX_PINに設定
    sm_config_set_set_pins(&c_tx, TX_PIN, 1);
    sm_config_set_out_shift(&c_tx, /*shift_right=*/false, /*autopull=*/false, /*pull_thresh=*/32);

    // --- RXステートマシン設定 ---
    pio_sm_config c_rx = joy_rx4_program_get_default_config(off_rx);
    // RXはRX_PINからサンプリング
    sm_config_set_in_pins(&c_rx, RX_PIN);
    sm_config_set_in_shift(&c_rx, /*shift_right=*/false, /*autopush=*/true, /*push_thresh=*/8);

    // クロック分周設定
    const float pio_hz = 4'000'000; // 4MHz
    float div = (float)clock_get_hz(clk_sys) / pio_hz;
    sm_config_set_clkdiv(&c_tx, div);
    sm_config_set_clkdiv(&c_rx, div);

    // ステートマシン初期化
    pio_gpio_init(pio, TX_PIN);
    pio_gpio_init(pio, RX_PIN);
    // TXを開放状態に設定
    pio_sm_set_consecutive_pindirs(pio, sm_tx, TX_PIN, 1, false);
    pio_sm_set_pins_with_mask(pio, sm_tx, 0u, 1u << TX_PIN);

    // RXを入力に設定
    pio_sm_set_consecutive_pindirs(pio, sm_rx, RX_PIN, 1, false);

    // ステートマシン起動
    pio_sm_init(pio, sm_tx, off_tx, &c_tx);
    pio_sm_init(pio, sm_rx, off_rx, &c_rx);

    // RXステートマシンを先に起動
    pio_sm_set_enabled(pio, sm_rx, true);
    pio_sm_set_enabled(pio, sm_tx, true);

    printf("Loopback test ready.\n");

    const uint8_t test_data[] = {0x00, 0xFF, 0x55, 0xAA, 0xA5, 0x3C, 0xC3};

    while (true) {
        for (uint8_t tx_byte : test_data) {
            // MSB-firstで送信するために上位8ビットに配置して送信
            pio_sm_put_blocking(pio, sm_tx, ((uint32_t)tx_byte) << 24);

            absolute_time_t start = get_absolute_time();
            while (pio_sm_is_rx_fifo_empty(pio, sm_rx)) {
                // タイムアウト処理（100ms）
                if (absolute_time_diff_us(start, get_absolute_time()) > 200000) {
                    printf("RX timeout, pin=%d\n", gpio_get(RX_PIN));
                    break;
                }
            }
            if (!pio_sm_is_rx_fifo_empty(pio, sm_rx)) {
                // 受信
                uint32_t rx_word = pio_sm_get_blocking(pio, sm_rx);
                uint8_t rx_byte = (uint8_t)(rx_word & 0xFF);
                // 結果表示
                printf("TX: %02X -> RX: %02X\n", tx_byte, rx_byte);
            }
            sleep_ms(200);
        }
        sleep_ms(5000);
    }
}
