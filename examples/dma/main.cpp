#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "joy_rx5.pio.h"
#include "joy_tx5.pio.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <vector>

namespace {
// TX FIFOに積める最大バイト数
constexpr size_t DEFAULT_MAX_FIFO_BYTES = 4 * 8;
// JoyBusでやりとりする最大フレーム長（バイト数）
// 実際はせいぜい10バイトだが非DMAでの限界を超えられるか試すため16に拡張
constexpr size_t JOYBUS_MAX_FRAME_BYTES = 16;

// 通電確認用のオンボードLED
constexpr uint ONBOARD_LED_PIN = PICO_DEFAULT_LED_PIN;
// BOOTSELに入るためのボタン入力
constexpr uint BOOT_BTN_PIN = 26; // GP26
// JoyBus
constexpr uint TX_PIN = 15; // GP15
constexpr uint RX_PIN = 16; // GP16

// DMAチャンネルはあとで空きを割り当てるので未初期化の意味を込めて-1設定
int rx_dma_chan = -1;
int tx_dma_chan = -1;

// 割り込みハンドラから不意にいじられるのでvolatile
volatile bool rx_dma_done = false;
volatile bool rx_dma_error = false;
volatile bool tx_dma_done = false;
volatile bool tx_dma_error = false;

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

void init_bus_pins_safe() {
    // バスへ接続するピンをHi-Zに設定
    gpio_init(TX_PIN);
    gpio_put(TX_PIN, 0);
    gpio_set_dir(TX_PIN, GPIO_IN);

    gpio_init(RX_PIN);
    gpio_set_dir(RX_PIN, GPIO_IN);
}

void init_led() {
    gpio_init(ONBOARD_LED_PIN);
    gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);
    gpio_put(ONBOARD_LED_PIN, 1);
}

inline uint8_t decode_3sample_msbfirst(uint32_t w) {
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

// RXのDMA割り込みハンドラ
void __isr dma_rx_handler() {
    uint32_t status = dma_hw->ints0;
    if (rx_dma_chan >= 0 && (status & (1u << rx_dma_chan))) {
        // 書き戻して割り込みフラグクリア
        dma_hw->ints0 = 1u << rx_dma_chan;
        // 受信完了
        rx_dma_done = true;
    } else {
        // エラー発生
        rx_dma_error = true;
    }
}

// TXのDMA割り込みハンドラ
void __isr dma_tx_handler() {
    uint32_t status = dma_hw->ints1;
    if (tx_dma_chan >= 0 && (status & (1u << tx_dma_chan))) {
        dma_hw->ints1 = 1u << tx_dma_chan;
        tx_dma_done = true;
    } else {
        tx_dma_error = true;
    }
}

void joybus_tx_send_dma(PIO pio, uint sm, const uint8_t *data, size_t nbytes, int tx_dma_chan,
                        dma_channel_config *tx_dma_config) {
    if (nbytes == 0) {
        return;
    }
    if (nbytes > JOYBUS_MAX_FRAME_BYTES) {
        printf("Error: joybus_tx_send_dma: nbytes=%zu exceeds max=%zu\n", nbytes,
               JOYBUS_MAX_FRAME_BYTES);
        return;
    }

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
    const size_t words_of_data = (nbytes + 3) / 4;
    const size_t words_to_send = words_of_data + 1; // bits_to_send_minus1 + データワード数
    uint32_t tx_buffer[words_to_send] = {0};
    tx_buffer[0] = bits_to_send_minus1;
    printf("[TX] bits_to_send_minus1=%lu\n", (unsigned long)bits_to_send_minus1);
    for (size_t w = 0; w < words_of_data; ++w) {
        uint32_t word = 0;
        for (size_t b = 0; b < 4; ++b) {
            size_t byte_index = w * 4 + b;
            uint8_t byte = (byte_index < nbytes) ? data[byte_index] : 0;
            word |= ((uint32_t)byte) << (8 * (3 - b));
        }
        tx_buffer[w + 1] = word;
        printf("[TX] Prepared word %zu: 0x%08lX\n", w, (unsigned long)word);
    }
    tx_dma_done = false;
    tx_dma_error = false;
    dma_channel_configure(tx_dma_chan, tx_dma_config,
                          &pio->txf[sm], // 書き込み先
                          tx_buffer,     // 読み込み元
                          words_to_send, // 転送するワード数（bits_to_send_minus1 + データワード数）
                          true           // 即時開始
    );
    pio->irq_force = (1u << 0);
    printf("[TX] TX start notified via DMA.\n");
    while (!tx_dma_done && !tx_dma_error) {
        // 送信完了待ち
        tight_loop_contents();
    }
    if (tx_dma_error) {
        printf("[TX] TX DMA error occurred.\n");
        return;
    }
    printf("[TX] TX DMA complete.\n");
}
} // namespace

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

    // RX向けDMAの初期化
    rx_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config rx_dma_config = dma_channel_get_default_config(rx_dma_chan);
    channel_config_set_transfer_data_size(&rx_dma_config, DMA_SIZE_32);
    // RX FIFOから読み続けるので固定
    channel_config_set_read_increment(&rx_dma_config, false);
    // バッファに順次書き込むのでインクリメント
    channel_config_set_write_increment(&rx_dma_config, true);
    channel_config_set_dreq(&rx_dma_config, pio_get_dreq(pio_rx, sm_rx, false));
    // RX向けDMA割り込み設定
    dma_channel_set_irq0_enabled(rx_dma_chan, true);      // RX向けチャンネルにDMA_IRQ_0を紐付け
    irq_set_exclusive_handler(DMA_IRQ_0, dma_rx_handler); // DMA_IRQ_0にハンドラを登録
    irq_set_enabled(DMA_IRQ_0, true);                     // DMA_IRQ_0割り込みを有効化

    // TX向けDMAの初期化
    tx_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config tx_dma_config = dma_channel_get_default_config(tx_dma_chan);
    channel_config_set_transfer_data_size(&tx_dma_config, DMA_SIZE_32);
    // バッファから順次読み込むのでインクリメント
    channel_config_set_read_increment(&tx_dma_config, true);
    // TX FIFOへ書き続けるので固定
    channel_config_set_write_increment(&tx_dma_config, false);
    channel_config_set_dreq(&tx_dma_config, pio_get_dreq(pio_tx, sm_tx, true));
    // TX向けDMA割り込み設定
    dma_channel_set_irq1_enabled(tx_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_tx_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    printf("Loopback test ready.\n");

    const std::vector<std::vector<uint8_t>> test_frames = {
        {0xA5},
        {0xFF},
        {0x00},
        {0xA5, 0x5A},                   // 2バイト
        {0x78, 0x56, 0x34, 0x12},       // 4バイト
        {0x12, 0x34, 0x56, 0x78},       // 4バイト
        {0x89, 0xAB, 0xCD, 0xEF},       // 4バイト
        {0x12, 0x34, 0x56, 0x78, 0x9A}, // 5バイト（DMAなしだとRX FIFOがあふれる）
        {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x12,
         0x34}, // 10バイト（JoyBusで最大のフレーム長）
        {0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78, 0x89, 0x9A, 0xAB},       // 11バイト
        {0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78, 0x89, 0x9A, 0xAB, 0xBC}, // 12バイト
        {0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78, 0x89, 0x9A, 0xAB, 0xBC,
         0xCD}, // 13バイト（DMAなしだとTX FIFOがあふれる）
        {0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78, 0x89, 0x9A, 0xAB, 0xBC, 0xCD,
         0xDE}, // 14バイト
        {0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78, 0x89, 0x9A, 0xAB, 0xBC, 0xCD, 0xDE, 0xEF},
        // 15バイト
        {0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78, 0x89, 0x9A, 0xAB, 0xBC, 0xCD, 0xDE, 0xEF,
         0xF0}, // 16バイト
        {0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78, 0x89, 0x9A, 0xAB, 0xBC, 0xCD, 0xDE, 0xEF,
         0xF0, 0x01} // 17バイト（バッファの最大長超過で送信前に弾くはずの長さ）
    };

    while (true) {
        for (const auto &frame : test_frames) {
            const uint32_t expected_bytes = (uint32_t)frame.size();
            if (expected_bytes == 0) {
                continue;
            }
            uint32_t raw_received_words[JOYBUS_MAX_FRAME_BYTES] = {0};
            rx_dma_done = false;
            rx_dma_error = false;
            if (expected_bytes > JOYBUS_MAX_FRAME_BYTES) {
                printf("Error: frame size %u exceeds JOYBUS_MAX_FRAME_BYTES=%u\n", expected_bytes,
                       JOYBUS_MAX_FRAME_BYTES);
                continue;
            }
            dma_channel_configure(rx_dma_chan, &rx_dma_config,
                                  raw_received_words,  // 書き込み先
                                  &pio_rx->rxf[sm_rx], // 読み込み元
                                  expected_bytes,      // 転送するワード数（1ワード=4バイト）
                                  true                 // 即時開始
            );

            const uint32_t bits_to_receive_minus1 = expected_bytes * 8 - 1;
            pio_sm_put_blocking(pio_rx, sm_rx, bits_to_receive_minus1);

            joybus_tx_send_dma(pio_tx, sm_tx, frame.data(), expected_bytes, tx_dma_chan,
                               &tx_dma_config);

            printf("TX(%lu bytes): ", (unsigned long)expected_bytes);
            for (size_t i = 0; i < expected_bytes; ++i) {
                printf(" 0x%02X ", frame[i]);
            }
            printf("\n");

            absolute_time_t start_time = get_absolute_time();
            while (!rx_dma_done && !rx_dma_error) {
                // タイムアウト処理
                if (absolute_time_diff_us(start_time, get_absolute_time()) > 65) {
                    dma_channel_abort(rx_dma_chan);
                    printf("RX DMA timeout (expected %u bytes)\n", expected_bytes);
                    break;
                }
                tight_loop_contents();
            }
            if (rx_dma_error) {
                printf("RX DMA error occurred.\n");
                continue;
            }
            printf("RX(%u bytes): ", expected_bytes);
            for (size_t i = 0; i < expected_bytes; ++i) {
                uint8_t decoded_byte = decode_3sample_msbfirst(raw_received_words[i]);
                printf(" 0x%02X ", decoded_byte);
            }
            printf("\n");
        }
        sleep_ms(5000);
    }
}
