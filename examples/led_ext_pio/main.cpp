#include "hardware/pio.h"
#include "led_ext.pio.h"
#include "pico/stdlib.h"

int main() {
    const uint LED_PIN = 16;
    stdio_init_all();

    // PIO本体とステートマシン番号
    PIO pio = pio0;
    const uint sm = 0;

    // PIOプログラムをロード
    uint offset = pio_add_program(pio, &led_ext_program);

    // デフォルト設定を取得
    pio_sm_config c = led_ext_program_get_default_config(offset);

    // このステートマシンが操作するピンを指定（LED_PINから始めて1ピン分）
    sm_config_set_out_pins(&c, LED_PIN, 1);

    // 実際にGPIOをPIO用に初期化
    pio_gpio_init(pio, LED_PIN);
    // 出力に設定
    pio_sm_set_consecutive_pindirs(pio, sm, LED_PIN, 1, true);

    // クロック分周
    sm_config_set_clkdiv(&c, 1.0f);

    // ステートマシン初期化
    pio_sm_init(pio, sm, offset, &c);
    // ステートマシン開始
    pio_sm_set_enabled(pio, sm, true);

    while (true) {
        // 1を送ってPIOがLEDを点灯させる
        pio_sm_put_blocking(pio, sm, 1);
        sleep_ms(500);

        // 0を送ってPIOがLEDを消灯させる
        pio_sm_put_blocking(pio, sm, 0);
        sleep_ms(500);
    }
}
