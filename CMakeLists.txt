cmake_minimum_required(VERSION 3.20)
project(ffiber VERSION 1.0 LANGUAGES CXX ASM_NASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(ffiber ffiber.cpp context_switch.asm)

add_custom_target(explain
    COMMAND ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_FLAGS} -S -O3 #-fverbose-asm
    -fno-asynchronous-unwind-tables
            ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
            -o generated.asm
    COMMENT "generating Assembly code..."
)
