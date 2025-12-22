#include "button_pio_in_out.pio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

int main() {
    const uint ONBOARD_LED_PIN = PICO_DEFAULT_LED_PIN;
    const uint BTN_PIN = 2;  // GP2
    const uint LED_PIN = 16; // GP16
    stdio_init_all();

    // ボタン入力ピンをプルアップ
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_pull_up(BTN_PIN);

    // PIO本体とステートマシン番号
    PIO pio = pio0;
    const uint sm = 0;

    // PIOプログラムをロード
    uint offset = pio_add_program(pio, &button_pio_in_out_program);
    // デフォルト設定を取得
    pio_sm_config c = button_pio_in_out_program_get_default_config(offset);

    // ステートマシンの操作するピンを指定
    sm_config_set_in_pins(&c, BTN_PIN);
    sm_config_set_out_pins(&c, LED_PIN, 1);

    // ピンをPIO用に切り替える
    pio_gpio_init(pio, BTN_PIN);
    pio_gpio_init(pio, LED_PIN);
    // 方向を設定
    pio_sm_set_consecutive_pindirs(pio, sm, BTN_PIN, 1, false); // 入力
    pio_sm_set_consecutive_pindirs(pio, sm, LED_PIN, 1, true);  // 出力

    // クロック分周
    sm_config_set_clkdiv(&c, 4000.0f);

    // ステートマシン初期化
    pio_sm_init(pio, sm, offset, &c);
    // ステートマシン起動
    pio_sm_set_enabled(pio, sm, true);

    // 動作開始の確認用にオンボードLEDを光らせる
    gpio_init(ONBOARD_LED_PIN);
    gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);
    gpio_put(ONBOARD_LED_PIN, 1);

    while (true) {
        // ステートマシンから値を受け取る
        uint32_t value = pio_sm_get_blocking(pio, sm);
        bool pin_is_high = (value != 0);
        bool button_is_pressed = !pin_is_high; // プルアップなのでLowのとき押している
        // アプリの意味づけ: 押している間LEDを点灯
        bool led_is_on = button_is_pressed;

        uint32_t out_bit = led_is_on ? 1 : 0;
        pio_sm_put_blocking(pio, sm, out_bit);
    }
}
