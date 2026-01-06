    cmake_minimum_required(VERSION 3.13)
    add_executable(prompt_response
    main.cpp
    joybus_pio_sm.cpp
    )

    # .pioからヘッダ生成
    pico_generate_pio_header(prompt_response ${CMAKE_CURRENT_LIST_DIR}/joybus_console.pio)
    pico_generate_pio_header(prompt_response ${CMAKE_CURRENT_LIST_DIR}/joybus_pad.pio)

    target_link_libraries(prompt_response
    pico_stdlib
    hardware_dma
    hardware_irq
    hardware_pio
    hardware_sync
    )

    pico_enable_stdio_uart(prompt_response 1) # UART経由のstdioを有効
    pico_enable_stdio_usb(prompt_response 0) # USB経由のstdioは無効（お好み）

    pico_add_extra_outputs(prompt_response)
