cmake_minimum_required(VERSION 3.13)
add_executable(button_pio
    main.cpp
)

# .pioからヘッダ生成
pico_generate_pio_header(button_pio ${CMAKE_CURRENT_LIST_DIR}/button_pio.pio)

target_link_libraries(button_pio
    pico_stdlib
    hardware_pio
)

pico_add_extra_outputs(button_pio)
