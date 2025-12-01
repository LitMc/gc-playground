#include "button_pio_in.pio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

int main() {
    const uint LED_PIN = 16;
    const uint BTN_PIN = 2;
    stdio_init_all();

    // 入力ピン（ボタン）の初期化
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_pull_up(BTN_PIN); // プルアップ抵抗を有効化

    // 出力ピン（LED）の初期化
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // PIO本体とステートマシン番号
    PIO pio = pio0;
    const uint sm = 0;

    // PIOプログラムをロード
    uint offset = pio_add_program(pio, &button_pio_in_program);
    pio_sm_config c = button_pio_in_program_get_default_config(offset);

    // このステートマシンが操作するピンを指定（BTN_PINから始めて1ピン分）
    sm_config_set_in_pins(&c, BTN_PIN);
    // GPIOをPIO用に初期化
    pio_gpio_init(pio, BTN_PIN);
    // ステートマシンのピン方向を設定（BTN_PINを入力に）
    pio_sm_set_consecutive_pindirs(pio, sm, BTN_PIN, 1, false);

    // FIFOがいっぱいになるのを防ぐため遅めに設定
    sm_config_set_clkdiv(&c, 4000.0f);

    // ステートマシン初期化
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    while (true) {
        // ステートマシンからRX FIFO経由でデータを受け取る
        uint32_t value = pio_sm_get_blocking(pio, sm);

        // ピンの状態
        bool pin_is_high = (value != 0);

        // ピンがLowならボタンが押されている（プルアップのためアクティブLow）
        bool button_is_pushed = !pin_is_high;

        // LEDはボタンを押している間点灯
        bool led_is_on = button_is_pushed;

        // 状態に合わせてLEDをON/OFF
        gpio_put(LED_PIN, led_is_on);
    }
}
