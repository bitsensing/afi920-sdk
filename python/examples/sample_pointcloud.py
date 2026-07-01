"""
Sample: Receive RDI (point cloud) data from AFI920 sensor

Supports TCP (default) and UDP transport modes.
TCP: connects to sensor and reads SOME/IP framed messages.
UDP: listens on local port for SOME/IP-TP segmented messages.

Protocol v3.0: the SOME/IP payload starts with a 20B E2E Profile 7
header (CRC-64/XZ), followed by the ISO 23150 RDI payload
(36B header + 51B per detection). Parsing lives in afi_sdk.streams.

Usage:
    python sample_pointcloud.py [sensor_ip] [tcp|udp]

Copyright 2026 bitsensing Inc. All rights reserved.
"""

import signal
import socket
import struct
import sys
import time
from typing import Dict

from afi_sdk.someip import (
    SOMEIP_HEADER_FMT, SOMEIP_HEADER_SIZE,
    MSG_TYPE_TP, DEFAULT_RDI_PORT,
)
from afi_sdk.streams import strip_e2e, parse_rdi_frame, parse_rdi_detection


# ── SOME/IP-TP Reassembler ──

class TpReassembler:
    """Reassemble SOME/IP-TP segmented messages (for RDI)."""

    def __init__(self):
        self._buffers: Dict[int, bytearray] = {}

    def feed(self, tp_header: bytes, payload: bytes,
             session_id: int) -> bytes:
        """Feed a TP segment. Returns complete payload when done, else b""."""
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
            return result

        return b""


# ── Main ──

g_running = True


def signal_handler(sig, frame):
    global g_running
    g_running = False


def recv_exact(sock, n):
    """Read exactly n bytes from TCP socket."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf


def main():
    global g_running
    signal.signal(signal.SIGINT, signal_handler)

    sensor_ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.150"
    transport = sys.argv[2] if len(sys.argv) > 2 else "tcp"
    port = DEFAULT_RDI_PORT

    print(f"Sensor: {sensor_ip}  Transport: {transport.upper()}")

    # Configure sensor protocol via Config API.
    # Protocol-only partial update: never touch the destination IP —
    # a unicast destination makes the sensor reject TCP clients connecting
    # from any other host IP (multi-NIC pitfall).
    try:
        from afi_sdk import AfiSdk, AfiSdkConfig
        sdk = AfiSdk()
        if sdk.init(AfiSdkConfig(sensor_ip=sensor_ip)) == 0:
            net = sdk.get_network_config() or {}
            if net.get("detection_protocol") != transport:
                sdk.set_network_config(detection_protocol=transport)
                print(f"Sensor detection stream switched to {transport.upper()}")
                time.sleep(1.0)  # stream channel reconnects after the change
            else:
                print(f"Sensor detection stream already {transport.upper()}")
            sdk.close()
    except Exception as e:
        print(f"Warning: could not configure sensor ({e})")

    if transport == "tcp":
        print(f"Connecting to {sensor_ip}:{port}... (Ctrl+C to stop)\n")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512 * 1024)
        sock.settimeout(5.0)
        sock.connect((sensor_ip, port))
    else:
        print(f"Listening on UDP:{port}... (Ctrl+C to stop)\n")
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        sock.bind(("", port))
        sock.settimeout(1.0)

    tp = TpReassembler()
    frame_count = 0
    total_detections = 0
    e2e_errors = 0
    prev_counter = -1
    lost_packets = 0
    total_packets = 0
    start = time.monotonic()

    while g_running:
        try:
            if transport == "tcp":
                # TCP: read SOME/IP framed message
                hdr_bytes = recv_exact(sock, 8)
                if len(hdr_bytes) < 8:
                    break
                length = struct.unpack(">I", hdr_bytes[4:8])[0]
                rest = recv_exact(sock, length)
                if len(rest) < length:
                    break
                data = hdr_bytes + rest
                frame_data = data[SOMEIP_HEADER_SIZE:]
            else:
                # UDP: receive datagram with TP reassembly
                data, addr = sock.recvfrom(2048)
                if len(data) < SOMEIP_HEADER_SIZE:
                    continue

                h = struct.unpack(SOMEIP_HEADER_FMT, data[:SOMEIP_HEADER_SIZE])
                msg_type = h[7]
                session_id = h[4]
                payload = data[SOMEIP_HEADER_SIZE:]

                is_tp = (msg_type & MSG_TYPE_TP) != 0
                if not is_tp:
                    continue
                if len(payload) < 4:
                    continue
                tp_hdr = payload[:4]
                tp_payload = payload[4:]
                frame_data = tp.feed(tp_hdr, tp_payload, session_id)
                if not frame_data:
                    continue
        except socket.timeout:
            continue
        except OSError:
            break

        # Strip + verify the E2E header ahead of the ISO payload
        e2e, iso_payload = strip_e2e(frame_data)
        if e2e is None:
            continue
        if not e2e.get("crc_ok", True):
            e2e_errors += 1

        # Got a complete RDI frame
        frame_count += 1
        hdr = parse_rdi_frame(iso_payload)
        if not hdr:
            continue

        num_det = hdr["num_detections"]
        total_detections += num_det

        # Packet loss detection
        counter = hdr["message_counter"]
        total_packets += 1
        if prev_counter >= 0:
            gap = counter - prev_counter - 1
            if 0 < gap < 1000:
                lost_packets += gap
                print(f"*** Packet loss detected: {gap} frames lost "
                      f"({lost_packets} lost / {total_packets + lost_packets} total "
                      f"= {100.0 * lost_packets / (total_packets + lost_packets):.2f}%)")
        prev_counter = counter

        # Print every 10th frame
        if frame_count % 10 == 0:
            print(f"[Frame {frame_count}] detections={num_det}  "
                  f"counter={counter}  timestamp={hdr['timestamp']} ns")

            if num_det > 0:
                det = parse_rdi_detection(iso_payload, hdr["det_start"], 0)
                if det:
                    print(f"  [0] r={det['radial_distance']:.2f} m  "
                          f"az={det['azimuth']:.3f} rad  "
                          f"el={det['elevation']:.3f} rad  "
                          f"v={det['radial_velocity']:.2f} m/s  "
                          f"rcs={det['rcs']:.1f} dBm2")

    sock.close()
    elapsed = time.monotonic() - start

    # Summary
    print("\n--- Summary ---")
    print(f"Duration:         {elapsed:.0f} seconds")
    print(f"Frames received:  {frame_count}")
    print(f"Total detections: {total_detections}")
    print(f"E2E CRC errors:   {e2e_errors}")
    if elapsed > 0:
        print(f"Avg frame rate:   {frame_count / elapsed:.1f} fps")


if __name__ == "__main__":
    main()
