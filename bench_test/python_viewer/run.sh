#!/usr/bin/env bash
# Launch the AFI920 Python 3D radar viewer (Linux).
# All arguments are passed through, e.g.:
#   ./run.sh 192.168.10.150
#   ./run.sh --sim
#   ./run.sh --replay recordings/rec_XXXX.jsonl
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
PY="${PYTHON:-python3}"
exec "$PY" radar_viewer.py "$@"
