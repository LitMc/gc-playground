cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(gc_playground C CXX ASM)

pico_sdk_init()

add_subdirectory(examples/led_ext)
add_subdirectory(examples/led_onboard)
add_subdirectory(examples/led_ext_pio)
add_subdirectory(examples/led_ext_pio_auto)
add_subdirectory(examples/button_pio)
add_subdirectory(examples/pull_up_test)
add_subdirectory(examples/button_pio_in)
add_subdirectory(examples/button_pio_in_out)
add_subdirectory(examples/button_duration)
add_subdirectory(examples/send_receive)
