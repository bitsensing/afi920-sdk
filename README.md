# AFI920 SDK

> **[한국어 (Korean)](README_ko.md)**

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-blue)
![Python](https://img.shields.io/badge/python-3.8%2B-blue)
![License](https://img.shields.io/badge/license-BSD--3--Clause-green)

C++ and Python SDK for the bitsensing **AFI920 4D Imaging Radar**.  
Covers sensor discovery, RDI/SHII/SPI data reception, CSII vehicle input, the Config API with its access modes, and firmware update.

> **Version 2.2.0**: tracks AFI920 interface v1.1.0 (firmware v3.1.1) and receives data from interface v1.0.x sensors (firmware 3.0.0 to 3.1.0). See [CHANGELOG.md](CHANGELOG.md).

> **Branch `feature/lsad_test`**: PMOI test build for sensors on firmware 3.1.0. Besides RDI/SHII/SPI it receives PMOI (Event `0x8005`) from sensors whose Discovery reports `objects_port`. Enable it with `AfiSdkConfig::enable_pmoi` and `pmoi_port` (C++) or `AfiSdkConfig.pmoi_port` with `afi_sdk.streams.parse_pmoi_frame()` (Python). On firmware 3.1.0 (interface v1.0.x) the Config API and firmware update return `kNotSupported`; firmware 3.1.1 does not send PMOI.

> **bitsensing**: [bitsensing.com](https://bitsensing.com)

---

## Scope

- Public C++ and Python SDK repository for AFI920 customer integration.
- This README covers quick-start workflows for sensor discovery, data reception, sensor configuration, and firmware update.
- Product manuals, interface specifications, and detailed installation/operation guides are provided separately.
- Sensors on interface v1.0.x (firmware 3.0.0 to 3.1.0) are supported for data only, in this release only. See [Firmware Compatibility](#firmware-compatibility).
- The protocol interface headers under `third_party/afi920_interface/src/c` are vendored from interface v1.1.0.

---

## Table of Contents

- [Supported Platforms](#supported-platforms)
- [Quick Start](#quick-start)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Building (C++ SDK)](#building-c-sdk)
- [Installation (Python SDK)](#installation-python-sdk)
- [Network Configuration](#network-configuration)
- [Firmware Compatibility](#firmware-compatibility)
- [Configuration Workflow](#configuration-workflow)
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
    print(f"{s['_source_ip']} - {s.get('serial_number', '?')}")

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
192.168.10.150 - AFI920-001234
Serial: AFI920-001234
SW Version: 3.1.1
```

> **Tip:** In environments with multiple NICs, specify the discovery interface explicitly. In C++, use `Discover(3000, 30520, "192.168.10.100")`. In Python, use `discover(bind_ip="192.168.10.100")`. Discovery responses travel over UDP and one can be missed; if a sensor does not appear, run Discovery again.

---

## Features

- **Discovery**: Find AFI920 sensors on the network via UDP broadcast. Responses are collected for one timeout window and deduplicated by source IP and serial number.
- **Data Streaming**: Receive RDI (Point Cloud), SHII (Health), and SPI (Performance) in real time over TCP or UDP (C++). The Python SDK provides the matching parsing helpers in `afi_sdk.streams`.
- **Vehicle Input (CSII)**: Send vehicle dynamics (speed, yaw rate, gear, ...) to the sensor over UDP (interface v1.1.0) or TCP (v1.0.x).
- **E2E Protection**: 20-byte E2E header (IEEE 802.3 CRC-32 over the body) on every data stream message and every Config API request and response. The verdict is delivered with each frame.
- **Config API**: Read and write sensor configuration over TCP via SOME/IP + JSON, including the access mode API (`GetMode`, `SetMode`, `EnterMode`).
- **Firmware Update**: Config API firmware update calls in C++ and Python, plus the complete update sequence in the Python module `afi_sdk.firmware`.
- **Multi-Sensor**: Operate multiple sensors with independent instances.
- **ISO 23150**: Compliant with the ISO 23150 automotive sensor data interface standard.
- **Cross-Platform**: Supports Linux and Windows environments.

---

## Prerequisites

### Compiler and Tools

| Item | C++ SDK | Python SDK |
|------|---------|------------|
| Language | C++17 (GCC 7+, Clang 5+, MSVC 2017+) | Python 3.8+ |
| Build Tool | CMake 3.16+ | pip |
| Dependencies | None. `nlohmann/json` 3.12.0 is bundled. | None. Uses only the standard library. |

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
| `CMAKE_BUILD_TYPE` | (none) | `Release`, `Debug`, `RelWithDebInfo` |

### Build Outputs

| Platform | Output File | Path |
|----------|-------------|------|
| Linux | `libafi_sdk.a` | `build/src/libafi_sdk.a` |
| Windows | `afi_sdk.lib` | `build/src/afi_sdk.lib` |

### CMake Integration

**Use as a subdirectory**

```cmake
add_subdirectory(afi920_sdk)
target_link_libraries(your_app PRIVATE afi_sdk)
```

**Use as an installed package**

```bash
cmake --install build --prefix /usr/local     # Visual Studio generator: add --config Release
```

```cmake
find_package(afi_sdk REQUIRED)                # set CMAKE_PREFIX_PATH to the prefix if it is not a default location
target_link_libraries(your_app PRIVATE afi::afi_sdk)
```

The install holds the static library, the `include/afi` headers, the protocol interface headers under `include/afi920_interface`, and the CMake package files under `lib/cmake/afi_sdk`. A subdirectory build can use the same `afi::afi_sdk` name.

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
# Output: 2.2.0
```

---

## Network Configuration

### Default Ports

| Port | Protocol | Direction | Event ID | Description |
|------|----------|-----------|----------|-------------|
| 30500 | TCP | Host ↔ Sensor | (none) | Config API (SOME/IP + JSON/raw chunk, Method IDs `0x0010`–`0x00E1`) |
| 30501 | UDP (v1.1.0), TCP (v1.0.x) | Host → Sensor | `0x8001` | CSII (Vehicle Input) |
| 30509 | TCP/UDP | Sensor → Host | `0x8002` | RDI (Radar Detections / Point Cloud) |
| 30510 | TCP/UDP | Sensor → Host | `0x8003` | SHII (Sensor Health Information) |
| 30511 | TCP/UDP | Sensor → Host | `0x8004` | SPI (Sensor Performance Information) |
| 30520 | UDP | Host ↔ Sensor | (none) | Discovery (Broadcast) |

> **Note:** The default data stream transport is TCP: the host connects to the sensor at the stream port. Over UDP the host binds the stream port and accepts only datagrams whose source is `sensor_ip`. Set `stream_transport` globally, or use `rdi_transport`, `shi_transport`, and `spi_transport` as per-stream overrides; the value must match the protocol the sensor is configured to send (`GetNetworkConfig`). CSII uses UDP on interface v1.1.0 and TCP on v1.0.x. The SDK picks the CSII transport from the sensor's interface generation, and `csii_transport` overrides it.

> **E2E Protection:** Every RDI/SHII/SPI/CSII message and every Config API request and response carries a 20-byte E2E header (CRC high 0, CRC-32, Length, Counter, Data ID; big-endian). The CRC covers the body only, and a zero CRC field never passes. The interface leaves the disposition of a failed check to the host (System Spec §7.5). By default the SDK logs a warning and delivers the frame with its verdict in `e2e_counter`, `e2e_checked`, and `e2e_valid` (`AfiRdiFrame`, `AfiStreamMeta`, `AfiWirePayloadFrame`). `validate_e2e` turns the data stream check on or off, and `e2e_strict = true` drops failing frames instead. A Config API response that fails the check is still returned, and `GetLastSensorResult().e2e_ok` holds its verdict. A sensor on interface v1.0.x puts a CRC-64/XZ over Length, Counter, Data ID and the body in the first 8 bytes. The SDK accepts it on receive and reports the sender in `generation`.

---

## Firmware Compatibility

This SDK release receives data from sensors on interface v1.0.x (firmware 3.0.0 to 3.1.0) as well as interface v1.1.0 (firmware 3.1.1). The Config API and the firmware update are interface v1.1.0 only. Support for v1.0.x ends with this release. bitsensing moves those sensors to firmware 3.1.1.

| Function | Interface v1.1.0 (FW 3.1.1) | Interface v1.0.x (FW 3.0.0 to 3.1.0) |
|----------|-----------------------------|--------------------------------------|
| Discovery | Supported | Supported |
| RDI, SHII, SPI reception | Supported | Supported (E2E CRC-64/XZ accepted) |
| CSII | UDP 30501 | TCP 30501 |
| PMOI reception (`feature/lsad_test` only) | Not sent | Supported when Discovery reports `objects_port` (FW 3.1.0, TCP 30512) |
| Config API | Supported | `kNotSupported` / `NOT_SUPPORTED`, nothing sent |
| Firmware update | Supported | `kNotSupported` / `NOT_SUPPORTED`, nothing sent |

The SDK learns the generation from a read-only GetDeviceInfo request at the start of each Config API connection. With the Config API off, it learns it from the stream frames that pass the E2E check. `GetInterfaceGeneration()` (C++) and `interface_generation` (Python) report it. Discovery reports it per sensor in `AfiSensorInfo::generation` and the Python `_interface_generation` key. On a v1.0.x sensor the SDK closes the Config API connection after that request and returns every later Config API call without sending it. A v1.0.x sensor runs some v1.1.0 requests, such as Restart and ResetAllSettings, even though it cannot parse them.

The stream layouts are the same in both generations. The SDK passes every value through unchanged, and the fields below mean different things on v1.0.x. The Common Header interface version of the payload (`1.0.0` or `1.1.0`) tells the generations apart.

| Field | Interface v1.0.x | Interface v1.1.0 |
|-------|------------------|------------------|
| RDI `existence_probability` | Marker 100, 60 or 40 | Probability × 100; augmented detections 95 or 90 |
| RDI DataQualifier | 0, 5 (injection or test mode), 6 | 0, 1, 6 |
| RDI MessageCounter | Send counter | Scan index; repeats on a DataQualifier 6 resend |
| RDI RecognisedDetectionsCapability | 4096 (FW 3.0.x) or 8192 | 2048 |
| RDI radial velocity domain | ±50 m/s (FW 3.0.0) | From −111.11 m/s |
| SHII sensor operation mode | `150` (a v1.0.x test mode) can occur | `6` (UDR) can occur |
| SHII input signal statuses | 1 entry | 4 entries |
| E2E Counter | Per stream, from 1 on each TCP connection | Send slot shared by RDI, SHII and SPI |

---

## Configuration Workflow

### Access Mode

Every config SET requires the diagnostic access mode, and the firmware update chain requires the programming access mode. Entry is one way, Normal → Diagnostic → Programming, and a reboot (or the radar's own communication-stack recovery) returns the radar to Normal.

| Mode | Value | Opens | Data streams |
|------|-------|-------|--------------|
| Normal | 0 | GET methods, `Restart`, `SetMode` | Sent |
| Diagnostic | 1 | Every config SET and `ResetAllSettings` | Sent |
| Programming | 2 | Firmware update chain | Halted until reboot |

| Operation | C++ | Python |
|-----------|-----|--------|
| Read the current mode | `GetMode(AfiModeStatus&)` | `get_mode()` |
| Enter the next mode | `SetMode(AfiAccessMode)` | `set_mode(AccessMode.X)` |
| Reach a mode from the current one | `EnterMode(AfiAccessMode)` | `enter_mode(AccessMode.X)` |

A SET outside the diagnostic mode returns `kSessionRequired`. A re-entry or a reverse step returns `kAccessDenied`, and `EnterMode(kDiagnostic)` from the programming mode returns `kAccessDenied`.

### Applying a Setting

The radar validates every SET and stores it in NVM before it answers. A setting takes effect after `Restart` (`0x0014`); there is no save command. Until the restart, a GET returns the stored value, which can differ from the value in operation. `SetNetworkConfig`, `SetRangeMode`, and `SetPtpConfig` can report `restart_required`. Stream destinations and the radar's own network settings follow the same rule. `Restart` answers before the radar resets, so the Config API connection drops; `ReconnectConfigApi()` (C++) or `reconnect()` (Python) waits until the radar accepts the connection again.

```cpp
afi::AfiSdk sdk;
sdk.Init(config);

if (sdk.EnterMode(afi::AfiAccessMode::kDiagnostic) != afi::kOk) {
    auto r = sdk.GetLastSensorResult();
    printf("EnterMode failed: %s\n", r.return_name.c_str());
    return 1;
}
bool restart_required = false;
sdk.SetRangeMode(afi::AfiRangeMode::kLongRange, &restart_required);
sdk.Restart();
sdk.ReconnectConfigApi();   // waits up to 60 s by default
```

```python
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, RangeMode, OK

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))

if sdk.enter_mode(AccessMode.DIAGNOSTIC) != OK:
    print(sdk.last_result)
elif sdk.set_range_mode(RangeMode.LONG_RANGE) == OK:
    sdk.restart()
    sdk.reconnect()          # waits up to 60 s by default
```

### Firmware Update

The firmware update chain runs in the programming access mode, which halts RDI/SHII/SPI until the radar reboots.

| `update_type` | Components | Written to |
|---------------|------------|------------|
| `"firmware"` | `m7_0`, `m7_1`, `m7_2`, `a53` (all four required) | The inactive slot face (`target_slot`), activated with `FirmwareSwitchSlot` |
| `"bootloader"` | `bootloader` | In place, with a backup copy |

- `FirmwareUpdateStart` sends the image CRC-32 as `"0x%08X"` and answers `max_chunk_size` (4096) and `target_slot`.
- `FirmwareDataChunk` may accept a chunk only in part. Resume at `received_bytes` (C++: the overload with `AfiOtaChunkResult`; Python: the returned dict).
- After `FirmwareUpdateCommit`, poll `FirmwareUpdateStatus` until the state is `idle`, then switch the slot and send `FirmwareUpdateReboot`.
- The Python module `afi_sdk.firmware` runs the whole sequence: `update()` enters the programming mode, uploads each component, switches the slot, and reboots; `wait_for_restart()` and `verify()` confirm the result. The C++ SDK provides the individual calls; the caller follows the same order.

### Error Reporting

Config API methods return an `AfiError` value, and radar return codes map to distinct errors.

| Radar return code | C++ `AfiError` | Value |
|-------------------|----------------|-------|
| `INVALID_PARAM` | `kInvalidParam` | -6 |
| `NOT_SUPPORTED` | `kNotSupported` | -10 |
| `BUSY` | `kBusy` | -11 |
| `SESSION_REQUIRED` | `kSessionRequired` | -12 |
| `ACCESS_DENIED` | `kAccessDenied` | -13 |
| `INVALID_STATE` | `kInvalidState` | -14 |
| Any other code | `kSensorError` | -8 |

`GetLastSensorResult()` holds the radar's `return_code`, `return_name`, `error_message` (firmware update methods), and the response E2E verdict `e2e_ok`. On an interface v1.0.x sensor every Config API call returns `kNotSupported` without sending, and `GetLastSensorResult()` reads `NOT_SUPPORTED` with an `error_message`.

The Python SDK uses the same numeric values (`OK`, `NOT_INITIALIZED`, `CONNECT_FAILED`, `TIMEOUT`, `DISCONNECTED`, `INVALID_PARAM`, `PROTOCOL_ERROR`, `SENSOR_ERROR`, `INTERNAL_ERROR`, `NOT_SUPPORTED`, `BUSY`, `SESSION_REQUIRED`, `ACCESS_DENIED`, `INVALID_STATE`). `last_error` holds the code of the last call, and `last_result` holds `return_code`, `return_name`, `error_message`, `data`, and the response E2E verdict `e2e_ok`.

---

## Examples

### C++ Examples

| Program | Description |
|---------|-------------|
| `sample_discovery` | Discover sensors on the network |
| `sample_pointcloud` | Receive RDI point cloud data |
| `sample_data_stream` | Receive RDI/SHII/SPI data simultaneously |
| `sample_config` | Read sensor configuration |
| `sample_multi_sensor` | Receive data from multiple sensors |
| `set_stream_destination` | Point the RDI/SHII/SPI streams at a host and restart to apply |
| `restart_sensor` | Restart the sensor and wait for the Config API |

Source code: [examples/cpp/](examples/cpp/)

### Python Examples

| Program | Description |
|---------|-------------|
| `sample_discovery.py` | Discover sensors on the network |
| `sample_pointcloud.py` | Receive RDI point cloud data |
| `sample_data_stream.py` | Receive RDI/SHII/SPI data simultaneously |
| `sample_config.py` | Read sensor configuration and write values back |
| `sample_multi_sensor.py` | Configure data ports on multiple sensors |
| `set_yaw_90.py` | Write the mounting yaw and restart to apply |

Source code: [python/examples/](python/examples/)

---

## Multi-Sensor

The AFI920 SDK supports up to **4 sensors** operating simultaneously. Create one `AfiSdk` instance per sensor, and each instance manages its connection and data reception independently.

### Port Assignment

Over TCP (the default) each instance connects to its own sensor's IP, so every sensor keeps the default stream ports. Over UDP the host binds each stream port, so sensors sending to the same host need unique ports. The recommended convention is an offset of `+100` per sensor. The Config API needs no offset because it is an outbound TCP connection to each sensor's IP at port 30500.

| Sensor | RDI Port | SHII Port | SPI Port |
|--------|----------|-----------|----------|
| Sensor 1 | 30509 | 30510 | 30511 |
| Sensor 2 | 30609 | 30610 | 30611 |
| Sensor 3 | 30709 | 30710 | 30711 |
| Sensor 4 | 30809 | 30810 | 30811 |

Write each sensor's destination with `SetStreamDestination` or `SetNetworkConfig` in the diagnostic access mode, restart the sensor, and set the same ports (`rdi_port`, `shi_port`, `spi_port`) in that instance's `AfiSdkConfig`.

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

CTest runs the C++ unit tests (including `test_config_transport`) and, when a Python 3 interpreter is found, `test_python_streams` and `test_python_sdk`. `test_python_config_transport` and `test_python_firmware` are registered when `pytest` is installed:

```bash
pip install pytest
```

### Integration Tests

Run with a live sensor:

```bash
./test/test_discovery
./test/test_config_api 192.168.10.150
./test/test_data_receive 192.168.10.150 60
python test/test_live_conformance.py 192.168.10.150 --duration 30
```

- `test_config_api` sends GET requests only. With `--write` it enters the diagnostic access mode and writes the same values back; every write is stored in NVM, and the radar stays in the diagnostic mode until it reboots.
- `test_live_conformance.py` is read-only: it sends no SET, Restart, firmware, or access mode request. `--skip-csii` skips the CSII section. Exit code 0 means every check passed on every sensor.

The following tests are destructive and refuse to run without their confirmation flag. Use them only on a sensor set aside for testing:

```bash
python test/test_live_write.py 192.168.10.150 --yes-mutate
python test/test_live_firmware.py 192.168.10.150 --images DIR --fw-hash <hash>     [--bootloader --bl-hash <hash>] --yes-flash
./test/test_firmware_live 192.168.10.150 DIR/bootloader.bin <hash> --yes-flash
```

- `test_live_write.py` checks the access mode gate, every config SET (applied after Restart and read back by GET and Discovery), ResetConfigDefaults, ResetAllSettings, and the programming mode. It reads the settings first and writes them back at the end.
- `test_live_firmware.py` checks the update refusals (oversized chunk, CRC mismatch, ABORT, 10 s inactivity), runs `afi_sdk.firmware.update()`, and switches faces back and forth. `DIR` holds the plain component images `m7_0.bin`, `m7_1.bin`, `m7_2.bin`, `a53.bin` and optionally `bootloader.bin`. The sensor ends on the written face.
- `test_firmware_live` runs the C++ chain: the bootloader written in place, then FirmwareSwitchSlot to the other face and back.

> **Windows:** Use `.exe` for executable names.

---

## Data Types

Wire-format data is represented via ISO 23150 C structures under `third_party/afi920_interface/src/c`, exposed through SDK wrapper types.

### Core Types

| Structure | Description |
|-----------|-------------|
| `AfiRdiFrame` | RDI frame passed through SDK callbacks, with the E2E verdict (`e2e_counter`, `e2e_checked`, `e2e_valid`) and the sender's `generation` |
| `bts_rdi_detection_t` | Individual detection (51 bytes) with position, velocity, and measurement errors |
| `bts_shi_message_t` | Sensor health information (variable length) |
| `bts_spi_message_t` | Sensor performance information |
| `AfiStreamMeta` | Receive time and E2E verdict passed to the SHII/SPI callback overloads |
| `AfiWirePayloadFrame` | Reassembled stream payload delivered before typed parsing |
| `AfiVehicleInput` | Vehicle dynamics sent to the sensor via `SendVehicleInput` (CSII); `sensor_id` defaults to 1 |
| `AfiModeStatus` | Access mode, time in the mode, and whether the data streams are halted |
| `AfiSensorResult` | Radar outcome of the last Config API call |
| `AfiInterfaceGeneration` | Interface generation of a sensor or frame: `kUnknown`, `kV1_0`, `kV1_1` |

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

The protocol allows up to **8192 detections per frame** (36-byte header + 51 bytes per detection); firmware v3.1.1 sends at most 2048. UDP large frames are segmented with SOME/IP-TP and reassembled automatically; TCP carries one complete SOME/IP frame.

> **Important:** A callback's frame reference is valid only during the callback. Copying the whole `AfiRdiFrame` is safe: the copy owns its vector and rebinds the C payload pointer. If you directly resize or replace the public storage vector, call `RebindStorage()` afterward.

For the full list of structures and response types, see [docs/api_reference.md](docs/api_reference.md) and the headers in [third_party/afi920_interface/src/c](third_party/afi920_interface/src/c).

---

## Architecture

```text
AfiSdk
  ├── Discovery
  ├── Config API Client (E2E request/response, reconnect on demand)
  ├── Data Stream Receivers (RDI / SHII / SPI)
  │     ├── E2E Validation (length / Data ID / CRC-32, verdict delivered with the frame)
  │     └── Bounded SOME/IP-TP Reassembly (UDP)
  ├── CSII Vehicle Input Sender (Host → Radar, UDP or TCP by generation)
  └── Protocol Parsers
```

- Each `AfiSdk` instance operates independently. For multi-sensor operation, create one instance per sensor.
- Callbacks fire on internal receiver threads; keep handlers non-blocking or hand off data to a dedicated worker thread.
- The RDI loss callback counts lost send slots from gaps in the E2E Counter of frames whose E2E check passed.
- TCP data streams reconnect after a connection drop, with a backoff from 1 s up to 30 s.
- `SendVehicleInput()` runs on the caller's thread and creates the CSII socket on first use: UDP for interface v1.1.0, TCP for v1.0.x.
- Each Config API connection opens with a read-only GetDeviceInfo request that decides the interface generation; on v1.0.x the connection closes and Config API calls return `kNotSupported` without sending.
- The Config API client sends one request at a time and verifies that each response echoes the request (message, client, and session ID, protocol version `0x01`, RESPONSE type). A timeout, a mismatched response, or a lost connection drops the socket, and the next call reconnects to the same endpoint. `request_timeout_ms` bounds each request, and a request body above 16,384 bytes returns `kInvalidParam` without being sent.
- A failed Config API connect during `Init()` does not fail `Init()`; the endpoint is kept and the next Config API call connects. `Init()` can be called again after `Stop()`.

---

## Troubleshooting

| Symptom | Cause | Solution |
|---------|-------|----------|
| `Init()` → `kConnectFailed` | A TCP data stream could not connect, or a UDP stream port could not be bound | Check sensor IP, network cable, subnet settings, and other processes using the port |
| A config SET returns `kSessionRequired` | The radar is not in the diagnostic access mode | Call `EnterMode(kDiagnostic)` before the SET |
| `SetMode` or `EnterMode` returns `kAccessDenied` | Re-entry, a reverse step, or the radar is in the programming mode | Restart the radar to return to the normal mode |
| A SET succeeded but the behavior did not change | Settings take effect after a restart | Call `Restart()`, then `ReconnectConfigApi()` |
| Data streams stopped after entering the programming mode | The programming mode halts RDI/SHII/SPI | Restart the radar |
| Config API calls return `kTimeout` or `kDisconnected` | No response within `request_timeout_ms`, the connection dropped, or another client holds the radar's single Config API connection | The next call reconnects; close other Config API clients |
| `kBusy` | More than 200 requests per second | Lower the request rate |
| Config API works but no data is received | Stream destination or transport mismatch, or UDP datagrams arrive from an address other than `sensor_ip` | Check destination IP/ports/protocols with `GetNetworkConfig` and match `AfiSdkConfig` |
| A sensor is missing from Discovery | A UDP response was lost, or the broadcast left through another NIC | Run Discovery again; pass `bind_ip` |
| E2E CRC mismatch warnings in the log | Corrupted frames | Check `e2e_valid` per frame; set `e2e_strict = true` to drop failing frames |
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
- Access mode entry has no authentication. Any host that reaches the Config API port can enter the diagnostic or programming mode, change settings, and update firmware. Keep the Config API port inside the trusted network.

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
