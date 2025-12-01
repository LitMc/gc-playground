#include "button_pio.pio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

int main() {
    const uint LED_PIN = 16;
    const uint BTN_PIN = 2;
    stdio_init_all();

    // ボタン入力用ピンのプルアップ抵抗を有効化
    gpio_init(BTN_PIN);
    gpio_pull_up(BTN_PIN);

    PIO pio = pio0;
    const uint sm = 0;

    // PIOプログラムをロード
    uint offset = pio_add_program(pio, &button_pio_program);
    pio_sm_config c = button_pio_program_get_default_config(offset);

    // ステートマシンが操作するピンを指定（LED_PINから始めて1ピン分）
    sm_config_set_set_pins(&c, LED_PIN, 1);

    // ボタンによるジャンプ用ピンを指定
    sm_config_set_jmp_pin(&c, BTN_PIN);

    // PIO用にGPIOを初期化
    pio_gpio_init(pio, LED_PIN);
    pio_gpio_init(pio, BTN_PIN);

    // LEDピンを出力に設定
    pio_sm_set_consecutive_pindirs(pio, sm, LED_PIN, 1, true);
    // ボタンピンを入力に設定
    pio_sm_set_consecutive_pindirs(pio, sm, BTN_PIN, 1, false);

    // クロック分周でステートマシンの速度を調整
    sm_config_set_clkdiv(&c, 4000.0f);

    // ステートマシン初期化
    pio_sm_init(pio, sm, offset, &c);
    // ステートマシン開始
    pio_sm_set_enabled(pio, sm, true);

    // メインループは何もしない
    while (true) {
        tight_loop_contents();
    }
}
