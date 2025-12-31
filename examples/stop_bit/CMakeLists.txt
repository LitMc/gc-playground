cmake_minimum_required(VERSION 3.13)
add_executable(stop_bit
    main.cpp
)

# .pioからヘッダ生成
# 本体からコントローラへの送信を想定し5us版を使用
pico_generate_pio_header(stop_bit ${CMAKE_CURRENT_LIST_DIR}/joy_tx5.pio)
pico_generate_pio_header(stop_bit ${CMAKE_CURRENT_LIST_DIR}/joy_rx5.pio)

target_link_libraries(stop_bit
    pico_stdlib
    hardware_pio
)

pico_enable_stdio_uart(stop_bit 1)  # UART経由のstdioを有効
pico_enable_stdio_usb(stop_bit 0)   # USB経由のstdioは無効（お好み）

pico_add_extra_outputs(stop_bit)
