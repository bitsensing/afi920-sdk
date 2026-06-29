"""
Set the mounting yaw of an AFI920 radar to 90 degrees (extrinsic orientation).

Reads the current rotation, overwrites only yaw (keeping pitch/roll), writes it
back, and persists to NVM. The new pose is what the sensor reports over SPI and
what the ROS 2 driver broadcasts as TF (parent_frame_id -> frame_id).

Usage:
    python set_yaw_90.py [sensor_ip] [yaw_deg]

Copyright 2026 bitsensing Inc. All rights reserved.
"""

import sys
from afi_sdk import AfiSdk, AfiSdkConfig


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.150"
    yaw_deg = float(sys.argv[2]) if len(sys.argv) > 2 else 90.0

    sdk = AfiSdk()
    print(f"Connecting to {host}:30500 ...")
    if sdk.init(AfiSdkConfig(sensor_ip=host)) != 0:
        print("Init failed — check power, IP, and that port 30500 is reachable.")
        return 1

    try:
        cur = sdk.get_rotation()
        if not cur:
            print("GetRotation failed.")
            return 1
        print(f"  current: roll={cur['roll_deg']:.1f} "
              f"pitch={cur['pitch_deg']:.1f} yaw={cur['yaw_deg']:.1f} deg")

        # Overwrite yaw only; keep existing pitch/roll.
        rc = sdk.set_rotation(yaw=yaw_deg,
                              pitch=cur["pitch_deg"],
                              roll=cur["roll_deg"])
        if rc != 0:
            print(f"SetRotation failed (rc={rc}).")
            return 1

        # Read back to confirm before persisting.
        new = sdk.get_rotation()
        print(f"  new:     roll={new['roll_deg']:.1f} "
              f"pitch={new['pitch_deg']:.1f} yaw={new['yaw_deg']:.1f} deg")

        if sdk.save_config() != 0:
            print("SaveConfig failed — change is live but will be lost on reboot.")
            return 1
        print("Saved to NVM. Done.")
        return 0
    finally:
        sdk.close()


if __name__ == "__main__":
    sys.exit(main())
