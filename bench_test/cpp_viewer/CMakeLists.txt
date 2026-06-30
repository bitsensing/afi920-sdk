cmake_minimum_required(VERSION 3.16)
project(afi_radar_viewer LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── AFI SDK ─────────────────────────────────────────────────────────────────
# Reuse the repo's afi_sdk target. If this CMakeLists is configured standalone
# (not via the top-level add_subdirectory), build the SDK from the repo root.
# This tool lives at <repo>/bench_test/cpp_viewer, so the SDK root is two up.
set(AFI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
if(NOT TARGET afi_sdk)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${AFI_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/afi_sdk_build)
endif()

# MinGW's headers don't declare SetThreadDescription (a Win10-RS1 API). The
# SDK's platform.h guards it on _WIN32_WINNT >= 0x0A00, which MinGW defaults to,
# so the call is emitted but unresolved. Cap _WIN32_WINNT below 0x0A00 for the
# SDK target so it takes the cosmetic thread-naming no-op branch instead.
if(MINGW AND TARGET afi_sdk)
    target_compile_definitions(afi_sdk PRIVATE _WIN32_WINNT=0x0603 WINVER=0x0603)
endif()

# ── Qt (6 preferred, 5 fallback) ────────────────────────────────────────────
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
    set(VIEWER_QT_LIBS Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
else()
    # 5.15 floor: reliable AUTOMOC for namespaced Q_OBJECT, and a stable QOpenGL* API.
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
    set(VIEWER_QT_LIBS Qt5::Widgets)  # QOpenGL* classes live in Qt5::Gui (via Widgets)
endif()

find_package(OpenGL REQUIRED)

set(CMAKE_AUTOMOC ON)

# ── Viewer executable ───────────────────────────────────────────────────────
add_executable(afi_radar_viewer
    main.cpp
    main_window.cpp
    point_cloud_view.cpp
    radar_source.cpp
    recording.cpp
    jsonl_recording.cpp
)

# nlohmann/json (header-only) is vendored under the repo's third_party/.
target_include_directories(afi_radar_viewer PRIVATE ${AFI_ROOT}/third_party)

target_link_libraries(afi_radar_viewer PRIVATE
    afi_sdk
    ${VIEWER_QT_LIBS}
    OpenGL::GL
)

if(MSVC)
    target_compile_options(afi_radar_viewer PRIVATE /utf-8)
endif()
