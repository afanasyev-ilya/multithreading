cmake_minimum_required(VERSION 3.20)
project(aggregator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(aggregator src/aggregator.cpp)

target_compile_options(aggregator PRIVATE -Wall -Wextra -Wpedantic)