cmake_minimum_required(VERSION 3.22.1)

project("snesbt")

add_library(
    snes_uinput
    SHARED
    snes_uinput.c
)

find_library(
    log-lib
    log
)

target_link_libraries(
    snes_uinput
    ${log-lib}
)