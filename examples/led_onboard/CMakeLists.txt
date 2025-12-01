cmake_minimum_required(VERSION 3.13)
add_executable(led_onboard
    led_onboard.cpp
)

target_link_libraries(led_onboard
    pico_stdlib
)

pico_add_extra_outputs(led_onboard)
