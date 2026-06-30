#!/usr/bin/env bash
# Launch the AFI920 Python CLI recorder (Linux).
# All arguments are passed through, e.g.:
#   ./run.sh record 192.168.10.150 --duration 30
#   ./run.sh record --config radars.json
#   ./run.sh record 192.168.10.150 --transport udp --host-ip 192.168.10.10
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
PY="${PYTHON:-python3}"
exec "$PY" record_cli.py "$@"
