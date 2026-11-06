"""
RDI UDP Sender Simulator — interface v1.1.0 (firmware v3.1.1)

Stands in for the radar: sends RDI frames protected by the 20B E2E header
(CRC-32 over the body) and split with SOME/IP-TP over UDP.

Usage:
    python rdi_udp_sender.py [target_ip] [target_port] [num_detections] [fps]

    target_ip:       receiver IP (default: 127.0.0.1)
    target_port:     RDI UDP port (default: 30509)
    num_detections:  detections per frame (default: 50)
    fps:             frames per second (default: 10)

Examples:
    # Local test, together with sample_pointcloud in UDP mode
    python rdi_udp_sender.py 127.0.0.1 30509 50 20

    # Send to the SDK on another host
    python rdi_udp_sender.py 192.168.10.100 30509 100 10

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import math
import random
import signal
import socket
import struct
import sys
import time
from pathlib import Path

# Prefer the SDK sources beside this tool so the simulator cannot drift from
# the parser constants and E2E implementation it is meant to exercise.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))

from afi_sdk.streams import (  # noqa: E402
    E2E_DATA_ID_RDI,
    RDI_DETECTION_SIZE,
    RDI_HEADER_SIZE,
    RDI_MAX_DETECTIONS,
    build_e2e_header,
)

# ── SOME/IP Constants ──

SERVICE_ID = 0x6000
EVENT_RDI = 0x8002
PROTO_VER = 0x01
IFACE_VER = 0x01
MSG_TYPE_NOTIFY = 0x02
MSG_TYPE_TP_NOTIFY = 0x22  # NOTIFY | TP flag (0x20)

SOMEIP_HEADER_SIZE = 16
SOMEIP_HEADER_FMT = ">HHIHHBBBB"
TP_CHUNK_SIZE = 1392  # SOME/IP-TP segment payload limit


def build_rdi_payload(msg_counter: int, num_detections: int,
                      timestamp_ns: int, cycle_time_ns: int) -> bytes:
    """Build an ISO 23150 RDI frame payload (little-endian)."""
    buf = bytearray()

    # ── Common Interface Header (24 bytes) ──
    buf.append(1)   # major version
    buf.append(1)   # minor version (interface v1.1.0)
    buf.append(0)   # patch version
    buf.append(0x08) # interface_id = RDI
    buf.append(1)   # num_serving_sensors
    buf.append(0x01) # sensor_id

    buf.extend(struct.pack("<Q", timestamp_ns))       # timestamp (8B LE)
    buf.extend(struct.pack("<I", msg_counter))         # message_counter (4B LE)
    buf.extend(struct.pack("<I", cycle_time_ns))       # cycle time (4B LE)
    buf.append(0)    # cycle_time_variation
    buf.append(0x00) # data_qualifier (NORMAL)

    # ── Ambiguity Domain (8 bytes: radial velocity only) ──
    buf.extend(struct.pack("<f", -60.0))    # velocity_begin
    buf.extend(struct.pack("<f",  60.0))    # velocity_end

    # ── Detection Info (4 bytes) ──
    buf.extend(struct.pack("<H", RDI_MAX_DETECTIONS))
    buf.extend(struct.pack("<H", num_detections))  # num_valid_detections

    assert len(buf) == RDI_HEADER_SIZE, f"Header size mismatch: {len(buf)} != {RDI_HEADER_SIZE}"

    # ── Detections (51 bytes each) ──
    for i in range(num_detections):
        det = bytearray(RDI_DETECTION_SIZE)

        # Simulate radar targets at various ranges/angles
        angle = 2.0 * math.pi * i / max(num_detections, 1)
        range_m = 5.0 + 50.0 * random.random()
        azimuth = 0.8 * math.sin(angle) + random.gauss(0, 0.02)
        elevation = 0.05 * math.sin(angle * 2) + random.gauss(0, 0.01)
        velocity = -20.0 + 40.0 * random.random()
        rcs = 30.0 * random.random()
        snr = 5.0 + 20.0 * random.random()

        # Status (9B)
        det[0] = int(80 + random.random() * 21)             # existence_prob (%)
        struct.pack_into("<H", det, 1, i & 0xFFFF)           # detection_id
        struct.pack_into("<H", det, 3, 0xFFFF)               # object_id_reference
        struct.pack_into("<f", det, 5, 0.0)                  # timestamp_diff

        # Signal quality and ambiguity grouping
        struct.pack_into("<f", det, 9, rcs)                  # RCS (dBsm)
        struct.pack_into("<f", det, 13, snr)                 # SNR (dB)
        struct.pack_into("<H", det, 17, 0xFFFF)              # ambiguity_grouping_id

        # Position (24B)
        struct.pack_into("<f", det, 19, range_m)             # radial_distance
        struct.pack_into("<f", det, 23, azimuth)             # azimuth
        struct.pack_into("<f", det, 27, elevation)           # elevation
        struct.pack_into("<f", det, 31, 0.1)                 # distance_error
        struct.pack_into("<f", det, 35, 0.01)                # azimuth_error
        struct.pack_into("<f", det, 39, 0.02)                # elevation_error

        # Velocity (8B)
        struct.pack_into("<f", det, 43, velocity)            # radial_velocity
        struct.pack_into("<f", det, 47, 0.2)                 # velocity_error

        buf.extend(det)

    return bytes(buf)


def build_someip_header(method_id: int, length: int, session_id: int,
                        msg_type: int) -> bytes:
    """SOME/IP 16-byte header (big-endian)."""
    return struct.pack(
        SOMEIP_HEADER_FMT,
        SERVICE_ID, method_id, length,
        0x0001, session_id & 0xFFFF,
        PROTO_VER, IFACE_VER, msg_type, 0x00
    )


def send_rdi_frame_udp(sock: socket.socket, target: tuple,
                       rdi_payload: bytes, session_id: int,
                       e2e_counter: int):
    """Send one RDI frame over UDP, split into SOME/IP-TP segments.

    The E2E header plus ISO payload is split when it exceeds TP_CHUNK_SIZE.
    """
    protected_payload = (
        build_e2e_header(E2E_DATA_ID_RDI, e2e_counter, rdi_payload)
        + rdi_payload
    )
    if len(protected_payload) <= TP_CHUNK_SIZE:
        # Single SOME/IP message (no TP)
        length = 8 + len(protected_payload)
        hdr = build_someip_header(EVENT_RDI, length, session_id, MSG_TYPE_NOTIFY)
        sock.sendto(hdr + protected_payload, target)
        return 1

    # SOME/IP-TP segmentation
    offset = 0
    segment_count = 0

    while offset < len(protected_payload):
        remaining = len(protected_payload) - offset
        chunk_len = min(TP_CHUNK_SIZE, remaining)
        more_segments = 1 if (offset + chunk_len < len(protected_payload)) else 0

        # TP header: offset in 16-byte units (bits 31:4) | more_segments (bit 0)
        # offset must be aligned to 16 bytes (except possibly the last segment)
        offset_units = offset // 16
        tp_word = (offset_units << 4) | more_segments
        tp_header = struct.pack(">I", tp_word)

        # SOME/IP length = 8 (rest of someip header) + 4 (TP header) + chunk_len
        length = 8 + 4 + chunk_len
        someip_hdr = build_someip_header(EVENT_RDI, length, session_id,
                                         MSG_TYPE_TP_NOTIFY)

        packet = someip_hdr + tp_header + protected_payload[offset:offset + chunk_len]
        sock.sendto(packet, target)

        # Advance offset by chunk aligned to 16 bytes
        if more_segments:
            offset += (chunk_len // 16) * 16
        else:
            offset += chunk_len

        segment_count += 1

    return segment_count


# ── Main ──

g_running = True


def signal_handler(sig, frame):
    global g_running
    g_running = False


def main():
    global g_running
    signal.signal(signal.SIGINT, signal_handler)

    target_ip = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    target_port = int(sys.argv[2]) if len(sys.argv) > 2 else 30509
    num_detections = int(sys.argv[3]) if len(sys.argv) > 3 else 50
    fps = float(sys.argv[4]) if len(sys.argv) > 4 else 10.0

    if not 0 <= num_detections <= RDI_MAX_DETECTIONS:
        raise SystemExit(
            f"num_detections must be in 0..{RDI_MAX_DETECTIONS}")
    if not math.isfinite(fps) or fps <= 0:
        raise SystemExit("fps must be a positive finite number")

    iso_payload_size = RDI_HEADER_SIZE + num_detections * RDI_DETECTION_SIZE
    protected_size = 20 + iso_payload_size
    needs_tp = protected_size > TP_CHUNK_SIZE
    est_segments = ((protected_size + TP_CHUNK_SIZE - 1) // TP_CHUNK_SIZE
                    if needs_tp else 1)

    print("=== RDI UDP Sender Simulator ===")
    print(f"Target:      {target_ip}:{target_port}")
    print(f"Detections:  {num_detections} per frame")
    print(f"Frame rate:  {fps:.1f} fps")
    print(f"Payload:     {iso_payload_size} ISO + 20 E2E bytes/frame")
    print(f"TP segments: {'Yes' if needs_tp else 'No'} ({est_segments} segments/frame)")
    print()
    print("Press Ctrl+C to stop.\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (target_ip, target_port)

    interval = 1.0 / fps
    msg_counter = 0
    session_id = 0
    frame_count = 0
    total_segments = 0
    start_time = time.monotonic()

    while g_running:
        loop_start = time.monotonic()

        timestamp_ns = time.time_ns()
        cycle_time_ns = min(int(round(1_000_000_000 / fps)), 0xFFFFFFFF)
        payload = build_rdi_payload(
            msg_counter, num_detections, timestamp_ns, cycle_time_ns)

        session_id = (session_id + 1) & 0xFFFF
        segs = send_rdi_frame_udp(
            sock, target, payload, session_id, msg_counter)

        msg_counter = (msg_counter + 1) & 0xFFFFFFFF
        frame_count += 1
        total_segments += segs

        # Print status roughly every two seconds.
        status_interval = max(1, int(fps * 2))
        if frame_count % status_interval == 0:
            elapsed = time.monotonic() - start_time
            actual_fps = frame_count / elapsed if elapsed > 0 else 0
            print(f"[Sent {frame_count} frames] {actual_fps:.1f} fps  "
                  f"segments={total_segments}  counter={msg_counter}")

        # Pace to target FPS
        elapsed_loop = time.monotonic() - loop_start
        sleep_time = interval - elapsed_loop
        if sleep_time > 0:
            time.sleep(sleep_time)

    sock.close()
    elapsed = time.monotonic() - start_time

    print("\n--- Summary ---")
    print(f"Duration:     {elapsed:.1f} seconds")
    print(f"Frames sent:  {frame_count}")
    print(f"Segments:     {total_segments}")
    if elapsed > 0:
        print(f"Actual FPS:   {frame_count / elapsed:.1f}")


if __name__ == "__main__":
    main()
