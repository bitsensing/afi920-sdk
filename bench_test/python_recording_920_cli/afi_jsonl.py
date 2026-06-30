"""
afi_jsonl — the unified ``afi-radar-jsonl`` recording format (single source of truth).

A recording is one UTF-8 JSONL file: line 1 is a header object, every following
line is a data record (rdi / shii / spi). The format stores *parsed* sensor-frame
fields, so a file written by any AFI920 tool (Python or C++, CLI or viewer) replays
in either viewer. See JSONL_FORMAT.md for the wire spec.

This module is **stdlib only** (no numpy) so the CLI recorder stays dependency-free;
it reuses the afi_sdk stream parsers for decoding ISO 23150 payloads.

Copyright (c) 2026 bitsensing Inc.
"""

import json
import struct
import sys
import threading
from pathlib import Path
from typing import Dict, Iterator, List, Optional


# ── Make afi_sdk importable (installed, or the repo's python/src) ──
def _ensure_afi_sdk() -> None:
    try:
        import afi_sdk  # noqa: F401
        return
    except ImportError:
        pass
    # Walk up until the SDK tree is found, so this works wherever the tool
    # folder lives under the repo (e.g. bench_test/python_recording_920_cli).
    here = Path(__file__).resolve().parent
    for base in (here, *here.parents):
        cand = base / "python" / "src"
        if (cand / "afi_sdk").is_dir():
            sys.path.insert(0, str(cand))
            return


_ensure_afi_sdk()

from afi_sdk.streams import parse_rdi_frame, parse_shi, parse_spi  # noqa: E402

FORMAT = "afi-radar-jsonl"
VERSION = 1

# RDI detection (51 B, little-endian, packed). We only keep six fields; the full
# struct is unpacked so iter_unpack can stride the detection block efficiently.
#   [0]existence(B) [1]det_id(H) [2]obj_id(H) [3]ts_diff(f) [4]rcs(f) [5]snr(f)
#   [6]ambig(H) [7]r(f) [8]az(f) [9]el(f) [10]r_err [11]az_err [12]el_err
#   [13]v(f) [14]v_err(f)
_DET = struct.Struct("<BHHfffHffffffff")
assert _DET.size == 51, _DET.size
_I_RCS, _I_SNR, _I_R, _I_AZ, _I_EL, _I_V = 4, 5, 7, 8, 9, 13

_VALID_STREAMS = ("rdi", "shii", "spi")


def _encode(stream: str, idx: int, host_ts: float,
            payload: bytes) -> Optional[dict]:
    """Parse one post-E2E ISO 23150 payload into a JSONL record dict."""
    if stream == "rdi":
        hdr = parse_rdi_frame(payload)
        if not hdr:
            return None
        det_start = hdr["det_start"]
        claimed = hdr["num_detections"]
        available = max(0, (len(payload) - det_start) // _DET.size)
        n = min(claimed, available)
        r: List[float] = []
        az: List[float] = []
        el: List[float] = []
        v: List[float] = []
        rcs: List[float] = []
        snr: List[float] = []
        if n:
            block = payload[det_start:det_start + n * _DET.size]
            for t in _DET.iter_unpack(block):
                rcs.append(t[_I_RCS]); snr.append(t[_I_SNR])
                r.append(t[_I_R]); az.append(t[_I_AZ]); el.append(t[_I_EL])
                v.append(t[_I_V])
        return {"t": "rdi", "ts": host_ts, "s": idx,
                "counter": hdr["message_counter"], "ts_ns": hdr["timestamp"],
                "n": n, "claimed": claimed, "r": r, "az": az, "el": el,
                "v": v, "rcs": rcs, "snr": snr}

    if stream == "shii":
        m = parse_shi(payload)
        if m is None:
            return None
        return {"t": "shii", "ts": host_ts, "s": idx,
                "counter": m.get("message_counter", 0),
                "defect": m.get("defect_recognised", 0),
                "reason": m.get("defect_reason", 0),
                "supply": m.get("supply_voltage_status", 0),
                "temp": m.get("temperature_status", 0),
                "time_sync": m.get("time_sync", 0),
                "modes": list(m.get("operation_modes", [])),
                "cal": list(m.get("calibration_statuses", []))}

    if stream == "spi":
        m = parse_spi(payload)
        if m is None:
            return None
        p = m.get("pose", {})
        pose = [p.get("origin_x", 0.0), p.get("origin_y", 0.0),
                p.get("origin_z", 0.0), p.get("yaw", 0.0),
                p.get("pitch", 0.0), p.get("roll", 0.0),
                p.get("yaw_error", 0.0), p.get("pitch_error", 0.0)]
        fov = [{"az0": s.get("azimuth_begin", 0.0),
                "az1": s.get("azimuth_end", 0.0),
                "el0": s.get("elevation_begin", 0.0),
                "el1": s.get("elevation_end", 0.0),
                "blockage": s.get("blockage_status", 0)}
               for s in m.get("fov_segments", [])]
        return {"t": "spi", "ts": host_ts, "s": idx,
                "counter": m.get("message_counter", 0),
                "pose": pose, "fov": fov}

    return None


class JsonlRecorder:
    """Thread-safe JSONL recorder. ``write()`` accepts raw post-E2E payloads so
    it is a drop-in for any sink that already strips the E2E header."""

    def __init__(self, path, meta: Optional[dict] = None):
        self.path = Path(path)
        if self.path.suffix != ".jsonl":
            self.path = self.path.with_suffix(".jsonl")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self.record_count = 0
        self._closed = False
        self._f = open(self.path, "w", encoding="utf-8", newline="\n")
        try:
            header = {"type": "header", "format": FORMAT, "version": VERSION}
            header.update(meta or {})
            self._f.write(json.dumps(header, separators=(",", ":")) + "\n")
            self._f.flush()
        except BaseException:
            self._closed = True
            try:
                self._f.close()
            except OSError:
                pass
            raise

    def write(self, host_ts: float, stream: str, sensor_index: int,
              payload: bytes) -> bool:
        """Encode + append one payload. Returns True iff a record was written
        (False if the stream is unknown, the payload failed to parse, or the
        recorder is closed) — callers use this for accurate per-frame stats."""
        if stream not in _VALID_STREAMS:
            return False
        rec = _encode(stream, sensor_index & 0xFF, host_ts, payload)
        if rec is None:
            return False
        line = json.dumps(rec, separators=(",", ":"))
        with self._lock:
            if self._closed:
                return False
            self._f.write(line)
            self._f.write("\n")
            self.record_count += 1
            return True

    def flush(self) -> None:
        with self._lock:
            if not self._closed:
                self._f.flush()

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._closed = True
            try:
                self._f.write(json.dumps(
                    {"type": "footer", "record_count": self.record_count},
                    separators=(",", ":")) + "\n")
                self._f.flush()
                self._f.close()
            except OSError:
                pass


class JsonlReplayReader:
    """Reads an ``afi-radar-jsonl`` file. ``meta`` mirrors the legacy sidecar
    shape so the viewer's replay path consumes it unchanged."""

    def __init__(self, path):
        self.path = Path(path)
        with open(self.path, "r", encoding="utf-8") as f:
            first = f.readline()
        try:
            hdr = json.loads(first) if first.strip() else {}
        except json.JSONDecodeError:
            hdr = {}
        if not isinstance(hdr, dict) or hdr.get("type") != "header":
            raise ValueError(
                f"not an {FORMAT} recording (missing header line): {self.path}")
        self.header = hdr
        self.meta = {
            "kind": hdr.get("kind", "replay"),
            "transport": hdr.get("transport", "?"),
            "started_unix": hdr.get("started_unix", 0.0),
            "record_count": hdr.get("record_count"),
            "sensors": hdr.get("sensors", []),
        }
        if self.meta["record_count"] is None:
            self.meta["record_count"] = self._count_records()

    def _count_records(self) -> int:
        n = 0
        with open(self.path, "r", encoding="utf-8") as f:
            f.readline()  # skip header
            for line in f:
                s = line.strip()
                if not s:
                    continue
                # cheap: a data record always contains the "t" key
                if '"t":' in s:
                    n += 1
        return n

    def records(self) -> Iterator[dict]:
        """Yield each data record dict (rdi/shii/spi) in capture order."""
        with open(self.path, "r", encoding="utf-8") as f:
            f.readline()  # skip header
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(rec, dict) and rec.get("t") in _VALID_STREAMS:
                    yield rec

    def count(self) -> Dict[str, int]:
        out = {"rdi": 0, "shii": 0, "spi": 0}
        for rec in self.records():
            t = rec.get("t")
            if t in out:
                out[t] += 1
        return out
