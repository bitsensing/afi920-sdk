#!/usr/bin/env python3
"""
AFI920 CLI Recorder (Python)
============================

Connect to one or more AFI920 imaging radars and record their RDI / SHII / SPI
streams to a single unified ``.jsonl`` file (see JSONL_FORMAT.md). Recording only
— no GUI. The output replays in either the Python or the C++ 3D viewer.

Targets (up to 4 radars) come from, in priority order:
  1. command line:  ``record IP[:PORT] ...``  or  ``--radar IP[:PORT]`` (repeatable)
  2. a JSON config: ``--config radars.json``   (used when no radar is given on the CLI)

PORT is the radar's RDI/detection port (default 30509); SHII and SPI use PORT+1
and PORT+2 (the AFI920 default contiguous layout).

Examples:
  python record_cli.py record 192.168.10.150
  python record_cli.py record 192.168.10.150 192.168.10.151:30509 --streams rdi shii spi
  python record_cli.py record --radar 192.168.10.150 --duration 30
  python record_cli.py record --config radars.json
  python record_cli.py record 192.168.10.150 --transport udp --host-ip 192.168.10.10

Dependencies: Python 3.8+ and the afi_sdk package (auto-found in ../python/src).

Copyright (c) 2026 bitsensing Inc.
"""

import argparse
import json
import logging
import signal
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import List, Optional

# ── Make afi_sdk + the logger's hardened receive layer importable ──
# afi_sdk lives in the SDK's python/src (at the repo root); the shared receive
# layer (radar_logger) lives next to the bench tools at bench_test/logger.
_HERE = Path(__file__).resolve().parent


def _find_repo_root(start: Path) -> Path:
    """Walk up until the SDK tree (python/src/afi_sdk) is found."""
    for base in (start, *start.parents):
        if (base / "python" / "src" / "afi_sdk").is_dir():
            return base
    return start.parent.parent


_ROOT = _find_repo_root(_HERE)
for _p in (_ROOT / "python" / "src", _HERE.parent / "logger", _HERE):
    if _p.is_dir() and str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

try:
    from afi_sdk import AfiSdk, AfiSdkConfig
    from afi_sdk.streams import strip_e2e
    from afi_sdk.someip import DEFAULT_RDI_PORT
    from radar_logger import (
        StreamWorker, StreamStats, STREAM_SPECS, configure_sensor, udp_port_for,
    )
    from afi_jsonl import JsonlRecorder
except ImportError as exc:  # pragma: no cover - environment guard
    sys.stderr.write(
        f"ERROR: could not import dependencies ({exc}).\n"
        "Run from inside the repo so ../python/src and ../logger are found,\n"
        "or 'pip install -e ./python'.\n")
    raise SystemExit(2)

MAX_RADARS = 4
log = logging.getLogger("afi_recorder")


# ── Radar target = ip + RDI base port ──

class Radar:
    __slots__ = ("ip", "port")

    def __init__(self, ip: str, port: int = DEFAULT_RDI_PORT):
        self.ip = ip
        self.port = int(port)

    def stream_port(self, stream: str) -> int:
        """TCP port for one stream = base + the stream's default offset."""
        offset = STREAM_SPECS[stream][1] - STREAM_SPECS["rdi"][1]
        return self.port + offset

    def __repr__(self):
        return f"{self.ip}:{self.port}"


def parse_target(token: str) -> Radar:
    """Parse an ``IP`` or ``IP:PORT`` token.

    Handles bracketed IPv6 (``[::1]`` / ``[::1]:30509``). A bare token with more
    than one colon is treated as a port-less IPv6 literal, so its address colons
    are not mistaken for a port separator.
    """
    token = token.strip()
    if token.startswith("["):                      # [IPv6] or [IPv6]:port
        end = token.find("]")
        if end < 0:
            raise ValueError(f"unterminated '[' in target '{token}'")
        ip = token[1:end]
        rest = token[end + 1:]
        if rest.startswith(":"):
            port = rest[1:]
            if not port.isdigit():
                raise ValueError(f"bad port in target '{token}'")
            return Radar(ip, int(port))
        return Radar(ip)
    if token.count(":") == 1:                       # IPv4:port
        ip, _, port = token.rpartition(":")
        if not port.isdigit():
            raise ValueError(f"bad port in target '{token}'")
        return Radar(ip, int(port))
    return Radar(token)                             # IPv4 / bare IPv6, no port


def load_config(path: Path):
    """Read radar targets (and optional defaults) from a JSON config file.

    Schema:
      {"transport":"tcp","streams":["rdi","shii","spi"],"host_ip":null,
       "radars":[{"ip":"192.168.10.150","port":30509}, ...]}
    Returns (radars, transport_or_None, streams_or_None, host_ip_or_None).
    """
    with open(path, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    entries = cfg.get("radars") or cfg.get("sensors") or []
    radars: List[Radar] = []
    for e in entries:
        if isinstance(e, str):
            radars.append(parse_target(e))
        elif isinstance(e, dict):
            ip = e.get("ip") or e.get("address")
            if not ip:
                raise ValueError(f"config radar entry missing 'ip': {e}")
            radars.append(Radar(ip, e.get("port", DEFAULT_RDI_PORT)))
        else:
            raise ValueError(f"bad radar entry in config: {e!r}")
    streams = cfg.get("streams")
    return radars, cfg.get("transport"), streams, cfg.get("host_ip")


# ── Sink: strip E2E, append to the JSONL recorder, keep light stats ──

class JsonlSink:
    def __init__(self, idx: int, ip: str, stream: str,
                 recorder: JsonlRecorder, stats: StreamStats):
        self.idx = idx
        self.ip = ip
        self.stream = stream
        self.recorder = recorder
        self.stats = stats

    def deliver(self, frame: bytes) -> None:
        self.stats.bytes_recv += len(frame)
        e2e, payload = strip_e2e(frame)
        if e2e is None:
            self.stats.parse_errors += 1
            return
        if not e2e.get("crc_ok", True):
            self.stats.e2e_errors += 1
        # write() returns True iff a record was appended; using the return value
        # (instead of comparing record_count before/after) avoids a TOCTOU race
        # with the other sink threads sharing this recorder.
        if self.recorder.write(time.time(), self.stream, self.idx, payload):
            self.stats.frames += 1
        else:
            self.stats.parse_errors += 1


def read_pose(ip: str) -> dict:
    """Best-effort mounting pose + identity via the Config API (for the header)."""
    out = {"pose": {"offset_x_m": 0.0, "offset_y_m": 0.0, "offset_z_m": 0.0,
                    "yaw_deg": 0.0, "pitch_deg": 0.0, "roll_deg": 0.0},
           "model": "", "serial": ""}
    sdk = AfiSdk()
    try:
        if sdk.init(AfiSdkConfig(sensor_ip=ip)) == 0:
            mp = sdk.get_mounting_position() or {}
            rot = sdk.get_rotation() or {}
            info = sdk.get_device_info() or {}
            out["pose"] = {
                "offset_x_m": mp.get("offset_x_m", 0.0),
                "offset_y_m": mp.get("offset_y_m", 0.0),
                "offset_z_m": mp.get("offset_z_m", 0.0),
                "yaw_deg": rot.get("yaw_deg", 0.0),
                "pitch_deg": rot.get("pitch_deg", 0.0),
                "roll_deg": rot.get("roll_deg", 0.0)}
            out["model"] = info.get("model_name", "")
            out["serial"] = info.get("serial_number", "")
    except Exception:
        pass
    finally:
        sdk.close()
    return out


def print_stats(sinks: List[JsonlSink], elapsed: float) -> None:
    log.info("-" * 62)
    log.info("[t=%5.0fs]  %-16s%-7s%-5s%9s%9s%7s%8s",
             elapsed, "sensor", "stream", "con", "frames", "MB", "e2e", "fps")
    for s in sinks:
        st = s.stats
        fps = st.frames / elapsed if elapsed > 0 else 0.0
        log.info("            %-16s%-7s%-5s%9d%9.1f%7d%8.1f",
                 st.sensor, st.stream, "yes" if st.connected else "no",
                 st.frames, st.bytes_recv / 1e6, st.e2e_errors, fps)


# ── Main ──

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="record_cli.py",
        description="Record AFI920 radar streams to a unified .jsonl file.")
    sub = p.add_subparsers(dest="command", required=True)
    rec = sub.add_parser("record", help="Record radar stream(s) to .jsonl")
    rec.add_argument("targets", nargs="*", metavar="IP[:PORT]",
                     help="Radar targets (up to 4). PORT is the RDI port "
                          "(default %d)." % DEFAULT_RDI_PORT)
    rec.add_argument("--radar", action="append", default=[], metavar="IP[:PORT]",
                     help="Add a radar target (repeatable).")
    rec.add_argument("--config", metavar="FILE",
                     help="JSON config with radar ip/port; used when no target "
                          "is given on the command line (default: radars.json).")
    rec.add_argument("--streams", nargs="+", default=None,
                     choices=list(STREAM_SPECS.keys()),
                     help="Streams to record (default: rdi shii spi).")
    rec.add_argument("--transport", choices=["tcp", "udp"], default=None,
                     help="Transport (default: tcp).")
    rec.add_argument("--host-ip", default=None,
                     help="Host IP the radars push UDP to (required for udp).")
    rec.add_argument("--duration", type=float, default=0.0,
                     help="Seconds to record, 0 = until Ctrl+C (default: 0).")
    rec.add_argument("--outdir", default=str(_HERE / "recordings"),
                     help="Output directory (default: ./recordings).")
    rec.add_argument("--out", default=None,
                     help="Output filename (default: rec_<timestamp>.jsonl).")
    rec.add_argument("--no-configure", action="store_true",
                     help="Do not touch sensor config; assume streams are set up.")
    rec.add_argument("--stats-interval", type=float, default=5.0,
                     help="Seconds between live stats prints (default: 5).")
    return p


def resolve_targets(args) -> tuple:
    """Resolve radars + transport + streams from CLI then config file."""
    transport = args.transport
    streams = args.streams
    host_ip = args.host_ip

    cli_tokens = list(args.targets) + list(args.radar)
    if cli_tokens:
        radars = [parse_target(t) for t in cli_tokens]
    else:
        cfg_path = Path(args.config) if args.config else (_HERE / "radars.json")
        if not cfg_path.is_file():
            raise SystemExit(
                "ERROR: no radar given on the command line and no config file "
                f"found ({cfg_path}).\n"
                "Pass a target (record 192.168.10.150) or --config FILE.")
        radars, c_tp, c_st, c_hip = load_config(cfg_path)
        if not radars:
            raise SystemExit(f"ERROR: config {cfg_path} lists no radars.")
        log.info("Loaded %d radar(s) from %s", len(radars), cfg_path)
        if transport is None:
            transport = c_tp
        if streams is None:
            streams = c_st
        if host_ip is None:
            host_ip = c_hip

    if transport is None:
        transport = "tcp"
    if not streams:
        streams = ["rdi", "shii", "spi"]
    return radars, transport, streams, host_ip


def main(argv: Optional[List[str]] = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s",
                        datefmt="%H:%M:%S")
    args = build_parser().parse_args(argv)
    if args.command != "record":
        build_parser().print_help()
        return 2

    radars, transport, streams, host_ip = resolve_targets(args)

    if not radars:
        raise SystemExit("ERROR: no radars to record.")
    if len(radars) > MAX_RADARS:
        raise SystemExit(f"ERROR: at most {MAX_RADARS} radars at once "
                         f"(got {len(radars)}).")
    if transport == "udp" and not host_ip:
        raise SystemExit("ERROR: --transport udp requires --host-ip.")

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    name = args.out or f"rec_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl"
    out_path = outdir / name

    log.info("AFI920 CLI Recorder")
    log.info("Radars:    %s", ", ".join(str(r) for r in radars))
    log.info("Streams:   %s", ", ".join(streams))
    log.info("Transport: %s", transport.upper())
    log.info("Output:    %s", out_path)

    # 1) Align sensor stream config (best effort) + read mounting poses.
    if not args.no_configure:
        for idx, r in enumerate(radars):
            try:
                configure_sensor(r.ip, streams, transport, host_ip, idx)
            except Exception as e:  # noqa: BLE001
                log.warning("[%s] configure skipped: %s", r.ip, e)

    sensors_meta = []
    for idx, r in enumerate(radars):
        info = read_pose(r.ip)
        sensors_meta.append({"index": idx, "ip": r.ip, "port": r.port,
                             "model": info["model"], "serial": info["serial"],
                             "pose": info["pose"]})

    meta = {"kind": "live", "transport": transport,
            "started_unix": time.time(), "sensors": sensors_meta}

    recorder = JsonlRecorder(out_path, meta)

    # 2) One StreamWorker + JsonlSink per (radar, stream).
    stop_event = threading.Event()
    sinks: List[JsonlSink] = []
    workers: List[StreamWorker] = []
    for idx, r in enumerate(radars):
        for stream in streams:
            _eid, _dport, uses_tp, _key = STREAM_SPECS[stream]
            port = (r.stream_port(stream) if transport == "tcp"
                    else udp_port_for(stream, idx))
            stats = StreamStats(sensor=r.ip, stream=stream)
            sink = JsonlSink(idx, r.ip, stream, recorder, stats)
            w = StreamWorker(r.ip, stream, port, uses_tp, transport, sink,
                             stop_event)
            sinks.append(sink)
            workers.append(w)

    # 3) Ctrl+C handler.
    def _stop(_sig, _frame):
        if not stop_event.is_set():
            log.info("Stop requested, finishing...")
        stop_event.set()

    signal.signal(signal.SIGINT, _stop)
    try:
        signal.signal(signal.SIGTERM, _stop)
    except (ValueError, AttributeError):
        pass

    for w in workers:
        w.start()
    log.info("Recording... (Ctrl+C to stop)\n")

    start = time.monotonic()
    deadline = start + args.duration if args.duration > 0 else None
    next_stats = start + args.stats_interval
    try:
        while not stop_event.is_set():
            now = time.monotonic()
            if deadline is not None and now >= deadline:
                log.info("Duration reached (%.0fs), stopping.", args.duration)
                break
            if now >= next_stats:
                print_stats(sinks, now - start)
                next_stats += args.stats_interval
            stop_event.wait(0.25)
    finally:
        stop_event.set()

    for w in workers:
        w.join(timeout=5.0)
    recorder.close()

    elapsed = time.monotonic() - start
    log.info("")
    print_stats(sinks, elapsed)
    total = sum(s.stats.frames for s in sinks)
    log.info("")
    log.info("Done. %.0fs  %d records -> %s", elapsed, recorder.record_count,
             recorder.path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
