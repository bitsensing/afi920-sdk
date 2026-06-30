#!/usr/bin/env bash
# Build (if needed) and launch the AFI920 C++ 3D radar viewer (Linux). Needs
# cmake, a C++17 compiler, Qt5/Qt6 (Widgets + OpenGL) and a desktop session.
# If AFI_QTDIR is configured for this host (in _env.local.sh) that Qt is used for
# both the build and at runtime; otherwise CMake finds the system Qt and the
# system provides the Qt/xcb runtime libs.
# All arguments are passed through, e.g.:
#   ./run.sh 192.168.10.150
#   ./run.sh --sim
#   ./run.sh --replay recordings/rec_XXXX.jsonl
# Override the CMake generator with CMAKE_GENERATOR=... (default: CMake's default).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
source ../_env.sh

# Use a non-system Qt only if one is configured for this host.
if [[ -n "${AFI_QTDIR:-}" && -d "$AFI_QTDIR" ]]; then
    export CMAKE_PREFIX_PATH="$AFI_QTDIR${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    export LD_LIBRARY_PATH="$AFI_QTDIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QT_PLUGIN_PATH="$AFI_QTDIR/plugins"
fi

BIN="build/afi_radar_viewer"
if [[ ! -x "$BIN" ]]; then
    echo "[run.sh] $BIN not found — building..."
    GEN=()
    [[ -n "${CMAKE_GENERATOR:-}" ]] && GEN=(-G "$CMAKE_GENERATOR")
    cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release
    cmake --build build
fi
exec "$BIN" "$@"
