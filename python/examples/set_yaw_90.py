"""
Sample: Set the mounting yaw of an AFI920 radar (extrinsic orientation)

Reads the current rotation, overwrites only yaw (keeping pitch/roll), and
writes it in the diagnostic access mode. The sensor stores the value in NVM
when it answers; it takes effect after a restart, which this sample performs.
The new pose is what the sensor reports over SPI and what the ROS 2 driver
broadcasts as TF (parent_frame_id -> frame_id).

Usage:
    python set_yaw_90.py [sensor_ip] [yaw_deg]

Copyright 2026 bitsensing Inc. All rights reserved.
"""

import sys
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, OK


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.150"
    yaw_deg = float(sys.argv[2]) if len(sys.argv) > 2 else 90.0

    sdk = AfiSdk()
    print(f"Connecting to {host}:30500 ...")
    if sdk.init(AfiSdkConfig(sensor_ip=host)) != OK:
        print("Init failed. Check power, IP, and that port 30500 is reachable.")
        return 1

    try:
        cur = sdk.get_rotation()
        if not cur:
            print(f"GetRotation failed (err={sdk.last_error}).")
            return 1
        print(f"  current: roll={cur['roll_deg']:.1f} "
              f"pitch={cur['pitch_deg']:.1f} yaw={cur['yaw_deg']:.1f} deg")

        # Every config SET needs the diagnostic access mode.
        rc = sdk.enter_mode(AccessMode.DIAGNOSTIC)
        if rc != OK:
            print(f"Entering the diagnostic mode failed (rc={rc}).")
            return 1

        # Overwrite yaw only; keep the existing pitch/roll.
        rc = sdk.set_rotation(yaw=yaw_deg, pitch=cur["pitch_deg"], roll=cur["roll_deg"])
        if rc != OK:
            print(f"SetRotation failed (rc={rc}).")
            return 1

        # A GET returns the stored value, which runs after the restart.
        new = sdk.get_rotation() or {}
        print(f"  stored:  roll={new.get('roll_deg', 0.0):.1f} "
              f"pitch={new.get('pitch_deg', 0.0):.1f} yaw={new.get('yaw_deg', 0.0):.1f} deg")

        if sdk.restart() != OK or sdk.reconnect(timeout_s=60.0) != OK:
            print("Restart failed. The value is stored and applies at the next restart.")
            return 1
        print("Restarted; the new yaw is active. Done.")
        return 0
    finally:
        sdk.close()


if __name__ == "__main__":
    sys.exit(main())
