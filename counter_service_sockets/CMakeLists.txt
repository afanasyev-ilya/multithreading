cmake_minimum_required(VERSION 3.12)
project(counter_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(counter_server src/counter_server.cpp)
add_executable(benchmark src/benchmark.cpp)

target_compile_options(counter_server PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(benchmark PRIVATE -Wall -Wextra -Wpedantic)