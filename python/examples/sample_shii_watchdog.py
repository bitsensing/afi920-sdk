"""
Sample: SHII health watchdog — restart the sensor on repeated warnings.

Receives the SHII (Sensor Health) stream and inspects each frame. When the
sensor reports a health warning (defect flagged, or supply-voltage / temperature
/ time-sync not nominal) for several consecutive frames, the watchdog calls the
Config API Restart (0x0014). This is a template for "self-healing" supervision;
tune the warning condition and threshold to your deployment.

Usage:
    python sample_shii_watchdog.py [sensor_ip] [consecutive_threshold]

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import socket
import struct
import sys
import time

from afi_sdk import AfiSdk, AfiSdkConfig
from afi_sdk.someip import SOMEIP_HEADER_SIZE, DEFAULT_SHII_PORT
from afi_sdk.streams import strip_e2e, parse_shi

# SHII status enums have different "nominal" values per field (see
# bts_iso23150_shi.h): defect_recognised 0 = NO_DEFECT; supply_voltage_status
# 2 = WITHIN_LIMITS (0/1 low, 3/4 high); temperature_status 2 = IN_LIMITS
# (0/1 under, 3/4 over). Time sync is intentionally NOT treated as a warning
# here — it is commonly NOT_SYNCHRONIZED until PTP locks and should not trigger
# a restart.
DEFECT_NONE = 0
VOLTAGE_OK = 2
TEMPERATURE_OK = 2

# Human-readable labels for the SHII status enums (bts_iso23150_shi.h).
DEFECT_RECOGNISED_LABELS = {
    0: "FULLY_FUNCTIONAL", 1: "NOT_FULLY_FUNCTIONAL", 2: "OUT_OF_ORDER",
}
DEFECT_REASON_LABELS = {
    0: "NO_DEFECT", 1: "INTERNAL_MEMORY_ERROR", 2: "HW_DEFECT",
    3: "THERMAL_DEFECT", 4: "COMMUNICATION_ERROR", 5: "CALIBRATION_ERROR",
    6: "CONFIGURATION_ERROR", 7: "MECHANICAL_DEFECT", 8: "SOFTWARE_DEFECT",
    9: "COMPUTING_POWER", 10: "OUT_OF_TIME_SYNC", 11: "EXTERNALLY_DISTURBED",
}
VOLTAGE_LABELS = {
    0: "LOW", 1: "PRE_LOW", 2: "WITHIN_LIMITS", 3: "PRE_HIGH", 4: "HIGH",
}
TEMPERATURE_LABELS = {
    0: "UNDER_TEMPERATURE", 1: "PRE_UNDER_TEMPERATURE", 2: "IN_LIMITS",
    3: "PRE_OVER_TEMPERATURE", 4: "OVER_TEMPERATURE",
}


def _label(mapping: dict, value) -> str:
    return mapping.get(value, f"UNKNOWN({value})")


def is_warning(shi: dict) -> bool:
    """Return True if this SHII frame indicates a health warning."""
    return (shi.get("defect_recognised", DEFECT_NONE) != DEFECT_NONE
            or shi.get("supply_voltage_status", VOLTAGE_OK) != VOLTAGE_OK
            or shi.get("temperature_status", TEMPERATURE_OK) != TEMPERATURE_OK)


def health_str(shi: dict) -> str:
    """Human-readable one-line health summary from a SHII frame."""
    defect = _label(DEFECT_RECOGNISED_LABELS, shi.get("defect_recognised", 0))
    reason = shi.get("defect_reason", 0)
    if reason:
        defect += f"/{_label(DEFECT_REASON_LABELS, reason)}"
    return (f"defect={defect}, "
            f"voltage={_label(VOLTAGE_LABELS, shi.get('supply_voltage_status', 2))}, "
            f"temperature={_label(TEMPERATURE_LABELS, shi.get('temperature_status', 2))}")


def _recv_exact(sock, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf


def read_shii_frame(sock):
    """Read one SOME/IP-framed SHII message over TCP; return the ISO payload."""
    hdr = _recv_exact(sock, 8)                       # MessageID(4) + Length(4)
    if len(hdr) < 8:
        return None
    length = struct.unpack(">I", hdr[4:8])[0]
    rest = _recv_exact(sock, length)
    if len(rest) < length:
        return None
    data = hdr + rest
    if len(data) < SOMEIP_HEADER_SIZE:
        return None
    _e2e, payload = strip_e2e(data[SOMEIP_HEADER_SIZE:])   # drop 20B E2E header
    return payload


def main() -> int:
    sensor_ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.150"
    threshold = int(sys.argv[2]) if len(sys.argv) > 2 else 5

    print("AFI920 SHII Health Watchdog")
    print(f"  Sensor:    {sensor_ip}")
    print(f"  Restart after {threshold} consecutive warning frames\n")

    streak = 0
    frames = 0
    while True:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.settimeout(5.0)
        try:
            sock.connect((sensor_ip, DEFAULT_SHII_PORT))
        except OSError as e:
            print(f"  SHII connect failed ({e}); retrying in 2s...")
            sock.close()
            time.sleep(2.0)
            continue

        print(f"  Connected to SHII stream ({sensor_ip}:{DEFAULT_SHII_PORT})")
        try:
            while True:
                payload = read_shii_frame(sock)
                if payload is None:
                    print("  SHII stream closed; reconnecting...")
                    break
                shi = parse_shi(payload)
                if shi is None:
                    continue

                frames += 1
                # Periodic heartbeat: print health status every 20 frames.
                if frames % 20 == 1:
                    state = "WARNING" if is_warning(shi) else "OK"
                    print(f"  [frame {frames}] {state}: {health_str(shi)}")

                if is_warning(shi):
                    streak += 1
                    print(f"  WARNING #{streak}: {health_str(shi)}")
                    if streak >= threshold:
                        print(f"\n  {streak} consecutive warnings — restarting sensor...")
                        restart_sensor(sensor_ip)
                        streak = 0
                        break   # reconnect after the restart
                elif streak:
                    print(f"  recovered after {streak} warning(s)")
                    streak = 0
        except socket.timeout:
            print("  SHII stream idle (timeout); reconnecting...")
        except KeyboardInterrupt:
            print("\n  Stopped.")
            sock.close()
            return 0
        finally:
            sock.close()

        time.sleep(1.0)   # brief pause before reconnecting


def restart_sensor(sensor_ip: str) -> None:
    """Call Config API Restart (0x0014)."""
    sdk = AfiSdk()
    try:
        if sdk.init(AfiSdkConfig(sensor_ip=sensor_ip)) == 0:
            rc = sdk.restart()
            print(f"  restart() rc={rc} — sensor rebooting, connection will drop")
        else:
            print("  could not connect to Config API to restart")
    finally:
        sdk.close()


if __name__ == "__main__":
    sys.exit(main())
