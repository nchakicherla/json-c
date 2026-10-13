cmake_minimum_required(VERSION 3.20)
project(json-c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)

file(GLOB_RECURSE SRCS CONFIGURE_DEPENDS src/*.c)

add_executable(main ${SRCS})
target_compile_options(main PRIVATE -Wall -Wextra -g)
