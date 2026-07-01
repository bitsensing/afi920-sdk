cmake_minimum_required(VERSION 3.16)
project(afi_sdk VERSION 3.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source files are UTF-8 (e.g. Korean comments). Tell MSVC to read/emit UTF-8
# so it does not warn (C4819) under non-UTF-8 system code pages such as 949.
if(MSVC)
    add_compile_options(/utf-8)
endif()

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
