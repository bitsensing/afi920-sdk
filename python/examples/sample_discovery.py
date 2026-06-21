"""
Sample: Discover AFI920 sensors on the network (multi-LAN)

Enumerates all local network interfaces and sends a UDP broadcast
Discovery request from each one, collecting responses across all LANs.

Usage:
    python sample_discovery.py [timeout_sec] [bind_ip]

    If bind_ip is given, discovery is limited to that single interface.
    Otherwise, all non-loopback interfaces are scanned automatically.

Copyright 2026 bitsensing Inc. All rights reserved.
"""

import socket
import struct
import sys
from afi_sdk import AfiSdk, RangeMode


def enumerate_local_ipv4() -> list[dict[str, str]]:
    """Return local non-loopback IPv4 interfaces as [{"ip": ..., "name": ...}]."""
    seen: set[str] = set()
    results: list[dict[str, str]] = []

    # Linux: fcntl + SIOCGIFADDR (catches USB/external adapters)
    if sys.platform != "win32":
        try:
            import fcntl

            for _idx, name in socket.if_nameindex():
                try:
                    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    ip_bytes = fcntl.ioctl(
                        s.fileno(), 0x8915,  # SIOCGIFADDR
                        struct.pack("256s", name.encode("utf-8")[:15]),
                    )[20:24]
                    s.close()
                    ip_str = socket.inet_ntoa(ip_bytes)
                    if not ip_str.startswith("127.") and ip_str not in seen:
                        seen.add(ip_str)
                        results.append({"ip": ip_str, "name": name})
                except OSError:
                    pass
        except (ImportError, OSError):
            pass

    # Fallback: getaddrinfo
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip_str = str(info[4][0])
            if not ip_str.startswith("127.") and ip_str not in seen:
                seen.add(ip_str)
                results.append({"ip": ip_str, "name": ""})
    except OSError:
        pass

    return results


def print_sensor(idx: int, s: dict):
    """Print a single sensor's discovery info."""
    mode = s.get("range_mode", -1)
    print(f"[Sensor {idx}]")
    print(f"  IP:             {s.get('ip_address', s.get('_source_ip', '?'))}:"
          f"{s.get('api_port', s.get('_source_port', '?'))}")
    print(f"  Serial:         {s.get('serial_number', '?')}")
    print(f"  Model:          {s.get('model_name', '?')}")
    print(f"  SW Version:     {s.get('software_version', '?')} "
          f"(bl={s.get('bootloader_hash', '?')} drv={s.get('driver_hash', '?')} "
          f"fw={s.get('fw_hash', '?')})")
    print(f"  HW Version:     {s.get('hw_version', '?')}")
    print(f"  MAC:            {s.get('mac_address', '?')}")
    print(f"  Range Mode:     {s.get('range_mode_name', RangeMode.name(mode))} ({mode})")
    print(f"  Position Code:  {s.get('position_code', '?')}")
    print(f"  Mounting:       ({s.get('offset_x_m', 0):.3f}, "
          f"{s.get('offset_y_m', 0):.3f}, {s.get('offset_z_m', 0):.3f}) m")
    print(f"  Ports:          det={s.get('detection_port', '?')} "
          f"health={s.get('health_port', '?')} perf={s.get('performance_port', '?')}")
    scan = s.get("scan_status")
    if isinstance(scan, dict):
        print(f"  Scan:           index={scan.get('scan_index', '?')} "
              f"uptime={scan.get('uptime_seconds', '?')}s")
    if "ptp_state" in s:
        print(f"  PTP:            {s.get('ptp_state', '?')} "
              f"offset={s.get('ptp_offset_ns', '?')} ns")
    print()


def main():
    timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
    bind_ip = sys.argv[2] if len(sys.argv) > 2 else ""

    # Determine interfaces to scan
    if bind_ip:
        interfaces = [{"ip": bind_ip, "name": "user-specified"}]
    else:
        interfaces = enumerate_local_ipv4()
        if not interfaces:
            interfaces = [{"ip": "", "name": "default"}]

    print(f"Discovering AFI920 sensors (timeout: {timeout}s, "
          f"{len(interfaces)} interface(s))...\n")

    # Discover on each interface, dedup by (ip, serial)
    all_sensors: list[dict] = []
    seen_keys: set[tuple[str, str]] = set()

    for iface in interfaces:
        ip = iface["ip"]
        name = iface.get("name", "")
        label = f"{name} ({ip})" if name else ip or "any"
        print(f"  Scanning on {label} ...")

        found = AfiSdk.discover(timeout_s=timeout, bind_ip=ip)

        for s in found:
            key = (
                s.get("ip_address", s.get("_source_ip", "")),
                s.get("serial_number", ""),
            )
            if key not in seen_keys:
                seen_keys.add(key)
                all_sensors.append(s)

        print(f"    -> {len(found)} response(s)")

    print(f"\nTotal unique sensors found: {len(all_sensors)}\n")

    if not all_sensors:
        print("No sensors found.")
        return

    for i, s in enumerate(all_sensors):
        print_sensor(i + 1, s)


if __name__ == "__main__":
    main()
