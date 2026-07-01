"""
Sample: RDI stream watchdog with automatic reconnect.

Receives the RDI (point cloud) stream over TCP and monitors data liveness. If no
frame arrives within a stall timeout (cable pull, sensor reboot, switch glitch),
the receiver treats the link as down, closes the socket, and reconnects — looping
until interrupted. A template for resilient long-running stream consumers.

Usage:
    python sample_stream_reconnect.py [sensor_ip] [stall_timeout_sec]

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import socket
import struct
import sys
import time

from afi_sdk.someip import SOMEIP_HEADER_SIZE, DEFAULT_RDI_PORT
from afi_sdk.streams import strip_e2e, parse_rdi_frame


def _recv_exact(sock, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf


def receive_until_stall(sock, stall_timeout: float) -> str:
    """Read RDI frames until the stream stalls or closes.

    Returns the reason the loop ended: 'stall', 'closed', or 'error'.
    """
    last_frame = time.monotonic()
    frames = 0
    while True:
        try:
            hdr = _recv_exact(sock, 8)              # MessageID(4) + Length(4)
            if len(hdr) < 8:
                return "closed"
            length = struct.unpack(">I", hdr[4:8])[0]
            rest = _recv_exact(sock, length)
            if len(rest) < length:
                return "closed"
        except socket.timeout:
            if time.monotonic() - last_frame > stall_timeout:
                return "stall"
            continue
        except OSError:
            return "error"

        data = hdr + rest
        if len(data) < SOMEIP_HEADER_SIZE:
            continue
        _e2e, payload = strip_e2e(data[SOMEIP_HEADER_SIZE:])
        hdr_info = parse_rdi_frame(payload)
        if hdr_info is None:
            continue

        frames += 1
        last_frame = time.monotonic()
        if frames % 20 == 1:
            print(f"  RDI frame #{frames}: {hdr_info['num_detections']} detections "
                  f"(counter={hdr_info['message_counter']})")


def main() -> int:
    sensor_ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.150"
    stall_timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0

    print("AFI920 RDI Stream Reconnect Watchdog")
    print(f"  Sensor:        {sensor_ip}")
    print(f"  Stall timeout: {stall_timeout}s\n")

    attempt = 0
    while True:
        attempt += 1
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512 * 1024)
        # Socket timeout must be shorter than the stall timeout so we can
        # notice a silent stall (no bytes) rather than blocking forever.
        sock.settimeout(min(1.0, stall_timeout))
        try:
            print(f"  [attempt {attempt}] connecting to {sensor_ip}:{DEFAULT_RDI_PORT}...")
            sock.connect((sensor_ip, DEFAULT_RDI_PORT))
            print("  connected — receiving")
            attempt = 0
            reason = receive_until_stall(sock, stall_timeout)
            print(f"  link down ({reason}); reconnecting...")
        except OSError as e:
            print(f"  connect failed ({e}); retrying...")
        except KeyboardInterrupt:
            print("\n  Stopped.")
            sock.close()
            return 0
        finally:
            sock.close()

        # Exponential-ish backoff, capped, so a persistent outage doesn't spin.
        time.sleep(min(5.0, 0.5 * attempt) if attempt else 1.0)


if __name__ == "__main__":
    sys.exit(main())
