cmake_minimum_required(VERSION 3.20)
project(counter_service LANGUAGES CXX)

option(ENABLE_TBB "Enable TBB backend if available" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ENABLE_TBB)
  # Prefer the config package (oneTBB); falls back to module if available
  find_package(TBB 2021 QUIET CONFIG)
  if(NOT TBB_FOUND)
    find_package(TBB QUIET) # legacy FindTBB.cmake, if present
  endif()

  if(TBB_FOUND)
    message(STATUS "TBB found: ${TBB_DIR}")
    add_compile_definitions(HAVE_TBB=1)
  else()
    message(WARNING "TBB not found; building without TBB backend")
  endif()
endif()

add_executable(counter_service src/counter_service.cpp)

if(TBB_FOUND)
  target_link_libraries(counter_service PRIVATE TBB::tbb)
endif()

target_compile_options(counter_service PRIVATE -Wall -Wextra -Wpedantic)