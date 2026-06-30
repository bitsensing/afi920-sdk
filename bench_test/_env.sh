# _env.sh — shared, portable environment setup for the bench_test run.sh scripts.
#
# Sourced by each tool's run.sh (which already has `set -euo pipefail`). It keeps
# NO machine-specific absolute paths: the venv is repo-local and auto-created, and
# any per-host extras (e.g. a non-system Qt, or staged shim libs) live in an
# untracked _env.local.sh next to this file. On a fresh machine the scripts fall
# back to the system python / cmake / Qt.
#
# Override knobs (set in _env.local.sh or the environment):
#   AFI_VENV       path to the Python venv           (default: <bench_test>/.venv)
#   AFI_QTDIR      a non-system Qt prefix             (used only by cpp_viewer)
#   AFI_EXTRALIBS  dir of extra runtime .so shims     (prepended to LD_LIBRARY_PATH)

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Repo-local venv shared by the bench tools (override with AFI_VENV).
AFI_VENV="${AFI_VENV:-$BENCH_DIR/.venv}"

# Per-machine overrides — not committed (see .gitignore).
if [[ -f "$BENCH_DIR/_env.local.sh" ]]; then
    source "$BENCH_DIR/_env.local.sh"
fi

# If a venv already exists, put it first on PATH (its python3, plus any
# pip-installed cmake/ninja used to build the C++ tools).
if [[ -x "$AFI_VENV/bin/python3" ]]; then
    export PATH="$AFI_VENV/bin:$PATH"
    export PYTHON="${PYTHON:-$AFI_VENV/bin/python3}"
fi

# Optional staged runtime libs (e.g. libxcb-cursor / libxcb-xinerama on a host
# where they aren't installed system-wide). Only used if AFI_EXTRALIBS is set.
if [[ -n "${AFI_EXTRALIBS:-}" && -d "$AFI_EXTRALIBS" ]]; then
    export LD_LIBRARY_PATH="$AFI_EXTRALIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

# ensure_venv [REQUIREMENTS_FILE]
#   Create the venv if missing and install REQUIREMENTS_FILE (if given). Degrades
#   gracefully to the system python3 when venv/pip are unavailable (minimal server
#   image) or offline, so a stdlib-only tool still runs.
ensure_venv() {
    if [[ ! -x "$AFI_VENV/bin/python3" ]]; then
        echo "[run.sh] creating venv: $AFI_VENV"
        if ! python3 -m venv "$AFI_VENV" 2>/dev/null; then
            echo "[run.sh] WARN: cannot create venv (try installing 'python3-venv'); using system python3." >&2
            export PYTHON="$(command -v python3)"
            return 0
        fi
        "$AFI_VENV/bin/python3" -m pip install -q --upgrade pip || true
    fi
    if [[ -n "${1:-}" && -f "$1" ]]; then
        "$AFI_VENV/bin/python3" -m pip install -q -r "$1" \
            || echo "[run.sh] WARN: 'pip install -r $1' failed (offline?); continuing." >&2
    fi
    export PATH="$AFI_VENV/bin:$PATH"
    export PYTHON="$AFI_VENV/bin/python3"
}
