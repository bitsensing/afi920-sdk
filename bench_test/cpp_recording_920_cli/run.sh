#!/usr/bin/env bash
# Build (if needed) and launch the AFI920 C++ CLI recorder (Linux). No GUI/Qt —
# suitable for a headless host (e.g. a Jetson Orin AGX). Needs cmake + a C++17
# compiler; ../_env.sh adds a venv's pip-installed cmake/ninja to PATH if present,
# otherwise the system cmake is used.
# All arguments are passed through, e.g.:
#   ./run.sh record 192.168.10.150 --duration 30
#   ./run.sh record --config radars.json
# Override the CMake generator with CMAKE_GENERATOR=... (default: CMake's default).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
source ../_env.sh

BIN="build/afi_recorder"
if [[ ! -x "$BIN" ]]; then
    echo "[run.sh] $BIN not found — building..."
    GEN=()
    [[ -n "${CMAKE_GENERATOR:-}" ]] && GEN=(-G "$CMAKE_GENERATOR")
    cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release
    cmake --build build
fi
exec "$BIN" "$@"
