"""
Sample: back up and restore sensor configuration.

--save reads the sensor's tunable configuration via the Config API and writes it
to a JSON file. --restore reads that JSON back and re-applies the tunable
settings, then persists them to NVM (SaveConfig). Handy for cloning a known-good
configuration across sensors or capturing a baseline before experiments.

Network config and device info are captured for reference but never re-applied
(restoring an IP address could make the sensor unreachable).

Usage:
    python sample_config_backup.py <sensor_ip> --save   config.json
    python sample_config_backup.py <sensor_ip> --restore config.json

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import argparse
import json
import sys

from afi_sdk import AfiSdk, AfiSdkConfig, OK


def backup(sdk: AfiSdk) -> dict:
    """Read all tunable config (+ reference info) into a plain dict."""
    ptp = sdk.get_ptp_config() or {}
    snapshot = {
        "range_mode": sdk.get_range_mode(),
        "detection_threshold": sdk.get_detection_threshold(),
        "detection_filters": sdk.get_detection_filters(),
        "detection_density": sdk.get_detection_density(),
        "mounting_position": sdk.get_mounting_position(),
        "rotation": sdk.get_rotation(),
        "sensor_position": sdk.get_sensor_position(),
        "ptp_profile": ptp.get("profile"),
        # Reference only — NOT re-applied on restore.
        "_reference": {
            "device_info": sdk.get_device_info(),
            "network_config": sdk.get_network_config(),
        },
    }
    return snapshot


def restore(sdk: AfiSdk, snap: dict) -> bool:
    """Apply the tunable settings from a snapshot. Returns True if all ok."""
    ok = True

    def _apply(label, rc):
        nonlocal ok
        good = (rc == OK)
        print(f"  {label:<22} {'ok' if good else f'FAILED (rc={rc})'}")
        ok = ok and good

    if snap.get("range_mode") is not None:
        _apply("range_mode", sdk.set_range_mode(snap["range_mode"]))

    dt = snap.get("detection_threshold") or {}
    if dt:
        _apply("detection_threshold", sdk.set_detection_threshold(
            snr_db=dt.get("snr_threshold_db"),
            cfar_table_first_frame=dt.get("cfar_table_first_frame"),
            cfar_table_second_frame=dt.get("cfar_table_second_frame"),
        ))

    filters = {k: v for k, v in (snap.get("detection_filters") or {}).items()
               if k.startswith("filter_")}
    if filters:
        _apply("detection_filters", sdk.set_detection_filters(**filters))

    dd = snap.get("detection_density") or {}
    if dd:
        _apply("detection_density", sdk.set_detection_density(
            range_density=dd.get("range_density"),
            angle_density=dd.get("angle_density"),
        ))

    mp = snap.get("mounting_position") or {}
    if mp:
        _apply("mounting_position", sdk.set_mounting_position(
            mp.get("offset_x_m", 0.0), mp.get("offset_y_m", 0.0),
            mp.get("offset_z_m", 0.0)))

    rot = snap.get("rotation") or {}
    if rot:
        _apply("rotation", sdk.set_rotation(
            rot.get("yaw_deg", 0.0), rot.get("pitch_deg", 0.0),
            rot.get("roll_deg", 0.0)))

    if snap.get("sensor_position") is not None:
        _apply("sensor_position", sdk.set_sensor_position(snap["sensor_position"]))

    if snap.get("ptp_profile") is not None:
        _apply("ptp_profile", sdk.set_ptp_config(snap["ptp_profile"]))

    # Persist to NVM so the restored config survives a reboot.
    _apply("save_config (NVM)", sdk.save_config())
    return ok


def main() -> int:
    p = argparse.ArgumentParser(description="Back up / restore AFI920 configuration.")
    p.add_argument("sensor_ip")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--save", metavar="FILE", help="read config and write it to FILE")
    g.add_argument("--restore", metavar="FILE", help="apply config from FILE")
    args = p.parse_args()

    sdk = AfiSdk()
    if sdk.init(AfiSdkConfig(sensor_ip=args.sensor_ip)) != OK:
        print(f"Failed to connect to {args.sensor_ip}", file=sys.stderr)
        return 1
    try:
        if args.save:
            snap = backup(sdk)
            with open(args.save, "w", encoding="utf-8") as f:
                json.dump(snap, f, indent=2)
            print(f"Saved configuration to {args.save}")
            return 0
        else:
            with open(args.restore, "r", encoding="utf-8") as f:
                snap = json.load(f)
            print(f"Restoring configuration from {args.restore}:")
            ok = restore(sdk, snap)
            print("Done." if ok else "Completed with errors.")
            return 0 if ok else 1
    finally:
        sdk.close()


if __name__ == "__main__":
    sys.exit(main())
