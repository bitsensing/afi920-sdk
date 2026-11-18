"""
Sample: Receive RDI (point cloud) data from AFI920 sensor

Supports TCP (default) and UDP transport modes.
TCP: connects to sensor and reads SOME/IP framed messages.
UDP: listens on local port for SOME/IP datagrams.

Interface v1.0.x and v1.1.0 send one RDI message per scan (SOME/IP-TP over
UDP); interface v1.2.0 sends header and chunk messages. RdiStreamDecoder
(afi_sdk.streams) reads both and returns one ISO 23150 RDI payload
(36B header + 51B per detection) per scan.

Usage:
    python sample_pointcloud.py [sensor_ip] [tcp|udp]

Copyright 2026 bitsensing Inc. All rights reserved.
"""

import signal
import socket
import struct
import sys
import time

from afi_sdk.someip import SOMEIP_HEADER_SIZE, DEFAULT_RDI_PORT
from afi_sdk.streams import RdiStreamDecoder, parse_rdi_frame, parse_rdi_detection


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

    # Select the sensor's detection transport via the Config API.
    # Protocol-only partial update: never touch the destination IP —
    # a unicast destination makes the sensor reject TCP clients connecting
    # from any other host IP (multi-NIC pitfall). The SET needs the
    # diagnostic access mode, is stored in NVM and applies after a restart.
    try:
        from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, OK
        sdk = AfiSdk()
        if sdk.init(AfiSdkConfig(sensor_ip=sensor_ip)) == OK:
            net = sdk.get_network_config() or {}
            if net.get("detection_protocol") == transport:
                print(f"Sensor detection stream already {transport.upper()}")
            elif (sdk.enter_mode(AccessMode.DIAGNOSTIC) != OK
                  or sdk.set_network_config(detection_protocol=transport) is None):
                print(f"Warning: could not switch the detection stream to "
                      f"{transport.upper()} (err={sdk.last_error})")
            elif sdk.restart() != OK or sdk.reconnect(timeout_s=60.0) != OK:
                print(f"Warning: {transport.upper()} is stored but the restart "
                      f"failed (err={sdk.last_error}); it applies at the next restart")
            else:
                print(f"Sensor detection stream switched to {transport.upper()} "
                      f"(sensor restarted)")
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

    decoder = RdiStreamDecoder()
    frame_count = 0
    partial_scans = 0
    total_detections = 0
    prev_e2e_counter = -1
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
            else:
                data, addr = sock.recvfrom(2048)
            # One message in; zero or more complete scans out.
            scans = decoder.feed(data) if len(data) >= SOMEIP_HEADER_SIZE else []
        except socket.timeout:
            scans = []
        except OSError:
            break
        # A v1.2.0 scan whose header B never came closes after 100 ms.
        scans += decoder.poll()

        for scan in scans:
            iso_payload = scan["payload"]
            hdr = parse_rdi_frame(iso_payload)
            if not hdr:
                continue
            frame_count += 1
            if not scan["complete"]:
                partial_scans += 1

            num_det = hdr["num_detections"]
            total_detections += num_det

            # Loss detection on the E2E Counter, which advances once per send
            # slot. MessageCounter repeats when the sensor resends a scan
            # (DataQualifier 6), so its gaps do not count lost frames. Only
            # scans that passed the E2E check advance the count; a decrease
            # (sensor restart) starts a new baseline.
            counter = hdr["message_counter"]
            total_packets += 1
            if scan["e2e_valid"]:
                e2e_counter = scan["e2e_counter"]
                if prev_e2e_counter >= 0 and e2e_counter > prev_e2e_counter:
                    gap = e2e_counter - prev_e2e_counter - 1
                    if gap > 0:
                        lost_packets += gap
                        print(f"*** Packet loss detected: {gap} frames lost "
                              f"({lost_packets} lost / {total_packets + lost_packets} total "
                              f"= {100.0 * lost_packets / (total_packets + lost_packets):.2f}%)")
                prev_e2e_counter = e2e_counter

            # Print every 10th frame
            if frame_count % 10 == 0:
                print(f"[Frame {frame_count}] detections={num_det}"
                      f"/{scan['announced_detections']}  "
                      f"counter={counter}  timestamp={hdr['timestamp']} ns")

                if num_det > 0:
                    det = parse_rdi_detection(iso_payload, hdr["det_start"], 0)
                    if det:
                        print(f"  [0] r={det['radial_distance']:.2f} m  "
                              f"az={det['azimuth']:.3f} rad  "
                              f"el={det['elevation']:.3f} rad  "
                              f"v={det['radial_velocity']:.2f} m/s  "
                              f"rcs={det['rcs']:.1f} dBsm")

    sock.close()
    elapsed = time.monotonic() - start

    # Summary
    print("\n--- Summary ---")
    print(f"Duration:         {elapsed:.0f} seconds")
    print(f"Frames received:  {frame_count}")
    print(f"Total detections: {total_detections}")
    print(f"E2E check errors: {decoder.e2e_failures}")
    print(f"Partial scans:    {partial_scans}")
    print(f"Frames lost:      {lost_packets}")
    if elapsed > 0:
        print(f"Avg frame rate:   {frame_count / elapsed:.1f} fps")


if __name__ == "__main__":
    main()
