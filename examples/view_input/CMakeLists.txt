    cmake_minimum_required(VERSION 3.13)
    project(view_input)
    add_executable(${PROJECT_NAME}
    main.cpp
    joybus_pio_sm.cpp
    pad_client.cpp
    console_client.cpp
    )

    # .pioからヘッダ生成
    pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/joybus_console.pio)
    pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/joybus_pad.pio)

    target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_dma
    hardware_irq
    hardware_pio
    hardware_sync
    )

    pico_enable_stdio_uart(${PROJECT_NAME} 1) # UART経由のstdioを有効
    pico_enable_stdio_usb(${PROJECT_NAME} 0) # USB経由のstdioは無効（お好み）

    pico_add_extra_outputs(${PROJECT_NAME})
