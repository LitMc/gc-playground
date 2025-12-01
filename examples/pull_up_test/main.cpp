#include "pico/stdlib.h"

int main() {
    const uint BTN_PIN = 2; // GP2
    stdio_init_all();
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_pull_up(BTN_PIN); // プルアップ抵抗を有効化

    while (true) {
        tight_loop_contents();
    }
}
