cmake_minimum_required(VERSION 3.16)
project(afi_recorder LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── AFI SDK ─────────────────────────────────────────────────────────────────
# Reuse the repo's afi_sdk target; build it from the repo root when configured
# standalone. This tool lives at <repo>/bench_test/cpp_recording_920_cli, so the
# SDK root is two directories up.
set(AFI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
if(NOT TARGET afi_sdk)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${AFI_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/afi_sdk_build)
endif()

# MinGW headers don't declare SetThreadDescription (a Win10-RS1 API) that the
# SDK's platform.h guards on _WIN32_WINNT >= 0x0A00 (MinGW's default). Cap it
# below 0x0A00 for the SDK target so it takes the cosmetic no-op branch.
if(MINGW AND TARGET afi_sdk)
    target_compile_definitions(afi_sdk PRIVATE _WIN32_WINNT=0x0603 WINVER=0x0603)
endif()

# ── Recorder executable (no Qt — CLI only) ──────────────────────────────────
add_executable(afi_recorder
    main.cpp
    jsonl_writer.cpp
)

# nlohmann/json (header-only) is vendored under the repo's third_party/.
target_include_directories(afi_recorder PRIVATE ${AFI_ROOT}/third_party)

target_link_libraries(afi_recorder PRIVATE afi_sdk)

if(MSVC)
    target_compile_options(afi_recorder PRIVATE /utf-8)
endif()

# Statically link the MinGW runtime so the CLI is a single standalone .exe
# (no libstdc++-6 / libgcc_s / libwinpthread DLLs needed on PATH).
if(MINGW)
    target_link_options(afi_recorder PRIVATE
        -static -static-libgcc -static-libstdc++)
endif()
