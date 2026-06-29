cmake_minimum_required(VERSION 3.16)
project(afi_sdk VERSION 2.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS    "Build test programs"    OFF)

add_subdirectory(src)

if(BUILD_EXAMPLES)
    add_subdirectory(examples/cpp)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
