cmake_minimum_required(VERSION 3.13)
add_executable(send_receive
    main.cpp
)

# .pioからヘッダ生成
pico_generate_pio_header(send_receive ${CMAKE_CURRENT_LIST_DIR}/joy_tx4.pio)
pico_generate_pio_header(send_receive ${CMAKE_CURRENT_LIST_DIR}/joy_rx4.pio)

target_link_libraries(send_receive
    pico_stdlib
    hardware_pio
)

pico_enable_stdio_uart(send_receive 1)  # UART経由のstdioを有効
pico_enable_stdio_usb(send_receive 0)   # USB経由のstdioは無効（お好み）

pico_add_extra_outputs(send_receive)
