    cmake_minimum_required(VERSION 3.13)
    add_executable(monitor
    main.cpp
    joybus_pio_sm.cpp
    )

    # .pioからヘッダ生成
    pico_generate_pio_header(monitor ${CMAKE_CURRENT_LIST_DIR}/joybus_console.pio)
    pico_generate_pio_header(monitor ${CMAKE_CURRENT_LIST_DIR}/joybus_pad.pio)

    target_link_libraries(monitor
    pico_stdlib
    hardware_dma
    hardware_irq
    hardware_pio
    hardware_sync
    )

    pico_enable_stdio_uart(monitor 1) # UART経由のstdioを有効
    pico_enable_stdio_usb(monitor 0) # USB経由のstdioは無効（お好み）

    pico_add_extra_outputs(monitor)
