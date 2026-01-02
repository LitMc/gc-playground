cmake_minimum_required(VERSION 3.13)
add_executable(dma
    main.cpp
)

# .pioからヘッダ生成
# 本体からコントローラへの送信を想定し5us版を使用
pico_generate_pio_header(dma ${CMAKE_CURRENT_LIST_DIR}/joy_tx5.pio)
pico_generate_pio_header(dma ${CMAKE_CURRENT_LIST_DIR}/joy_rx5.pio)

target_link_libraries(dma
    pico_stdlib
    hardware_dma
    hardware_irq
    hardware_pio
)

pico_enable_stdio_uart(dma 1)  # UART経由のstdioを有効
pico_enable_stdio_usb(dma 0)   # USB経由のstdioは無効（お好み）

pico_add_extra_outputs(dma)
