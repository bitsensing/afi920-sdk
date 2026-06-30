#!/usr/bin/env bash
# Build (if needed) and launch the AFI920 C++ 3D radar viewer (Linux).
# All arguments are passed through, e.g.:
#   ./run.sh 192.168.10.150
#   ./run.sh --sim
#   ./run.sh --replay recordings/rec_XXXX.jsonl
#
# Requires: cmake, a C++17 compiler, and Qt5/Qt6 (Widgets + OpenGL) dev packages.
# Override the CMake generator with CMAKE_GENERATOR=... (default: CMake's default).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

BIN="build/afi_radar_viewer"
if [[ ! -x "$BIN" ]]; then
    echo "[run.sh] $BIN not found — building..."
    GEN=()
    [[ -n "${CMAKE_GENERATOR:-}" ]] && GEN=(-G "$CMAKE_GENERATOR")
    cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release
    cmake --build build
fi
exec "$BIN" "$@"
