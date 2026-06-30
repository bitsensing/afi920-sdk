"""
radar_logger — AFI920 hardened stream receive layer (shared by the bench tools).

This is the "shared receive layer" referenced by the bench_test tools' READMEs.
It lives alongside the tools under ``bench_test/logger/``. Both the Python CLI
recorder (``bench_test/python_recording_920_cli/record_cli.py``) and the Python
3D viewer (``bench_test/python_viewer/radar_source.py``) import the public names
below:

    StreamWorker, StreamStats, STREAM_SPECS, configure_sensor, udp_port_for

It receives AFI920 RDI / SHII / SPI SOME/IP events over TCP or UDP and delivers
each complete frame — the SOME/IP payload starting at the 20-byte E2E Profile 7
header — to a sink's ``deliver(frame: bytes)`` method (the sink strips and
verifies the E2E header itself). RDI frames over UDP are reassembled from
SOME/IP-TP segments.

The wire handling mirrors the SDK's own examples (``python/examples/
sample_data_stream.py`` / ``sample_pointcloud.py``) and reuses
``afi_sdk.someip`` for the header layout and constants.

Stdlib-only (no numpy, no Qt) so the dependency-free CLI recorder can use it and
it can be unit-tested headless.

Copyright (c) 2026 bitsensing Inc.
SPDX-License-Identifier: BSD-3-Clause
"""

import socket
import struct
import sys
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, Optional


# ── Make afi_sdk importable (installed, or the repo's python/src) ──
def _ensure_afi_sdk() -> None:
    try:
        import afi_sdk  # noqa: F401
        return
    except ImportError:
        pass
    here = Path(__file__).resolve().parent
    for base in (here, *here.parents):
        cand = base / "python" / "src"
        if (cand / "afi_sdk").is_dir():
            sys.path.insert(0, str(cand))
            return


_ensure_afi_sdk()

from afi_sdk.someip import (                                       # noqa: E402
    SOMEIP_HEADER_FMT, SOMEIP_HEADER_SIZE, MSG_TYPE_TP,
    EVENT_RDI, EVENT_SHII, EVENT_SPI,
    DEFAULT_RDI_PORT, DEFAULT_SHII_PORT, DEFAULT_SPI_PORT,
)

# Sanity cap on a single SOME/IP message length (TCP). A full RDI frame is
# ~4096 detections * 51 B + headers (~210 KB); 16 MB is comfortably above that
# while still rejecting an obviously corrupt length field.
_MAX_SOMEIP_LEN = 16 * 1024 * 1024


# ── Per-stream registry ──
#
# value = (event_id, default_tcp_port, uses_tp, config_name)
#   event_id      SOME/IP event id for the stream
#   default_tcp_port  the radar's default TCP port for the stream
#   uses_tp       whether the stream is SOME/IP-TP segmented over UDP (RDI is)
#   config_name   the Config-API channel name ("detection"/"health"/"performance")
#                 used by AfiSdk.set_network_config / set_stream_destination
STREAM_SPECS = {
    "rdi":  (EVENT_RDI,  DEFAULT_RDI_PORT,  True,  "detection"),
    "shii": (EVENT_SHII, DEFAULT_SHII_PORT, False, "health"),
    "spi":  (EVENT_SPI,  DEFAULT_SPI_PORT,  False, "performance"),
}

# UDP local-bind ports are laid out per sensor index in blocks of 10, matching
# the C++ CLI recorder (rdi=30509+idx*10, shii=30510+idx*10, spi=30511+idx*10).
_UDP_INDEX_STRIDE = 10


def udp_port_for(stream: str, idx: int) -> int:
    """Local UDP port to bind when receiving ``stream`` from sensor ``idx``."""
    return STREAM_SPECS[stream][1] + idx * _UDP_INDEX_STRIDE


@dataclass
class StreamStats:
    """Light per-(sensor, stream) counters shared between a worker and its sink.

    Ownership split: the :class:`StreamWorker` sets ``connected``; the sink owns
    the frame / byte / error / detection counts (it sees the actual payloads).
    """
    sensor: str = ""
    stream: str = ""
    connected: bool = False
    frames: int = 0
    detections: int = 0
    bytes_recv: int = 0
    parse_errors: int = 0
    e2e_errors: int = 0


# ── SOME/IP-TP reassembly (RDI over UDP) ──

class _TpReassembler:
    """Reassemble SOME/IP-TP segmented messages, keyed by session id.

    TP word (big-endian uint32): bits [31:4] = offset in 16-byte units,
    bit 0 = "more segments" flag. The final segment (more=0) completes the frame.
    """

    def __init__(self):
        self._buffers: Dict[int, bytearray] = {}

    def feed(self, tp_header: bytes, payload: bytes, session_id: int) -> bytes:
        offset_raw = struct.unpack(">I", tp_header)[0]
        more_flag = offset_raw & 0x01
        offset_bytes = (offset_raw >> 4) * 16

        buf = self._buffers.setdefault(session_id, bytearray())
        end = offset_bytes + len(payload)
        if end > len(buf):
            buf.extend(b"\x00" * (end - len(buf)))
        buf[offset_bytes:end] = payload

        if more_flag == 0:
            result = bytes(buf)
            del self._buffers[session_id]
            return result
        return b""


# ── Stream worker thread ──

class StreamWorker(threading.Thread):
    """Receive one (sensor, stream) over TCP or UDP and deliver frames to a sink.

    Each complete frame passed to ``sink.deliver(frame)`` is the SOME/IP payload
    starting at the 20-byte E2E header. The worker only manages the connection
    (and the ``connected`` flag on the sink's stats); the sink does the E2E
    strip / CRC check / parse / record.

    Args:
        ip:         sensor IP (TCP connect target; ignored for the UDP bind)
        stream:     "rdi" / "shii" / "spi"
        port:       TCP connect port, or UDP local-bind port
        uses_tp:    expect SOME/IP-TP segmentation (UDP RDI)
        transport:  "tcp" or "udp"
        sink:       object with ``.deliver(frame: bytes)`` and ``.stats``
        stop_event: ``threading.Event`` — set it to stop the worker
    """

    def __init__(self, ip, stream, port, uses_tp, transport, sink, stop_event):
        super().__init__(name=f"{stream}@{ip}:{port}", daemon=True)
        self.ip = ip
        self.stream = stream
        self.port = int(port)
        self.uses_tp = bool(uses_tp)
        self.transport = transport
        self.sink = sink
        self._stats = getattr(sink, "stats", None)
        # NB: must NOT be named ``_stop`` — that shadows threading.Thread._stop,
        # an internal method the runtime calls on join()/thread-exit, which would
        # raise "'Event' object is not callable".
        self._stop_evt = stop_event
        self._sock: Optional[socket.socket] = None

    # -- helpers --

    def _set_connected(self, value: bool) -> None:
        if self._stats is not None:
            self._stats.connected = value

    def _close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def _recv_exact(self, n: int) -> Optional[bytes]:
        """Read exactly n bytes from the TCP socket, staying stop-aware.

        Accumulates across socket timeouts (so a slow frame never desyncs the
        SOME/IP framing). Returns None if the stop event fires or the peer
        closes / errors.
        """
        buf = bytearray()
        while len(buf) < n:
            if self._stop_evt.is_set():
                return None
            try:
                chunk = self._sock.recv(n - len(buf))
            except socket.timeout:
                continue
            except OSError:
                return None
            if not chunk:
                return None
            buf += chunk
        return bytes(buf)

    # -- run loops --

    def run(self) -> None:
        try:
            if self.transport == "tcp":
                self._run_tcp()
            else:
                self._run_udp()
        finally:
            self._set_connected(False)
            self._close()

    def _run_tcp(self) -> None:
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512 * 1024)
            self._sock.settimeout(1.0)
            self._sock.connect((self.ip, self.port))
        except OSError:
            return  # sensor unreachable / stream port closed — nothing to do
        self._set_connected(True)

        while not self._stop_evt.is_set():
            # SOME/IP framing: MessageID(4) + Length(4), then Length more bytes.
            prefix = self._recv_exact(8)
            if prefix is None:
                break
            length = struct.unpack(">I", prefix[4:8])[0]
            if length < 8 or length > _MAX_SOMEIP_LEN:
                break  # corrupt length field — bail rather than desync
            rest = self._recv_exact(length)
            if rest is None:
                break
            data = prefix + rest  # full SOME/IP message (16B header + payload)
            if len(data) < SOMEIP_HEADER_SIZE:
                continue
            # No TP over TCP: payload starts right after the 16B SOME/IP header.
            self.sink.deliver(data[SOMEIP_HEADER_SIZE:])

    def _run_udp(self) -> None:
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                  4 * 1024 * 1024)
            self._sock.bind(("", self.port))
            self._sock.settimeout(1.0)
        except OSError:
            return
        tp = _TpReassembler() if self.uses_tp else None

        while not self._stop_evt.is_set():
            try:
                data, _addr = self._sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                break
            if len(data) < SOMEIP_HEADER_SIZE:
                continue
            self._set_connected(True)

            h = struct.unpack(SOMEIP_HEADER_FMT, data[:SOMEIP_HEADER_SIZE])
            msg_type = h[7]
            session_id = h[4]
            payload = data[SOMEIP_HEADER_SIZE:]

            if tp is not None and (msg_type & MSG_TYPE_TP):
                if len(payload) < 4:
                    continue
                frame = tp.feed(payload[:4], payload[4:], session_id)
                if frame:
                    self.sink.deliver(frame)
            else:
                # A non-segmented datagram carries the whole frame after the
                # SOME/IP header (small RDI frames, or all SHII / SPI frames).
                self.sink.deliver(payload)


# ── Best-effort sensor configuration via the Config API ──

def configure_sensor(ip: str, streams: Iterable[str], transport: str,
                     host_ip: Optional[str] = None, idx: int = 0) -> None:
    """Align a sensor's stream transport to ``transport`` (best effort).

    TCP: switch the requested streams' protocol to TCP with a *protocol-only*
         partial update. The destination IP is deliberately left untouched — a
         fixed unicast destination makes the sensor reject TCP clients connecting
         from any other host IP (a multi-NIC pitfall; see the SDK examples).
    UDP: point each requested stream at this host (``host_ip``) and the per-index
         UDP port, so the sensor pushes its datagrams to us.

    A missing / offline sensor is a quiet no-op (there is nothing to configure).
    Hard Config-API misuse (e.g. UDP without ``host_ip``) raises; callers in the
    bench tools wrap this in try/except and log "configure skipped".
    """
    from afi_sdk import AfiSdk, AfiSdkConfig

    streams = list(streams)
    sdk = AfiSdk()
    try:
        if sdk.init(AfiSdkConfig(sensor_ip=ip)) != 0:
            return  # sensor not reachable — leave its config alone
        if transport == "udp":
            if not host_ip:
                raise ValueError("UDP configure_sensor requires host_ip")
            for stream in streams:
                name = STREAM_SPECS[stream][3]
                sdk.set_stream_destination(name, host_ip,
                                           udp_port_for(stream, idx), "udp")
        else:
            fields = {f"{STREAM_SPECS[s][3]}_protocol": "tcp" for s in streams}
            if fields:
                sdk.set_network_config(**fields)
    finally:
        sdk.close()


__all__ = [
    "StreamWorker",
    "StreamStats",
    "STREAM_SPECS",
    "configure_sensor",
    "udp_port_for",
]
