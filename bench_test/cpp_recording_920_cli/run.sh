#!/usr/bin/env bash
# Build (if needed) and launch the AFI920 C++ CLI recorder (Linux).
# All arguments are passed through, e.g.:
#   ./run.sh record 192.168.10.150 --duration 30
#   ./run.sh record --config radars.json
#   ./run.sh record 192.168.10.150 --transport udp --host-ip 192.168.10.10
#
# Requires: cmake and a C++17 compiler (no Qt needed for the CLI recorder).
# Override the CMake generator with CMAKE_GENERATOR=... (default: CMake's default).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

BIN="build/afi_recorder"
if [[ ! -x "$BIN" ]]; then
    echo "[run.sh] $BIN not found — building..."
    GEN=()
    [[ -n "${CMAKE_GENERATOR:-}" ]] && GEN=(-G "$CMAKE_GENERATOR")
    cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release
    cmake --build build
fi
exec "$BIN" "$@"
