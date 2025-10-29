cmake_minimum_required(VERSION 3.12)
project(counter_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(counter_service src/counter_service.cpp)

target_compile_options(counter_service PRIVATE -Wall -Wextra -Wpedantic)