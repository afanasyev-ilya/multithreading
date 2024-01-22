cmake_minimum_required(VERSION 3.26)
project(gemm)

set (CMAKE_CXX_FLAGS " -fPIC -O2 -march=haswell")
set(CMAKE_CXX_STANDARD 17)

add_executable(gemm gemm.cpp
        vectorized.hpp
        reordered.hpp
        naive.hpp
        transposed.hpp)
