#include "button_duration.pio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>

constexpr uint ONBOARD_LED_PIN = PICO_DEFAULT_LED_PIN;
constexpr uint BTN_PIN = 2;  // GP2
constexpr uint LED_PIN = 16; // GP16

int main() {
    stdio_init_all();

    // ボタン入力ピンをプルアップ
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_pull_up(BTN_PIN);

    // PIO本体とステートマシン番号
    PIO pio = pio0;
    const uint sm = 0;

    // PIOプログラムをロード
    uint offset = pio_add_program(pio, &button_duration_program);
    // デフォルト設定を取得
    pio_sm_config c = button_duration_program_get_default_config(offset);

    // ステートマシンの操作するピンを指定
    sm_config_set_in_pins(&c, BTN_PIN);
    sm_config_set_out_pins(&c, LED_PIN, 1);
    sm_config_set_set_pins(&c, LED_PIN, 1);

    // ピンをPIO用に切り替える
    pio_gpio_init(pio, BTN_PIN);
    pio_gpio_init(pio, LED_PIN);
    // 方向を設定
    pio_sm_set_consecutive_pindirs(pio, sm, BTN_PIN, 1, false); // 入力
    pio_sm_set_consecutive_pindirs(pio, sm, LED_PIN, 1, true);  // 出力

    // クロック分周
    sm_config_set_clkdiv(&c, 1.0f);

    // ステートマシン初期化
    pio_sm_init(pio, sm, offset, &c);
    // ステートマシン起動
    pio_sm_set_enabled(pio, sm, true);

    // 動作開始の確認用にオンボードLEDを光らせる
    gpio_init(ONBOARD_LED_PIN);
    gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);
    gpio_put(ONBOARD_LED_PIN, 1);

    printf("button_duration (PIO+LED GP16) ready.\n");
    while (true) {
        pio_sm_get_blocking(pio, sm); // ボタンが押されるまで待つ
        uint64_t press_start_us = time_us_64();

        pio_sm_get_blocking(pio, sm); // ボタンが離されるまで待つ
        uint64_t press_end_us = time_us_64();

        uint64_t delta_us = press_end_us - press_start_us;
        float delta_ms = delta_us / 1000.0f;

        if (delta_ms < 5.0f) {
            // あまりに短い押下はノイズとみなして無視
            continue;
        }
        printf("pressed for %.1f ms (%llu us)\n", delta_ms, (unsigned long long)delta_us);
    }
}
