"""
Sample: Receive RDI/SHII/SPI data streams from AFI920 sensor

Supports TCP (default) and UDP transport modes.
TCP: connects to sensor and reads SOME/IP framed messages.
UDP: listens on local ports for SOME/IP-TP segmented events.

Protocol v3.0: every stream payload carries a 20B E2E Profile 7 header
(CRC-64/XZ) ahead of the ISO 23150 payload — parsing lives in
afi_sdk.streams.

Usage:
    python sample_data_stream.py [sensor_ip] [duration_sec] [tcp|udp]

Copyright 2026 bitsensing Inc. All rights reserved.
"""

import socket
import struct
import sys
import threading
import time
from typing import Dict

from afi_sdk.someip import (
    SOMEIP_HEADER_FMT, SOMEIP_HEADER_SIZE, EVENT_RDI, EVENT_SHII, EVENT_SPI, MSG_TYPE_TP,
    DEFAULT_RDI_PORT, DEFAULT_SHII_PORT, DEFAULT_SPI_PORT,
)
from afi_sdk.streams import strip_e2e, parse_rdi_frame, parse_rdi_detection


# ── SOME/IP-TP Reassembler ──

class TpReassembler:
    """Reassemble SOME/IP-TP segmented messages (for RDI)."""

    def __init__(self):
        self._buffers: Dict[int, bytearray] = {}
        self.completed_frames = 0
        self.total_segments = 0

    def feed(self, tp_header: bytes, payload: bytes,
             session_id: int) -> bytes:
        """Feed a TP segment. Returns complete payload when done, else b""."""
        self.total_segments += 1
        offset_raw = struct.unpack(">I", tp_header)[0]
        more_flag = offset_raw & 0x01
        offset_bytes = (offset_raw >> 4) * 16

        if session_id not in self._buffers:
            self._buffers[session_id] = bytearray()

        buf = self._buffers[session_id]
        end = offset_bytes + len(payload)
        if end > len(buf):
            buf.extend(b"\x00" * (end - len(buf)))
        buf[offset_bytes:end] = payload

        if more_flag == 0:
            result = bytes(buf)
            del self._buffers[session_id]
            self.completed_frames += 1
            return result

        return b""


# ── Stream Receiver (TCP / UDP) ──

class StreamReceiver:
    """Receives SOME/IP events via TCP or UDP in a background thread.

    Strips and CRC-verifies the E2E header on every frame; the stored
    first frame is the ISO 23150 payload (post-E2E).
    """

    def __init__(self, name: str, port: int, event_id: int,
                 sensor_ip: str = "", transport: str = "tcp"):
        self.name = name
        self.port = port
        self.event_id = event_id
        self.sensor_ip = sensor_ip
        self.transport = transport
        self.packets = 0
        self.bytes_recv = 0
        self.frames = 0
        self.e2e_errors = 0
        self._running = False
        self._sock = None
        self._thread = None
        self._tp = TpReassembler() if event_id == EVENT_RDI else None
        self._first_frame = None

    def start(self):
        self._running = True
        self._thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=3.0)
        if self._sock:
            self._sock.close()

    def _deliver(self, frame: bytes):
        """Strip/verify E2E and record the ISO payload."""
        e2e, payload = strip_e2e(frame)
        if e2e is None:
            return
        if not e2e.get("crc_ok", True):
            self.e2e_errors += 1
        self.frames += 1
        if not self._first_frame:
            self._first_frame = payload

    def _recv_exact(self, n: int) -> bytes:
        """Read exactly n bytes from TCP socket."""
        buf = b""
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                return b""
            buf += chunk
        return buf

    def _recv_loop_tcp(self):
        """TCP mode: connect to sensor, read SOME/IP framed messages."""
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512 * 1024)
            self._sock.settimeout(5.0)
            self._sock.connect((self.sensor_ip, self.port))
        except Exception:
            return

        while self._running:
            try:
                # Read SOME/IP prefix: MessageID(4) + Length(4)
                hdr = self._recv_exact(8)
                if len(hdr) < 8:
                    break
                length = struct.unpack(">I", hdr[4:8])[0]
                rest = self._recv_exact(length)
                if len(rest) < length:
                    break

                data = hdr + rest
                self.packets += 1
                self.bytes_recv += len(data)

                if len(data) < SOMEIP_HEADER_SIZE:
                    continue

                # Payload starts after 16-byte SOME/IP header (no TP over TCP)
                self._deliver(data[SOMEIP_HEADER_SIZE:])
            except socket.timeout:
                continue
            except OSError:
                break

    def _recv_loop_udp(self):
        """UDP mode: bind to port, receive SOME/IP datagrams with TP reassembly."""
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        self._sock.bind(("", self.port))
        self._sock.settimeout(1.0)

        while self._running:
            try:
                data, addr = self._sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                break

            if len(data) < SOMEIP_HEADER_SIZE:
                continue

            self.packets += 1
            self.bytes_recv += len(data)

            h = struct.unpack(SOMEIP_HEADER_FMT, data[:SOMEIP_HEADER_SIZE])
            msg_type = h[7]
            session_id = h[4]
            payload = data[SOMEIP_HEADER_SIZE:]

            is_tp = (msg_type & MSG_TYPE_TP) != 0

            if is_tp and self._tp:
                if len(payload) < 4:
                    continue
                tp_hdr = payload[:4]
                tp_payload = payload[4:]
                frame = self._tp.feed(tp_hdr, tp_payload, session_id)
                if frame:
                    self._deliver(frame)
            else:
                self._deliver(payload)

    def _recv_loop(self):
        if self.transport == "tcp":
            self._recv_loop_tcp()
        else:
            self._recv_loop_udp()


def main():
    sensor_ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.150"
    duration = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    transport = sys.argv[3] if len(sys.argv) > 3 else "tcp"

    print("AFI920 Data Stream Receiver")
    print(f"  Sensor:    {sensor_ip}")
    print(f"  Duration:  {duration}s")
    print(f"  Transport: {transport.upper()}")
    print()

    # Configure sensor to match our transport mode.
    # Protocol-only partial update: never touch the destination IPs —
    # a unicast destination makes the sensor reject TCP clients connecting
    # from any other host IP (multi-NIC pitfall).
    try:
        from afi_sdk import AfiSdk, AfiSdkConfig
        sdk = AfiSdk()
        if sdk.init(AfiSdkConfig(sensor_ip=sensor_ip)) == 0:
            net = sdk.get_network_config() or {}
            current = {net.get(f"{s}_protocol") for s in
                       ("detection", "health", "performance")}
            if current != {transport}:
                sdk.set_network_config(
                    detection_protocol=transport,
                    health_protocol=transport,
                    performance_protocol=transport,
                )
                print(f"  Sensor streams switched to {transport.upper()}")
                time.sleep(1.0)  # stream channels reconnect after the change
            else:
                print(f"  Sensor streams already {transport.upper()}")
            sdk.close()
    except Exception as e:
        print(f"  Warning: could not configure sensor protocol ({e})")

    # Create receivers for each stream type
    receivers = [
        StreamReceiver("RDI",  DEFAULT_RDI_PORT,  EVENT_RDI,  sensor_ip, transport),
        StreamReceiver("SHII", DEFAULT_SHII_PORT, EVENT_SHII, sensor_ip, transport),
        StreamReceiver("SPI",  DEFAULT_SPI_PORT,  EVENT_SPI,  sensor_ip, transport),
    ]

    # Start all receivers
    for r in receivers:
        r.start()
        if transport == "tcp":
            print(f"  Connecting to {sensor_ip}:{r.port} ({r.name})")
        else:
            print(f"  Listening on UDP:{r.port} ({r.name})")

    print(f"\nReceiving for {duration} seconds...")
    time.sleep(duration)

    # Stop all receivers
    for r in receivers:
        r.stop()

    # Print results
    print(f"\n{'='*70}")
    print(f"  {'Stream':<6} {'Packets':>8} {'Bytes':>10} {'Frames':>8} "
          f"{'E2E err':>8} {'Rate':>10}")
    print(f"  {'-'*6:<6} {'-'*8:>8} {'-'*10:>10} {'-'*8:>8} "
          f"{'-'*8:>8} {'-'*10:>10}")

    for r in receivers:
        rate = f"{r.packets / duration:.1f} Hz" if duration > 0 else "-"
        mb = f"{r.bytes_recv / 1024 / 1024:.2f} MB"
        print(f"  {r.name:<6} {r.packets:>8} {mb:>10} {r.frames:>8} "
              f"{r.e2e_errors:>8} {rate:>10}")

    # Print first RDI frame details
    rdi = receivers[0]
    if rdi._first_frame:
        hdr = parse_rdi_frame(rdi._first_frame)
        if hdr:
            print("\n--- First RDI Frame ---")
            print(f"  Version:    {hdr['version']}")
            print(f"  Sensor ID:  {hdr['sensor_id']}")
            print(f"  Detections: {hdr['num_detections']}")
            print(f"  Counter:    {hdr['message_counter']}")

            if hdr["num_detections"] > 0:
                det = parse_rdi_detection(rdi._first_frame, hdr["det_start"], 0)
                if det:
                    print(f"  Detection #0: range={det['radial_distance']:.2f}m "
                          f"az={det['azimuth']:.3f}rad "
                          f"el={det['elevation']:.3f}rad "
                          f"vel={det['radial_velocity']:.2f}m/s "
                          f"rcs={det['rcs']:.1f}dBm2")

    total_packets = sum(r.packets for r in receivers)
    total_bytes = sum(r.bytes_recv for r in receivers)
    print(f"\nTotal: {total_packets} packets, {total_bytes / 1024 / 1024:.2f} MB")
    print("Done.")


if __name__ == "__main__":
    main()
