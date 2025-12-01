cmake_minimum_required(VERSION 3.13)
add_executable(led_ext_pio
    main.cpp
)

# .pioからヘッダ生成
pico_generate_pio_header(led_ext_pio ${CMAKE_CURRENT_LIST_DIR}/led_ext.pio)

target_link_libraries(led_ext_pio
    pico_stdlib
    hardware_pio
)

pico_add_extra_outputs(led_ext_pio)
