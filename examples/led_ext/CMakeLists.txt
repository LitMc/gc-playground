cmake_minimum_required(VERSION 3.13)
add_executable(led_ext
    led_ext.cpp
)

target_link_libraries(led_ext
    pico_stdlib
)

pico_add_extra_outputs(led_ext)
