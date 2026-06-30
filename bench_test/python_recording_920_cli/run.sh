#!/usr/bin/env bash
# Launch the AFI920 Python CLI recorder (Linux). Stdlib-only, no GUI — suitable
# for a headless host (e.g. a Jetson Orin AGX). Uses a repo-local venv when one
# can be created, and otherwise falls back to the system python3 (see ../_env.sh).
# All arguments are passed through, e.g.:
#   ./run.sh record 192.168.10.150 --duration 30
#   ./run.sh record --config radars.json
#   ./run.sh record 192.168.10.150 --transport udp --host-ip 192.168.10.10
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
source ../_env.sh
ensure_venv            # stdlib-only: no requirements to install
exec "$PYTHON" record_cli.py "$@"
