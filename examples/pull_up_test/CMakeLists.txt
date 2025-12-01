cmake_minimum_required(VERSION 3.13)
add_executable(pull_up_test
    main.cpp
)

target_link_libraries(pull_up_test
    pico_stdlib
)

pico_add_extra_outputs(pull_up_test)
