"""
Recording / replay container for the AFI920 3D viewer.

A recording is two files that share a stem:
  <stem>.btsrec   binary log of raw ISO 23150 payloads (post-E2E), so replay
                  re-parses through the exact same code path as live capture
  <stem>.json     sidecar metadata: sensor IPs, per-sensor mounting pose,
                  transport, start time

Binary layout:
  8-byte magic  b"BTSREC1\n"
  then repeated records:
    record header  "<dBBI"  = host_unix_ts (f64), stream_code (u8),
                              sensor_index (u8), payload_len (u32)
    payload        <payload_len> bytes  (ISO 23150 payload, little-endian)

stdlib only — unit-testable headless.

Copyright (c) 2026 bitsensing Inc.
"""

import json
import struct
import threading
from pathlib import Path
from typing import Dict, Iterator, Optional, Tuple

MAGIC = b"BTSREC1\n"
_REC_HDR = struct.Struct("<dBBI")  # host_ts, stream_code, sensor_idx, payload_len

STREAM_CODE = {"rdi": 0, "shii": 1, "spi": 2}
STREAM_NAME = {v: k for k, v in STREAM_CODE.items()}

# Guard against a corrupt length field allocating gigabytes.
_MAX_PAYLOAD = 16 * 1024 * 1024


class Recorder:
    """Append raw ISO payloads to a .btsrec file (thread-safe)."""

    def __init__(self, path, meta: Optional[dict] = None):
        self.path = Path(path)
        if self.path.suffix != ".btsrec":
            self.path = self.path.with_suffix(".btsrec")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._f = open(self.path, "wb")
        try:
            self._f.write(MAGIC)
            self.record_count = 0
            self.bytes_written = len(MAGIC)
            self._meta = dict(meta or {})
            self._closed = False
            self._write_sidecar()
        except BaseException:
            # Don't leak the file handle if the sidecar write (or anything
            # else here) fails before the object is usable.
            self._closed = True
            try:
                self._f.close()
            except OSError:
                pass
            raise

    @property
    def sidecar_path(self) -> Path:
        return self.path.with_suffix(".json")

    def _write_sidecar(self) -> None:
        with open(self.sidecar_path, "w", encoding="utf-8") as f:
            json.dump(self._meta, f, indent=2)

    def write(self, host_ts: float, stream: str, sensor_index: int,
              payload: bytes) -> None:
        code = STREAM_CODE.get(stream)
        if code is None or len(payload) > _MAX_PAYLOAD:
            return
        hdr = _REC_HDR.pack(host_ts, code, sensor_index & 0xFF, len(payload))
        with self._lock:
            if self._closed:
                return
            self._f.write(hdr)
            self._f.write(payload)
            self.record_count += 1
            self.bytes_written += len(hdr) + len(payload)

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
                self._f.flush()
                self._f.close()
            except OSError:
                pass
        # Refresh sidecar with the final record count.
        self._meta["record_count"] = self.record_count
        try:
            self._write_sidecar()
        except OSError:
            pass


class ReplayReader:
    """Iterate the records of a .btsrec file in capture order."""

    def __init__(self, path):
        self.path = Path(path)
        if self.path.suffix != ".btsrec":
            self.path = self.path.with_suffix(".btsrec")
        with open(self.sidecar_path, "r", encoding="utf-8") as f:
            self.meta = json.load(f)

    @property
    def sidecar_path(self) -> Path:
        return self.path.with_suffix(".json")

    def records(self) -> Iterator[Tuple[float, str, int, bytes]]:
        """Yield (host_ts, stream_name, sensor_index, payload) tuples."""
        with open(self.path, "rb") as f:
            magic = f.read(len(MAGIC))
            if magic != MAGIC:
                raise ValueError(f"not a BTSREC file: {self.path}")
            while True:
                head = f.read(_REC_HDR.size)
                if len(head) < _REC_HDR.size:
                    break  # clean EOF (or truncated tail — stop gracefully)
                host_ts, code, sensor_index, plen = _REC_HDR.unpack(head)
                if plen > _MAX_PAYLOAD:
                    break
                payload = f.read(plen)
                if len(payload) < plen:
                    break  # truncated final record
                stream = STREAM_NAME.get(code)
                if stream is None:
                    continue
                yield host_ts, stream, sensor_index, payload

    def time_span(self) -> Tuple[Optional[float], Optional[float]]:
        """Return (first_ts, last_ts) over the file, or (None, None) if empty."""
        first = last = None
        for host_ts, _s, _i, _p in self.records():
            if first is None:
                first = host_ts
            last = host_ts
        return first, last

    def count(self) -> Dict[str, int]:
        """Per-stream record counts (a quick integrity summary)."""
        out = {"rdi": 0, "shii": 0, "spi": 0}
        for _ts, stream, _i, _p in self.records():
            out[stream] = out.get(stream, 0) + 1
        return out
