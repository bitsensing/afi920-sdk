"""
Sample: Read and write sensor configuration via Config API

Connects to a sensor, reads all config categories, and demonstrates
non-destructive write-back tests.

Usage:
    python sample_config.py [sensor_ip] [config_port]

Copyright 2026 bitsensing Inc. All rights reserved.
"""

import sys
from afi_sdk import AfiSdk, AfiSdkConfig, RangeMode


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.150"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 30500

    config = AfiSdkConfig(sensor_ip=host, config_port=port)
    sdk = AfiSdk()

    print(f"Connecting to {host}:{port} ...")
    rc = sdk.init(config)
    if rc != 0:
        print(f"Init failed (rc={rc})")
        return

    # ── Device Info ──
    print("\n=== Device Info ===")

    info = sdk.get_device_info()
    if info:
        print(f"  Serial:     {info.get('serial_number', '?')}")
        print(f"  Model:      {info.get('model_name', '?')}")
        print(f"  HW Version: {info.get('hw_version', '?')}")
        print(f"  SW Version: {info.get('software_version', '?')}")
        print(f"  Hashes:     bl={info.get('bootloader_hash', '?')} "
              f"drv={info.get('driver_hash', '?')} fw={info.get('fw_hash', '?')}")
        print(f"  MAC:        {info.get('mac_address', '?')}")

    status = sdk.get_sensor_status()
    if status:
        print(f"  Op State:   {status.get('operation_state', '?')} "
              f"({status.get('operation_state_code', '?')})")
        print(f"  MCU Temp:   {status.get('temperature_mcu', '?')} C")
        print(f"  RF Temp:    {status.get('temperature_rf', '?')} C")
        print(f"  Voltage:    {status.get('voltage', '?')} V")
        print(f"  Uptime:     {status.get('uptime_sec', '?')} sec")

    diag = sdk.get_diagnostic_info()
    if diag:
        print(f"  Combined:   {diag.get('combined_state', '?')} "
              f"({diag.get('combined_state_code', '?')}), "
              f"active faults: {diag.get('active_fault_count', '?')}")
        for f in diag.get("faults", []):
            print(f"    [{f.get('source', '?')}] {f.get('id', '?')} "
                  f"{f.get('name', '?')} level={f.get('level', '?')} "
                  f"active={f.get('active', '?')}")

    # ── Network ──
    print("\n=== Network Config ===")

    net = sdk.get_network_config()
    if net:
        print(f"  Radar IP:   {net.get('ip_address', '?')}")
        print(f"  Subnet:     {net.get('subnet_mask', '?')}")
        print(f"  Gateway:    {net.get('gateway', '?')}")
        print(f"  API Port:   {net.get('api_port', '?')}")
        for stream in ("detection", "health", "performance"):
            print(f"  {stream.capitalize():11s} {net.get(f'{stream}_ip', '?')}:"
                  f"{net.get(f'{stream}_port', '?')} "
                  f"({net.get(f'{stream}_protocol', '?')})")

    # ── Range Mode ──
    print("\n=== Range Mode ===")

    mode = sdk.get_range_mode()
    if mode is not None:
        print(f"  Mode: {RangeMode.name(mode)} ({mode})")

    # ── Detection ──
    print("\n=== Detection ===")

    thresh = sdk.get_detection_threshold()
    if thresh:
        print(f"  SNR:  {thresh.get('snr_threshold_db', '?')} dB")
        print(f"  CFAR first:  {thresh.get('cfar_table_first_frame', '?')}")
        print(f"  CFAR second: {thresh.get('cfar_table_second_frame', '?')}")

    filters = sdk.get_detection_filters()
    if filters:
        for key, value in sorted(filters.items()):
            if key.startswith("filter_"):
                print(f"  {key[7:]:22s} {'ON' if value else 'OFF'}")

    density = sdk.get_detection_density()
    if density:
        print(f"  range_density: {'ON' if density.get('range_density') else 'OFF'}")
        print(f"  angle_density: {'ON' if density.get('angle_density') else 'OFF'}")

    # ── Time Sync ──
    print("\n=== Time Sync (PTP) ===")

    ptp = sdk.get_ptp_config()
    if ptp:
        print(f"  Profile: {ptp.get('profile', '?')} ({ptp.get('profile_name', '?')})")
        print(f"  State:   {ptp.get('state', '?')}, offset={ptp.get('offset_ns', '?')} ns, "
              f"master={ptp.get('master_clock_id', '?')}")

    # ── Mounting ──
    print("\n=== Mounting ===")

    pos = sdk.get_mounting_position()
    if pos:
        print(f"  Position: ({pos.get('offset_x_m', 0):.3f}, "
              f"{pos.get('offset_y_m', 0):.3f}, "
              f"{pos.get('offset_z_m', 0):.3f}) m")

    rot = sdk.get_rotation()
    if rot:
        print(f"  Rotation: roll={rot.get('roll_deg', 0):.1f} "
              f"pitch={rot.get('pitch_deg', 0):.1f} "
              f"yaw={rot.get('yaw_deg', 0):.1f} deg")

    # ── Persistence ──
    print("\n=== Config Persistence ===")

    cfg_status = sdk.get_config_status()
    if cfg_status:
        print(f"  Load status: {cfg_status.get('load_status_name', '?')} "
              f"({cfg_status.get('load_status', '?')})")

    # ── Write-back Test (non-destructive) ──
    print("\n--- Write-back Test ---")

    if mode is not None:
        rc = sdk.set_range_mode(mode)
        print(f"  SetRangeMode (same value): {'OK' if rc == 0 else f'FAIL ({rc})'}")

    if filters and "filter_fov" in filters:
        rc = sdk.set_detection_filters(filter_fov=filters["filter_fov"])
        print(f"  SetDetectionFilters (same value): {'OK' if rc == 0 else f'FAIL ({rc})'}")

    sdk.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
