# AFI920 SDK

> **[한국어 (Korean)](README_ko.md)**

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-blue)
![Python](https://img.shields.io/badge/python-3.8%2B-blue)
![License](https://img.shields.io/badge/license-BSD--3--Clause-green)

C++ and Python SDK for the bitsensing **AFI920 4D Imaging Radar**.  
Covers sensor discovery, RDI/SHII/SPI data reception, CSII vehicle input, and key configuration APIs.

> **Version 3.0.0** — tracks AFI920 protocol v3.0. Sensors running v3.0 firmware require this SDK; earlier sensors require SDK v2.x. See [CHANGELOG.md](CHANGELOG.md).

> **bitsensing** — [bitsensing.com](https://bitsensing.com)

---

## Scope

- Public C++ and Python SDK repository for AFI920 customer integration.
- This README covers quick-start workflows for sensor discovery, data reception, and key configuration APIs.
- Product manuals, interface specifications, and detailed installation/operation guides are provided separately.

---

## Table of Contents

- [Supported Platforms](#supported-platforms)
- [Quick Start](#quick-start)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Building (C++ SDK)](#building-c-sdk)
- [Installation (Python SDK)](#installation-python-sdk)
- [Network Configuration](#network-configuration)
- [Examples](#examples)
- [Multi-Sensor](#multi-sensor)
- [Testing](#testing)
- [Data Types](#data-types)
- [Architecture](#architecture)
- [Troubleshooting](#troubleshooting)
- [Documentation](#documentation)
- [Security Notice](#security-notice)
- [Related Projects](#related-projects)
- [Changelog](#changelog)
- [License](#license)
- [Support](#support)

---

## Supported Platforms

| SDK | Supported Platforms | Minimum Requirements |
|-----|---------------------|----------------------|
| C++ SDK | Linux / Windows | C++17, CMake 3.16+ |
| Python SDK | Linux / Windows | Python 3.8+ |

---

## Quick Start

### C++

The snippet below demonstrates the minimal workflow: discover a sensor, connect, and print the detection count.

```cpp
#include <afi/afi_sdk.h>

int main() {
    // Discover sensors
    auto sensors = afi::AfiSdk::Discover();
    if (sensors.empty()) return 1;

    // Connect and receive data
    afi::AfiSdkConfig config;
    config.sensor_ip = sensors[0].ip;

    afi::AfiSdk sdk;
    if (sdk.Init(config) != 0) return 1;

    sdk.RegRecvCallback([](const afi::AfiRdiFrame& frame) {
        printf("Detections: %d\n", frame.message.num_detections);
    });

    sdk.Start();

    // ... application loop ...

    sdk.Stop();
}
```

### Python

The snippet below demonstrates the minimal workflow: discover a sensor, connect, and query basic sensor information.

```python
from afi_sdk import AfiSdk, AfiSdkConfig

# Discover sensors
sensors = AfiSdk.discover(timeout_s=3.0)
for s in sensors:
    print(f"{s['_source_ip']} — {s.get('serial_number', '?')}")

# Connect and read sensor info
sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))

info = sdk.get_device_info()
if info:
    print(f"Serial: {info['serial_number']}")
    print(f"SW Version: {info['software_version']}")

sdk.close()
```

Expected output:
```
192.168.10.150 — AFI920-001234
Serial: AFI920-001234
SW Version: 3.0.0
```

> **Tip:** In environments with multiple NICs, specify the discovery interface explicitly. In C++, use `Discover(3000, 30520, "192.168.10.100")`. In Python, use `discover(bind_ip="192.168.10.100")`.

---

## Features

- **Discovery**: Automatically discover AFI920 sensors on the network via UDP broadcast
- **Data Streaming**: Receive RDI (Point Cloud), SHII (Health), and SPI (Performance) data in real time
- **Vehicle Input (CSII)**: Send vehicle dynamics (speed, yaw rate, gear, ...) to the sensor over TCP
- **E2E Protection**: AUTOSAR E2E Profile 7 (CRC-64/XZ) verification on all data streams
- **Config API**: Read and write sensor configuration over TCP via SOME/IP + JSON
- **Multi-Sensor**: Operate multiple sensors with independent instances
- **ISO 23150**: Compliant with the ISO 23150 automotive sensor data interface standard
- **Cross-Platform**: Supports Linux and Windows environments

---

## Prerequisites

### Compiler and Tools

| Item | C++ SDK | Python SDK |
|------|---------|------------|
| Language | C++17 (GCC 7+, Clang 5+, MSVC 2017+) | Python 3.8+ |
| Build Tool | CMake 3.16+ | pip |
| Dependencies | None. `nlohmann/json` 3.12.0 is bundled. | Core: none (standard library only). The software update utility optionally needs `cryptography` — install with `pip install afi-sdk[update]`. |

### Network Setup

The AFI920 sensor communicates over Ethernet. The host PC must be on the same subnet as the sensor.

| Setting | Value |
|---------|-------|
| Sensor Default IP | `192.168.10.150` |
| Host IP | `192.168.10.x` (for example, `192.168.10.100`) |
| Subnet Mask | `255.255.255.0` |

**Linux example**
```bash
# Assign an IP address to the NIC connected to the sensor
sudo ip addr add 192.168.10.100/24 dev eth0
```

**Windows**  
Network Adapter Properties → IPv4 → Manual IP configuration (`192.168.10.100`, `255.255.255.0`)

---

## Building (C++ SDK)

The protocol interface headers are vendored in this repository (under `third_party/afi920_interface/`), so no submodule initialization is required — just clone and build.

### Linux

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Windows (Visual Studio 2022)

**Developer Command Prompt**
```cmd
mkdir build && cd build
cmake -G "NMake Makefiles" .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

**Visual Studio Generator**
```cmd
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

**Ninja Generator**
```cmd
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_EXAMPLES` | `ON` | Build example programs |
| `BUILD_TESTS` | `OFF` | Build test programs |
| `CMAKE_BUILD_TYPE` | — | `Release`, `Debug`, `RelWithDebInfo` |

### Build Outputs

| Platform | Output File | Path |
|----------|-------------|------|
| Linux | `libafi_sdk.a` | `build/src/libafi_sdk.a` |
| Windows | `afi_sdk.lib` | `build/src/afi_sdk.lib` |

### CMake Integration

Integrate the SDK by adding it as a subdirectory of your project:

```cmake
add_subdirectory(afi920_sdk)
target_link_libraries(your_app PRIVATE afi_sdk)
```

### Platform Notes

- **Windows:** Automatically links against `ws2_32`, and Winsock initialization is handled internally.
- **Linux:** Automatically links against `pthread`.

---

## Installation (Python SDK)

```bash
cd python/
pip install .
```

Development install:
```bash
pip install -e .
```

Verify installation:
```bash
python -c "import afi_sdk; print(afi_sdk.__version__)"
# Output: 3.0.0
```

---

## Network Configuration

### Default Ports

| Port | Protocol | Direction | Event ID | Description |
|------|----------|-----------|----------|-------------|
| 30500 | TCP | Host ↔ Sensor | — | Config API (SOME/IP + JSON, Method IDs `0x0010`–`0x0092`) |
| 30501 | TCP | Host → Sensor | `0x8001` | CSII (Vehicle Input) |
| 30509 | TCP/UDP | Sensor → Host | `0x8002` | RDI (Radar Detections / Point Cloud) |
| 30510 | TCP/UDP | Sensor → Host | `0x8003` | SHII (Sensor Health Information) |
| 30511 | TCP/UDP | Sensor → Host | `0x8004` | SPI (Sensor Performance Information) |
| 30520 | UDP | Host ↔ Sensor | — | Discovery (Broadcast) |

> **Note:** The default data stream transport is TCP. If needed, it can be changed through `stream_transport` in `AfiSdkConfig`.

> **E2E Protection:** Every RDI/SHII/SPI/CSII payload carries a 20-byte AUTOSAR E2E Profile 7 header (CRC-64/XZ) between the SOME/IP header and the ISO 23150 payload. The SDK verifies the CRC on receive (`validate_e2e`, default on); set `e2e_strict = true` to drop frames with a failing CRC instead of delivering them with a warning.

---

## Examples

### C++ Examples

| Program | Description |
|---------|-------------|
| `sample_discovery` | Discover sensors on the network |
| `sample_pointcloud` | Receive RDI point cloud data |
| `sample_data_stream` | Receive RDI/SHII/SPI data simultaneously |
| `sample_config` | Read and write sensor configuration |
| `sample_multi_sensor` | Receive data from multiple sensors |

Source code: [examples/cpp/](examples/cpp/)

### Python Examples

| Program | Description |
|---------|-------------|
| `sample_discovery.py` | Discover sensors on the network |
| `sample_data_stream.py` | Receive RDI/SHII/SPI data simultaneously |
| `sample_config.py` | Read and write sensor configuration |
| `sample_multi_sensor.py` | Receive data from multiple sensors |
| `sample_shii_watchdog.py` | Restart the sensor on repeated SHII health warnings |
| `sample_stream_reconnect.py` | Auto-reconnect an RDI stream when it stalls |
| `sample_config_backup.py` | Save/restore sensor configuration to/from JSON |

Source code: [python/examples/](python/examples/)

---

## Software Update

Flash sensors from a software bundle. The utility discovers sensors and updates
them in parallel (up to 8 at once); for each sensor it compares every domain's
git version in the bundle against the hash the sensor reports and flashes only
the domains that differ, then reboots and verifies.

```bash
pip install afi-sdk[update]        # decryption needs the 'cryptography' extra
python tools/software_update.py <bundle.tar> --password <pw>
```

- Bundle = a `.tar` carrying `software_manifest.json` plus per-domain
  `bootloader/driver/firmware` packages (`*.tar.gz.enc`).
- The bundle password is **not** shipped with the SDK — pass `--password` or set
  `AFI_UPDATE_PASSWORD`.
- If two sensors answer on the same IP, the tool stops and asks you to assign
  unique IPs first (`set_network_config`).
- All three domains (bootloader/driver/firmware) are compared and flashed only
  when their hash differs; `--force` re-flashes even unchanged domains.
  `--no-reboot` / `--no-verify` skip the post-flash reboot / verification.

See [API Reference](docs/api_reference.md#software-update) for details.

---

## Multi-Sensor

The AFI920 SDK supports up to **4 sensors** operating simultaneously. Create one `AfiSdk` instance per sensor, and each instance manages its connection and data reception independently.

### Port Assignment

To avoid port conflicts on the host, each sensor should use unique data ports. The recommended convention is a base port offset of `+100` per sensor. (The Config API needs no offset — it is an outbound TCP connection to each sensor's own IP at port 30500.)

| Sensor | RDI Port | SHII Port | SPI Port |
|--------|----------|-----------|----------|
| Sensor 1 | 30509 | 30510 | 30511 |
| Sensor 2 | 30609 | 30610 | 30611 |
| Sensor 3 | 30709 | 30710 | 30711 |
| Sensor 4 | 30809 | 30810 | 30811 |

Use `SetStreamDestination` (or `SetNetworkConfig`) to configure the correct destination IP and ports on the sensor side.

For complete multi-sensor examples, see [`sample_multi_sensor.cpp`](examples/cpp/sample_multi_sensor.cpp) and [`sample_multi_sensor.py`](python/examples/sample_multi_sensor.py).

---

## Testing

### Unit Tests

Build and run:

```bash
cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
ctest --output-on-failure
```

### Integration Tests

Run with a live sensor:

```bash
./test/test_discovery
./test/test_config_api 192.168.10.150
./test/test_data_receive 192.168.10.150 60
```

> **Windows:** Use `.exe` for executable names.

---

## Data Types

Wire-format data is represented via ISO 23150 C structures under `third_party/afi920_interface/src/c`, exposed through SDK wrapper types.

### Core Types

| Structure | Description |
|-----------|-------------|
| `AfiRdiFrame` | RDI frame passed through SDK callbacks |
| `bts_rdi_detection_t` | Individual detection (51 bytes) with position, velocity, and measurement errors |
| `bts_shi_message_t` | Sensor health information (variable length) |
| `bts_spi_message_t` | Sensor performance information |
| `AfiVehicleInput` | Vehicle dynamics sent to the sensor via `SendVehicleInput` (CSII) |

### Key Fields in `bts_rdi_detection_t`

| Field | Type | Description |
|-------|------|-------------|
| `position_radial_distance` | `float` | Range (m) |
| `position_azimuth` | `float` | Azimuth angle (rad) |
| `position_elevation` | `float` | Elevation angle (rad) |
| `relative_velocity_radial` | `float` | Radial velocity (m/s) |
| `radar_cross_section` | `float` | RCS (dBsm) |
| `signal_to_noise_ratio` | `float` | SNR (dB) |
| `existence_probability` | `uint8_t` | Existence probability (0-100%) |
| `object_id_reference` | `uint16_t` | Associated object ID (reserved) |
| `position_*_error`, `relative_velocity_radial_error` | `float` | Measurement errors (range/azimuth/elevation/velocity) |

Up to **8192 detections per frame** (36-byte header + 51 bytes per detection). Large RDI frames are segmented with SOME/IP-TP and reassembled automatically by the SDK.

> **Important:** Detection data in `AfiRdiFrame` is only valid within the callback. Deep-copy the frame if you need to retain it beyond the callback's lifetime.

For the full list of structures and response types, see [docs/api_reference.md](docs/api_reference.md) and the headers in [third_party/afi920_interface/src/c](third_party/afi920_interface/src/c).

---

## Architecture

```text
AfiSdk
  ├── Discovery
  ├── Config API Client
  ├── Data Stream Receivers (RDI / SHII / SPI)
  │     └── E2E Profile 7 Validation (CRC-64/XZ)
  ├── CSII Vehicle Input Sender (Host → Radar)
  └── Protocol Parsers
```

- Each `AfiSdk` instance operates independently.
- For multi-sensor operation, create one instance per sensor.
- Callbacks fire on internal receiver threads; keep handlers non-blocking or hand off data to a dedicated worker thread.
- `SendVehicleInput()` runs on the caller's thread and connects to the CSII port lazily on first use.

---

## Troubleshooting

| Symptom | Cause | Solution |
|---------|-------|----------|
| `Init()` → `kConnectFailed` | Cannot connect to the sensor | Check sensor IP, network cable, and subnet settings |
| Config API works but no data is received | Stream destination configuration mismatch | Check destination IP/ports/protocols with `GetNetworkConfig` |
| E2E CRC mismatch warnings in the log | Corrupted frames, or sensor firmware older than v3.0 | Verify the sensor runs v3.0 firmware; set `e2e_strict = true` to drop bad frames |
| High packet loss rate | UDP receive buffer is too small | Increase `socket_buffer_size` in `AfiSdkConfig` (C++ SDK) |

For the full error code list and recovery guidance, see [API Reference](docs/api_reference.md#error-codes).

---

## Documentation

| Document | Description |
|----------|-------------|
| [API Reference](docs/api_reference.md) | Public C++ and Python API reference, including key data types |
| [Python SDK](python/README.md) | Python SDK quick reference |
| [CHANGELOG](CHANGELOG.md) | Release history |

---

## Security Notice

The AFI920 SDK communicates with the sensor over plaintext TCP/UDP protocols and does not include encryption or authentication mechanisms such as TLS or DTLS.

- Use a dedicated VLAN or a physically isolated network for the sensor network whenever possible.
- Restrict access to the network segment carrying sensor data.
- Apply additional measures such as VPNs and firewalls in untrusted network environments.
- **Software update** is transferred over the same plaintext Config API and flashes sensor firmware. Run updates only on a trusted, isolated network. Update bundles are encrypted; keep the bundle password secret and never commit it or the bundle contents to source control.

---

## Related Projects

| Project | Description |
|---------|-------------|
| [afi920-ros2-driver](https://github.com/bitsensing/afi920-ros2-driver) | AFI920 ROS 2 driver package |

---

## Changelog

For the full release history, see [CHANGELOG.md](CHANGELOG.md).

---

## License

BSD-3-Clause

Copyright (c) 2025-2026 bitsensing Inc.

See [LICENSE](LICENSE) for the full license text. Third-party license notices are documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## Support

- **GitHub Issues**: Reproducible bugs in the public SDK, documentation typos, and improvement suggestions ([github.com/bitsensing/afi920-sdk/issues](https://github.com/bitsensing/afi920-sdk/issues))
- **Technical Support**: AFI920 product usage, customer environment integration, and separately provided documentation (`tech-support@bitsensing.com`)
