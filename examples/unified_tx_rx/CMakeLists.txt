    cmake_minimum_required(VERSION 3.13)
    add_executable(unified_tx_rx
    main.cpp
    joybus_pio_sm.cpp
    )

    # .pioからヘッダ生成
    pico_generate_pio_header(unified_tx_rx ${CMAKE_CURRENT_LIST_DIR}/joy_tx5.pio)
    pico_generate_pio_header(unified_tx_rx ${CMAKE_CURRENT_LIST_DIR}/joy_rx5.pio)
    pico_generate_pio_header(unified_tx_rx ${CMAKE_CURRENT_LIST_DIR}/joybus_console.pio)
    pico_generate_pio_header(unified_tx_rx ${CMAKE_CURRENT_LIST_DIR}/joybus_pad.pio)

    target_link_libraries(unified_tx_rx
    pico_stdlib
    hardware_dma
    hardware_irq
    hardware_pio
    hardware_sync
    )

    pico_enable_stdio_uart(unified_tx_rx 1) # UART経由のstdioを有効
    pico_enable_stdio_usb(unified_tx_rx 0) # USB経由のstdioは無効（お好み）

    pico_add_extra_outputs(unified_tx_rx)
