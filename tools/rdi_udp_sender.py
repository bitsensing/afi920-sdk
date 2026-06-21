"""
RDI UDP Sender Simulator — DEF-10 디버깅용

레이더가 RDI UDP를 지원하지 않으므로, 이 스크립트가 레이더 역할을 대신하여
SOME/IP-TP 세그먼트된 RDI 프레임을 UDP로 전송합니다.

사용법:
    python rdi_udp_sender.py [target_ip] [target_port] [num_detections] [fps]

    target_ip:       수신측 IP (default: 127.0.0.1)
    target_port:     RDI UDP 포트 (default: 30509)
    num_detections:  프레임당 detection 수 (default: 50)
    fps:             전송 프레임 레이트 (default: 20)

예시:
    # 로컬 테스트 (sample_pointcloud UDP 모드와 함께)
    python rdi_udp_sender.py 127.0.0.1 30509 50 20

    # 다른 PC의 SDK로 전송
    python rdi_udp_sender.py 192.168.10.100 30509 100 10

Copyright 2026 bitsensing Inc. All rights reserved.
"""

import math
import random
import signal
import socket
import struct
import sys
import time

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

# ── RDI Constants ──

RDI_HEADER_SIZE = 61      # Common header (24B) + Ambiguity domain (32B) + Det info (5B)
RDI_DETECTION_SIZE = 94   # Per-detection size (ISO 23150)


def build_rdi_payload(msg_counter: int, num_detections: int,
                      timestamp_ns: int) -> bytes:
    """ISO 23150 RDI 프레임 페이로드 생성 (little-endian)."""
    buf = bytearray()

    # ── Common Interface Header (24 bytes) ──
    buf.append(1)   # major version
    buf.append(0)   # minor version
    buf.append(0)   # patch version
    buf.append(0x08) # interface_id = RDI
    buf.append(1)   # num_serving_sensors
    buf.append(0x01) # sensor_id

    buf.extend(struct.pack("<Q", timestamp_ns))       # timestamp (8B LE)
    buf.extend(struct.pack("<I", msg_counter))         # message_counter (4B LE)
    buf.extend(struct.pack("<I", 50_000_000))          # cycle_time 50ms in ns (4B LE)
    buf.append(0)    # cycle_time_variation
    buf.append(0x01) # data_qualifier (VALID)

    # ── Ambiguity Domain (32 bytes: 8 floats LE) ──
    buf.extend(struct.pack("<f", -60.0))    # velocity_begin
    buf.extend(struct.pack("<f",  60.0))    # velocity_end
    buf.extend(struct.pack("<f",   0.3))    # range_begin
    buf.extend(struct.pack("<f", 300.0))    # range_end
    buf.extend(struct.pack("<f", -1.22))    # azimuth_begin (~-70 deg)
    buf.extend(struct.pack("<f",  1.22))    # azimuth_end (~+70 deg)
    buf.extend(struct.pack("<f", -0.26))    # elevation_begin (~-15 deg)
    buf.extend(struct.pack("<f",  0.26))    # elevation_end (~+15 deg)

    # ── Detection Info (5 bytes) ──
    buf.extend(struct.pack("<H", 4096))      # recognised_detections_capability
    buf.append(0x01)                          # recognised_detections_status
    buf.extend(struct.pack("<H", num_detections))  # num_valid_detections

    assert len(buf) == RDI_HEADER_SIZE, f"Header size mismatch: {len(buf)} != {RDI_HEADER_SIZE}"

    # ── Detections (94 bytes each) ──
    for i in range(num_detections):
        det = bytearray(RDI_DETECTION_SIZE)

        # Simulate radar targets at various ranges/angles
        angle = 2.0 * math.pi * i / max(num_detections, 1)
        range_m = 5.0 + 50.0 * random.random()
        azimuth = 0.8 * math.sin(angle) + random.gauss(0, 0.02)
        elevation = 0.05 * math.sin(angle * 2) + random.gauss(0, 0.01)
        velocity = -20.0 + 40.0 * random.random()
        rcs = -10.0 + 30.0 * random.random()
        snr = 5.0 + 20.0 * random.random()

        # Status (9B)
        det[0] = min(255, int(80 + random.random() * 175))  # existence_prob
        struct.pack_into("<H", det, 1, i & 0xFFFF)           # detection_id
        struct.pack_into("<H", det, 3, 0)                    # detection_level
        struct.pack_into("<f", det, 5, 0.0)                  # timestamp_diff

        # Signal Quality (16B)
        struct.pack_into("<f", det, 9, rcs)                  # RCS
        struct.pack_into("<f", det, 13, 1.0)                 # RCS error
        struct.pack_into("<f", det, 17, snr)                 # SNR
        struct.pack_into("<f", det, 21, 0.5)                 # SNR error

        # Probabilities
        det[25] = 10                                          # multi_target_prob
        struct.pack_into("<H", det, 26, 0)                   # ambiguity_grouping_id
        det[28] = 5                                           # ambiguity_prob
        det[29] = 200                                         # free_space_prob

        # Classification (18B)
        det[30] = 0  # num_classifications

        # Position (24B)
        struct.pack_into("<f", det, 47, range_m)             # radial_distance
        struct.pack_into("<f", det, 51, azimuth)             # azimuth
        struct.pack_into("<f", det, 55, elevation)           # elevation
        struct.pack_into("<f", det, 59, 0.1)                 # distance_error
        struct.pack_into("<f", det, 63, 0.01)                # azimuth_error
        struct.pack_into("<f", det, 67, 0.02)                # elevation_error

        # Velocity (8B)
        struct.pack_into("<f", det, 71, velocity)            # radial_velocity
        struct.pack_into("<f", det, 75, 0.2)                 # velocity_error

        # Debug (15B) — zeros ok

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
                       rdi_payload: bytes, session_id: int):
    """SOME/IP-TP 세그먼트화 후 UDP 전송.

    payload > TP_CHUNK_SIZE 일 때 자동으로 TP 세그먼트 분할.
    payload <= TP_CHUNK_SIZE 일 때는 단일 SOME/IP 패킷으로 전송.
    """
    if len(rdi_payload) <= TP_CHUNK_SIZE:
        # Single SOME/IP message (no TP)
        length = 8 + len(rdi_payload)  # SOME/IP length field = 8B (header rest) + payload
        hdr = build_someip_header(EVENT_RDI, length, session_id, MSG_TYPE_NOTIFY)
        sock.sendto(hdr + rdi_payload, target)
        return 1

    # SOME/IP-TP segmentation
    offset = 0
    segment_count = 0

    while offset < len(rdi_payload):
        remaining = len(rdi_payload) - offset
        chunk_len = min(TP_CHUNK_SIZE, remaining)
        more_segments = 1 if (offset + chunk_len < len(rdi_payload)) else 0

        # TP header: offset in 16-byte units (bits 31:4) | more_segments (bit 0)
        # offset must be aligned to 16 bytes (except possibly the last segment)
        offset_units = offset // 16
        tp_word = (offset_units << 4) | more_segments
        tp_header = struct.pack(">I", tp_word)

        # SOME/IP length = 8 (rest of someip header) + 4 (TP header) + chunk_len
        length = 8 + 4 + chunk_len
        someip_hdr = build_someip_header(EVENT_RDI, length, session_id,
                                         MSG_TYPE_TP_NOTIFY)

        packet = someip_hdr + tp_header + rdi_payload[offset:offset + chunk_len]
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
    fps = float(sys.argv[4]) if len(sys.argv) > 4 else 20.0

    payload_size = RDI_HEADER_SIZE + num_detections * RDI_DETECTION_SIZE
    needs_tp = payload_size > TP_CHUNK_SIZE
    est_segments = (payload_size + TP_CHUNK_SIZE - 1) // TP_CHUNK_SIZE if needs_tp else 1

    print(f"=== RDI UDP Sender Simulator ===")
    print(f"Target:      {target_ip}:{target_port}")
    print(f"Detections:  {num_detections} per frame")
    print(f"Frame rate:  {fps:.1f} fps")
    print(f"Payload:     {payload_size} bytes/frame")
    print(f"TP segments: {'Yes' if needs_tp else 'No'} ({est_segments} segments/frame)")
    print(f"")
    print(f"Press Ctrl+C to stop.\n")

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

        timestamp_ns = int(time.time() * 1_000_000_000)
        payload = build_rdi_payload(msg_counter, num_detections, timestamp_ns)

        session_id = (session_id + 1) & 0xFFFF
        segs = send_rdi_frame_udp(sock, target, payload, session_id)

        msg_counter += 1
        frame_count += 1
        total_segments += segs

        # Print status every 10 frames
        if frame_count % int(fps * 2) == 0:
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

    print(f"\n--- Summary ---")
    print(f"Duration:     {elapsed:.1f} seconds")
    print(f"Frames sent:  {frame_count}")
    print(f"Segments:     {total_segments}")
    if elapsed > 0:
        print(f"Actual FPS:   {frame_count / elapsed:.1f}")


if __name__ == "__main__":
    main()
