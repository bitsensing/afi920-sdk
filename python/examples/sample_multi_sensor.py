"""
Sample: Connect to multiple AFI920 sensors and configure data ports

Demonstrates multi-sensor setup by assigning different UDP data ports
per sensor to avoid port conflicts, then verifying the configuration.

Usage:
    python sample_multi_sensor.py <sensor1_ip> <sensor2_ip> [...]

Copyright 2026 bitsensing Inc. All rights reserved.
"""

import argparse
import sys
from afi_sdk import AfiSdk, AfiSdkConfig, RangeMode


# Port offsets per sensor (sensor 0 uses defaults, sensor 1 uses +100, etc.)
PORT_OFFSET_PER_SENSOR = 100

# Default data ports
BASE_RDI_PORT = 30509
BASE_SHII_PORT = 30510
BASE_SPI_PORT = 30511


def main():
    parser = argparse.ArgumentParser(
        description="Connect to multiple AFI920 sensors and configure data ports")
    parser.add_argument("sensor_ips", nargs="+", metavar="SENSOR_IP",
                        help="Sensor IP addresses (e.g. 192.168.10.150 192.168.10.151)")
    parser.add_argument("--host-ip", required=True,
                        help="Host IP where data streams are received (UDP mode). "
                             "NOTE: a unicast destination also makes the sensor "
                             "accept TCP stream clients only from this IP — on a "
                             "multi-NIC host, pass the NIC on the sensor subnet.")
    args = parser.parse_args()

    sensor_ips = args.sensor_ips
    host_ip = args.host_ip

    print(f"AFI920 Multi-Sensor Setup ({len(sensor_ips)} sensors)")
    print(f"  Host IP: {host_ip}\n")

    for idx, ip in enumerate(sensor_ips):
        print(f"{'='*50}")
        print(f"  Sensor {idx + 1}: {ip}")
        print(f"{'='*50}")

        sdk = AfiSdk()
        config = AfiSdkConfig(sensor_ip=ip)

        if sdk.init(config) != 0:
            print("  Failed to connect. Skipping.\n")
            continue

        # Read device info
        info = sdk.get_device_info()
        if info:
            print(f"  Serial: {info.get('serial_number', '?')}")
            print(f"  Model:  {info.get('model_name', '?')}")

        mode = sdk.get_range_mode()
        if mode is not None:
            print(f"  Mode:   {RangeMode.name(mode)} ({mode})")

        # Calculate per-sensor data ports
        offset = idx * PORT_OFFSET_PER_SENSOR
        rdi_port = BASE_RDI_PORT + offset
        shii_port = BASE_SHII_PORT + offset
        spi_port = BASE_SPI_PORT + offset

        print("\n  Configuring data ports:")
        print(f"    RDI  -> {host_ip}:{rdi_port}")
        print(f"    SHII -> {host_ip}:{shii_port}")
        print(f"    SPI  -> {host_ip}:{spi_port}")

        # Stream destinations via unified SetNetworkConfig (partial update,
        # applies immediately — no restart)
        resp = sdk.set_network_config(
            detection_ip=host_ip, detection_port=rdi_port,
            health_ip=host_ip, health_port=shii_port,
            performance_ip=host_ip, performance_port=spi_port,
        )
        print(f"    Result: {'OK' if resp is not None else 'FAILED'}")

        # Verify
        verified = sdk.get_network_config()
        if verified:
            print("\n  Verified:")
            print(f"    Detection IP:   {verified.get('detection_ip', '?')}")
            print(f"    Detection Port: {verified.get('detection_port', '?')}")
            print(f"    Health Port:    {verified.get('health_port', '?')}")
            print(f"    Perf Port:      {verified.get('performance_port', '?')}")

        sdk.close()
        print()

    print("All sensors configured.")
    print("Start receiving data on the assigned ports per sensor.")


if __name__ == "__main__":
    main()
