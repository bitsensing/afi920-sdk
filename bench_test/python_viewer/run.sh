#!/usr/bin/env bash
# Launch the AFI920 Python 3D radar viewer (Linux). Auto-creates a repo-local
# venv and installs requirements.txt on first run (see ../_env.sh). Needs a
# desktop session (X11) and the system xcb libs PyQt5 depends on — on a fresh
# host: apt install python3-venv libxcb-cursor0 libxcb-xinerama0 (or equivalent).
# All arguments are passed through, e.g.:
#   ./run.sh 192.168.10.150
#   ./run.sh --sim
#   ./run.sh --replay recordings/rec_XXXX.jsonl
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
source ../_env.sh
ensure_venv requirements.txt
exec "$PYTHON" radar_viewer.py "$@"
