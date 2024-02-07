cmake_minimum_required(VERSION 3.26)
project(sorting)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp")
include_directories(PRIVATE "/usr/local/opt/tbb/include")

find_package(TBB REQUIRED)

link_libraries(TBB::tbb)

add_executable(sorting sorting.cpp
        bucket_sort.cpp
        bucket_sort.h
        quick_sort.cpp
        quick_sort.h)
