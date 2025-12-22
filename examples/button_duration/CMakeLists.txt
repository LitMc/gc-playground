cmake_minimum_required(VERSION 3.13)
add_executable(button_duration
    main.cpp
)

# .pioからヘッダ生成
pico_generate_pio_header(button_duration ${CMAKE_CURRENT_LIST_DIR}/button_duration.pio)

target_link_libraries(button_duration
    pico_stdlib
    hardware_pio
)

pico_enable_stdio_uart(button_duration 1)  # UART経由のstdioを有効
pico_enable_stdio_usb(button_duration 0)   # USB経由のstdioは無効（お好み）

pico_add_extra_outputs(button_duration)
