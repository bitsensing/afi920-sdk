# AFI920 SDK AI Integration Guide

| Item | Value |
|------|-------|
| Audience | AI coding assistants, LLM-driven automation, and engineers who need the shortest path from a bitsensing AFI920 4D imaging radar to working code |
| SDK version | 2.2.0 (C++ and Python) |
| Sensor interface | AFI920 interface v1.1.0. Interface v1.0.x (firmware 3.0.0 to 3.1.0) is supported for data only, see [section 13](#13-interface-v10x-compatibility) |
| License | BSD-3-Clause |
| Full API reference | [docs/api_reference.md](docs/api_reference.md) |

---

## Table of Contents

1. [What the SDK Does](#1-what-the-sdk-does)
2. [Repository Layout](#2-repository-layout)
3. [Network Prerequisites](#3-network-prerequisites)
4. [Build & Install](#4-build--install)
5. [Quickstart: Receive Point Cloud Data](#5-quickstart-receive-point-cloud-data)
6. [All Three Data Streams](#6-all-three-data-streams)
7. [Access Modes and Persistence](#7-access-modes-and-persistence)
8. [Configuration API](#8-configuration-api)
9. [Firmware Update](#9-firmware-update)
10. [Vehicle Input (CSII)](#10-vehicle-input-csii)
11. [Sensor Discovery](#11-sensor-discovery)
12. [Multi-Sensor Setup](#12-multi-sensor-setup)
13. [Interface v1.0.x Compatibility](#13-interface-v10x-compatibility)
14. [Key Data Structures](#14-key-data-structures)
15. [Error Codes](#15-error-codes)
16. [Port & Protocol Reference](#16-port--protocol-reference)
17. [Troubleshooting Checklist](#17-troubleshooting-checklist)

---

## 1. What the SDK Does

The AFI920 SDK connects a host computer to one or more bitsensing AFI920 radars over Ethernet.

| Direction | Channel | What travels |
|-----------|---------|--------------|
| Radar → Host | RDI (TCP/UDP 30509, Event `0x8002`) | Point cloud detections (range, azimuth, elevation, radial velocity, RCS, SNR, errors) |
| Radar → Host | SHII (TCP/UDP 30510, Event `0x8003`) | Sensor health information |
| Radar → Host | SPI (TCP/UDP 30511, Event `0x8004`) | Sensor performance and mounting pose |
| Host → Radar | CSII (30501, Event `0x8001`; UDP on v1.1.0, TCP on v1.0.x) | Vehicle dynamics |
| Host ↔ Radar | Config API (TCP 30500, SOME/IP + JSON) | Status, settings, access modes, firmware update |
| Host ↔ Radar | Discovery (UDP broadcast 30520) | Find sensors on the LAN |

Every RDI, SHII, SPI and CSII message and every Config API request and response carries a 20-byte E2E header (AUTOSAR Profile 7 layout, IEEE 802.3 CRC-32 over the body). Discovery has no E2E header. The SDK handles SOME/IP framing, SOME/IP-TP reassembly and E2E checking.

| Capability | C++ (`afi::AfiSdk`) | Python (`afi_sdk`) |
|------------|---------------------|--------------------|
| Discovery | `AfiSdk::Discover()` | `AfiSdk.discover()` |
| RDI / SHII / SPI reception | Receiver threads and typed callbacks | Parsers in `afi_sdk.streams`; the caller opens the sockets |
| CSII vehicle input | `SendVehicleInput()` | `send_vehicle_input()` |
| Config API and access modes | `int` return code plus out-parameter | `dict` / `int` return values |
| Firmware update | Individual protocol steps | Individual steps plus the complete sequence `afi_sdk.firmware.update()` |

---

## 2. Repository Layout

```
afi920-sdk/
├── CMakeLists.txt                 ← top-level build (CMake 3.16+, C++17)
├── README.md                      ← overview, build, workflows (English)
├── README_ko.md                   ← overview, build, workflows (Korean)
├── AFI920_SDK_GUIDE.md            ← this guide (English)
├── AFI920_SDK_GUIDE_ko.md         ← this guide (Korean)
├── CHANGELOG.md                   ← release history
├── LICENSE                        ← BSD-3-Clause
├── THIRD_PARTY_NOTICES.md
├── docs/
│   └── api_reference.md           ← complete C++ and Python API reference
├── include/afi/
│   ├── afi_sdk.h                  ← AfiSdk class: lifecycle, callbacks, CSII, Discovery, Config API
│   ├── afi_types.h                ← stream and Config API data types
│   ├── afi_config.h               ← AfiSdkConfig
│   └── afi_error.h                ← AfiError, AfiReturnCode, AfiErrorStr()
├── src/                           ← C++ implementation (static library target afi_sdk)
├── examples/cpp/                  ← sample_discovery, sample_pointcloud, sample_data_stream,
│                                    sample_config, sample_multi_sensor, restart_sensor,
│                                    set_stream_destination
├── python/
│   ├── pyproject.toml             ← package afi-sdk 2.2.0 (Python 3.8+, standard library only)
│   ├── src/afi_sdk/
│   │   ├── __init__.py            ← AfiSdk, AfiSdkConfig, RangeMode, AccessMode, error codes
│   │   ├── _sdk.py                ← AfiSdk class: Config API, CSII, Discovery
│   │   ├── firmware.py            ← complete firmware update sequence: update(), verify(), ...
│   │   ├── someip.py              ← SOME/IP framing, ConfigClient, discover(), constants
│   │   └── streams.py             ← E2E, SOME/IP-TP, RDI/SHII/SPI parsers, CSII builder
│   └── examples/                  ← sample_discovery.py, sample_pointcloud.py,
│                                    sample_data_stream.py, sample_config.py,
│                                    sample_multi_sensor.py, set_yaw_90.py
├── test/                          ← C++ and Python unit tests, live-sensor tests
├── third_party/
│   ├── afi920_interface/src/c/    ← vendored ISO 23150 protocol headers (bts_*.h)
│   └── nlohmann/                  ← nlohmann/json 3.12.0 (bundled)
└── tools/
    └── rdi_udp_sender.py          ← RDI UDP simulator for tests without a radar
```

| Example | What it shows |
|---------|---------------|
| `sample_discovery` / `.py` | Discover sensors on the network |
| `sample_pointcloud` / `.py` | Receive the RDI point cloud |
| `sample_data_stream` / `.py` | Receive RDI, SHII and SPI together |
| `sample_config` / `.py` | Read every config category (`.py --write` also writes values back) |
| `sample_multi_sensor` / `.py` | C++: receive from up to 4 sensors. Python: write per-sensor data ports and restart each sensor |
| `restart_sensor` | Restart a sensor through the Config API |
| `set_stream_destination` | Point the RDI, SHII and SPI destinations at a host IP |
| `set_yaw_90.py` | Set the mounting yaw (default 90°) |

> The ROS 2 driver lives in a separate repository, [afi920-ros2-driver](https://github.com/bitsensing/afi920-ros2-driver).

---

## 3. Network Prerequisites

| Item | Default value |
|------|---------------|
| Sensor IP | `192.168.10.150` |
| Host IP | `192.168.10.x`, for example `192.168.10.100` |
| Subnet mask | `255.255.255.0` |

1. Connect the host to the radar over Ethernet.
2. Give the host NIC a static IP in the `192.168.10.x` subnet.
   - Linux: `sudo ip addr add 192.168.10.100/24 dev eth0`
   - Windows: Network Adapter Properties → IPv4 → manual IP `192.168.10.100`, mask `255.255.255.0`
3. Check connectivity: `ping 192.168.10.150`.

The SDK has no encryption or authentication, and access mode entry is not authenticated. Keep the sensor network on a dedicated VLAN or an isolated segment.

---

## 4. Build & Install

### C++ (CMake 3.16+, C++17)

The ISO 23150 protocol headers are vendored under `third_party/afi920_interface/src/c` and `nlohmann/json` under `third_party/nlohmann`. No extra checkout or download step is needed.

```bash
cd afi920-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # examples build by default (BUILD_EXAMPLES=ON)
cmake --build build --config Release
# Linux:   build/src/libafi_sdk.a
# Windows: build/src/afi_sdk.lib (Visual Studio generators add a Release/ subfolder)
```

| CMake option | Default | Description |
|--------------|---------|-------------|
| `BUILD_EXAMPLES` | `ON` | Build `examples/cpp` |
| `BUILD_TESTS` | `OFF` | Build `test/` (run with `ctest --output-on-failure` in the build directory) |

**Link from your own CMakeLists.txt:**

```cmake
add_subdirectory(afi920-sdk)                       # path to this repository
target_link_libraries(my_app PRIVATE afi_sdk)
```

The `afi_sdk` target carries its include directories (`include/` and `third_party/afi920_interface/src/c`), the C++17 requirement, and the platform libraries (`ws2_32` on Windows, threads on Linux). Winsock initialization is internal.

### Python (3.8+, no external dependencies)

```bash
cd afi920-sdk/python
pip install .            # or for development: pip install -e .
python -c "import afi_sdk; print(afi_sdk.__version__)"     # 2.2.0
```

---

## 5. Quickstart: Receive Point Cloud Data

### C++

```cpp
#include <afi/afi_sdk.h>
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    afi::AfiSdkConfig cfg;
    cfg.sensor_ip = "192.168.10.150";    // default sensor IP
    cfg.enable_rdi = true;               // RDI over TCP (stream_transport defaults to "tcp")
    cfg.enable_config_api = false;       // data-only mode

    afi::AfiSdk sdk;

    // Called once per RDI frame (one per 100 ms send slot) on the RDI receiver thread.
    // Positions are spherical (range / azimuth / elevation), not Cartesian.
    sdk.RegRecvCallback([](const afi::AfiRdiFrame& frame) {
        const auto& m = frame.message;
        printf("Frame %u  detections: %u  e2e_valid: %d\n",
               frame.frame_seq, m.num_detections, frame.e2e_valid ? 1 : 0);
        for (uint16_t i = 0; i < m.num_detections; ++i) {
            const auto& d = m.detections[i];
            printf("  [%u] r=%.2f m  az=%.3f rad  el=%.3f rad  vel=%.2f m/s  rcs=%.1f dBsm\n",
                   i, d.position_radial_distance, d.position_azimuth,
                   d.position_elevation, d.relative_velocity_radial,
                   d.radar_cross_section);
        }
    });

    if (sdk.Init(cfg) != afi::kOk) return 1;    // connects the TCP stream
    if (sdk.Start() != afi::kOk) return 1;      // starts the receiver thread

    std::this_thread::sleep_for(std::chrono::seconds(10));
    sdk.Stop();
    return 0;
}
```

### Python

The Python `AfiSdk` class covers the Config API, CSII and Discovery. It opens no stream sockets: receive each stream with a plain socket and decode it with `afi_sdk.streams`. `python/examples/sample_pointcloud.py` covers TCP and UDP, and `python/examples/sample_data_stream.py` adds bounded SOME/IP-TP reassembly with `SomeipTpReassembler`.

```python
import socket, struct
from afi_sdk.someip import SOMEIP_HEADER_SIZE, DEFAULT_RDI_PORT
from afi_sdk.streams import (E2E_DATA_ID_RDI, strip_e2e,
                             parse_rdi_frame, parse_rdi_detection)

def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf

sock = socket.create_connection(("192.168.10.150", DEFAULT_RDI_PORT))   # TCP RDI stream

while True:
    hdr = recv_exact(sock, 8)                        # SOME/IP Message ID (4) + Length (4)
    if len(hdr) < 8:
        break
    length = struct.unpack(">I", hdr[4:8])[0]
    data = hdr + recv_exact(sock, length)            # one complete SOME/IP message
    e2e, iso = strip_e2e(data[SOMEIP_HEADER_SIZE:], expected_data_id=E2E_DATA_ID_RDI)
    if e2e is None or not e2e["ok"]:
        continue                                     # failed checks are logged; the body is kept
    frame = parse_rdi_frame(iso)                     # 36 B header
    if not frame:
        continue
    print(f"detections={frame['num_detections']}  counter={frame['message_counter']}  "
          f"dq={frame['data_qualifier']}")
    if frame["num_detections"] > 0:
        d = parse_rdi_detection(iso, frame["det_start"], 0)          # 51 B per detection
        print(f"  r={d['radial_distance']:.2f} m  az={d['azimuth']:.3f} rad  "
              f"el={d['elevation']:.3f} rad  v={d['radial_velocity']:.2f} m/s  "
              f"rcs={d['rcs']:.1f} dBsm")
```

DataQualifier `0` marks new scan data, `6` a resend of the previous scan (MessageCounter repeats), and `1` content that must not be used.

---

## 6. All Three Data Streams

### Register All Callbacks (C++)

`RegRecvCallback` is overloaded and selects the stream from the callback's parameter types. Enable the streams in the config (`enable_rdi`, `enable_shi`, `enable_spi`) and register the callbacks before `Start()`.

```cpp
afi::AfiSdkConfig cfg;
cfg.enable_rdi = true;
cfg.enable_shi = true;
cfg.enable_spi = true;

afi::AfiSdk sdk;

// RDI: point cloud (interface v1.1.0 firmware sends at most 2048 detections per frame; the protocol allows 8192)
sdk.RegRecvCallback([](const afi::AfiRdiFrame& f) {
    /* f.message.detections[0 .. f.message.num_detections-1], f.e2e_valid, f.generation */
});

// SHII: sensor health, with receive time and E2E verdict in AfiStreamMeta
sdk.RegRecvCallback([](const afi::AfiShiMessage& msg, const afi::AfiStreamMeta& meta) {
    /* msg.sensor_defect_recognised, msg.supply_voltage_status, meta.e2e_valid, ... */
});

// SPI: sensor performance and mounting pose
sdk.RegRecvCallback([](const afi::AfiSpiMessage& msg, const afi::AfiStreamMeta& meta) {
    /* msg.sensor_pose.orientation_yaw (rad), msg.fov_segments[0].blockage_status, ... */
});

// RDI send-slot loss: running totals of counted frames and lost slots
sdk.RegRecvCallback([](uint32_t total, uint32_t lost) {
    std::fprintf(stderr, "RDI: %u lost slots, %u frames counted\n", lost, total);
});

// Optional: reassembled E2E header + ISO payload before typed parsing (recorders)
sdk.RegWirePayloadCallback([](const afi::AfiWirePayloadFrame& w) {
    /* w.stream, w.payload, w.e2e_valid, w.generation */
});

if (sdk.Init(cfg) == afi::kOk) sdk.Start();
```

| Overload | Called with |
|----------|-------------|
| `(const AfiRdiFrame&)` | Each RDI frame, with `e2e_counter`, `e2e_checked`, `e2e_valid`, `generation` |
| `(const AfiShiMessage&)` / `(const AfiShiMessage&, const AfiStreamMeta&)` | Each SHII message. When both are registered, only the `AfiStreamMeta` overload is called |
| `(const AfiSpiMessage&)` / `(const AfiSpiMessage&, const AfiStreamMeta&)` | Each SPI message, same rule |
| `(uint32_t total, uint32_t lost)` | An RDI gap in the E2E Counter (send slot), with running totals |
| `RegWirePayloadCallback(const AfiWirePayloadFrame&)` | Every RDI, SHII and SPI payload after E2E checking and before the typed parser |

> **Callback threading and data lifetime**
> - Callbacks run on the receiver threads (one per enabled stream), not on the thread that called `Start()`. Keep them non-blocking and synchronize shared state.
> - The frame or message reference is valid only during the callback. Copying the whole `AfiRdiFrame` is safe: the copy owns its detection vector and rebinds `message.detections`. Call `RebindStorage()` after resizing or replacing `detections_storage` directly.
> - A failed E2E check is logged and the frame is still delivered with `e2e_valid = false`. `e2e_strict = true` drops it before any callback.
> - A typed callback runs only when the typed parser accepts the payload. The wire payload callback runs first on the same thread.
> - The loss callback counts gaps in the E2E Counter of frames whose E2E check passed. A gap includes slots the radar did not send to this host, such as slots before the first scan after boot or while a TCP connection was backlogged. A decreasing Counter (radar restart) starts a new baseline.
> - TCP streams reconnect after a drop, with a backoff from 1 s up to 30 s. UDP receivers accept datagrams only from `sensor_ip`.
> - Config API calls and `SendVehicleInput()` run synchronously on the caller's thread. Config API calls are serialized internally, but `GetLastSensorResult()` reflects whichever call finished last, so issue Config API calls from one thread. `SendVehicleInput()` may run on its own cyclic thread.

### Receiving All Streams (Python)

The Python SDK has no streaming callbacks. Receive RDI (30509), SHII (30510) and SPI (30511) on separate sockets, typically one background thread per stream, and parse each payload with `afi_sdk.streams`:

| Function | Result |
|----------|--------|
| `strip_e2e(frame, validate=True, expected_data_id=None)` | `(e2e, body)`. `e2e` holds `length_ok`, `data_id_ok`, `crc_zero`, `crc_ok`, `ok` and `generation` |
| `parse_rdi_frame(payload)` / `parse_rdi_detection(payload, det_start, index)` | RDI header dict / one detection dict |
| `parse_shi(payload)` | Common Header plus `operation_modes`, `defect_recognised`, `defect_reason`, `supply_voltage_status`, `temperature_status`, `input_signal_types`, `input_signal_statuses`, `time_sync`, `calibration_components`, `calibration_statuses`, `calibration_states` |
| `parse_spi(payload)` | Common Header plus `vehicle_coord_system`, `pose`, `fov_segments`, `recognisable_object_types`, `reference_target_types` |
| `SomeipTpReassembler(max_payload_size, max_sessions=8, max_segment_size=1392)` | UDP SOME/IP-TP reassembly: `feed(tp_header, payload, session_id)` |

Use `E2E_DATA_ID_RDI`, `E2E_DATA_ID_SHII` and `E2E_DATA_ID_SPI` as `expected_data_id`. `python/examples/sample_data_stream.py` wraps this in a reusable `StreamReceiver` class for TCP and UDP.

---

## 7. Access Modes and Persistence

Interface v1.1.0 gates the Config API by access mode. Every config SET needs the diagnostic mode, and the firmware update chain needs the programming mode. Entry is one way, Normal → Diagnostic → Programming. Only a reboot (or the radar's own communication-stack recovery) returns the radar to Normal.

| Mode | Value | Opens | Data streams |
|------|-------|-------|--------------|
| Normal | 0 | Every GET, `FirmwareUpdateStatus`, `GetBootInfo`, `Restart`, `GetMode`, `SetMode` | Sent |
| Diagnostic | 1 | The above, plus every config SET, `ResetAllSettings` and `ResetConfigDefaults` | Sent; up to about 1 s late while the radar writes NVM |
| Programming | 2 | Every GET, `Restart`, `GetMode`, `SetMode`, and the firmware update chain. The diagnostic methods close | RDI, SHII and SPI stop until reboot. CSII is still accepted |

| Operation | C++ | Python |
|-----------|-----|--------|
| Read the current mode | `GetMode(AfiModeStatus& out)` | `get_mode()` → `{access_mode, access_mode_name, elapsed_ms, streams_halted}` |
| Enter the next mode | `SetMode(AfiAccessMode mode, AfiModeStatus* out = nullptr)` | `set_mode(AccessMode.DIAGNOSTIC)` / `set_mode(AccessMode.PROGRAMMING)` |
| Reach a mode from the current one | `EnterMode(AfiAccessMode target)` | `enter_mode(AccessMode.DIAGNOSTIC)` / `enter_mode(AccessMode.PROGRAMMING)` |

| Situation | Result |
|-----------|--------|
| SET outside the diagnostic mode | `kSessionRequired` / `SESSION_REQUIRED` |
| `SetMode` re-entry of the current mode, or a step back | `kAccessDenied` / `ACCESS_DENIED` |
| `SetMode(kProgramming)` from Normal | `kSessionRequired`. `EnterMode(kProgramming)` steps through Diagnostic |
| `EnterMode(kDiagnostic)` while in Programming | `kAccessDenied` (reboot first) |
| `SetMode` / `EnterMode` with `kNormal` | `kInvalidParam`, nothing sent |
| `EnterMode` to the current mode | `kOk` |

### Persistence and Restart

- Every SET, `ResetAllSettings` and `ResetConfigDefaults` writes NVM before it answers (about 175 ms). `kOk` means the value is stored. There is no save call.
- Stored values take effect after `Restart()`. Until then a GET returns the stored value, which can differ from the running value. `GetPtpConfig` reports both (`configured_profile`, `profile`).
- A SET that fails changes neither RAM nor NVM. An NVM write failure answers `INTERNAL_ERROR` (`kSensorError`).
- `Restart()` works in every mode and answers before the radar resets, so the Config API connection drops. `ReconnectConfigApi(timeout_ms = 60000)` (C++, retries every 500 ms) or `reconnect(timeout_s=60.0, interval_s=1.0)` (Python) waits for the radar. After a reboot the radar is in Normal.

| Setting | C++ SET | Python SET | `restart_required` |
|---------|---------|------------|--------------------|
| Radar network and all stream destinations | `SetNetworkConfig` (full update) | `set_network_config(**fields)` (partial update) | Always `true` |
| One stream destination | `SetStreamDestination` | `set_stream_destination` | |
| Range mode | `SetRangeMode` | `set_range_mode` | Always `true` |
| SNR level and CFAR tables | `SetDetectionThreshold` | `set_detection_threshold` | |
| Detection filters | `SetDetectionFilters` | `set_detection_filters` | |
| PTP profile | `SetPtpConfig` | `set_ptp_config` / `set_ptp_config_with_result` | When the stored profile differs from the running one |
| Mounting position, rotation, position code | `SetMountingPosition`, `SetRotation`, `SetSensorPosition` | `set_mounting_position`, `set_rotation`, `set_sensor_position` | |
| Listed keys back to defaults | `ResetConfigDefaults` | `reset_config_defaults` | |
| All settings back to defaults | `ResetAllSettings` | `reset_all_settings` | Restarts the radar itself; keeps its IP address, subnet mask and gateway |

All rows need the diagnostic mode, and all of them apply after a restart.

```cpp
afi::AfiSdk sdk;
sdk.Init(cfg);                                   // enable_config_api = true (default)

if (sdk.EnterMode(afi::AfiAccessMode::kDiagnostic) != afi::kOk) {
    printf("EnterMode failed: %s\n", sdk.GetLastSensorResult().return_name.c_str());
    return 1;
}
bool restart_required = false;
sdk.SetRangeMode(afi::AfiRangeMode::kLongRange, &restart_required);
sdk.Restart();                                   // settings take effect after the restart
sdk.ReconnectConfigApi();                        // waits up to 60 s; the radar is back in Normal
```

```python
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, RangeMode, OK

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))

if sdk.enter_mode(AccessMode.DIAGNOSTIC) != OK:
    print(sdk.last_result)                       # return_code, return_name, error_message, ...
elif sdk.set_range_mode(RangeMode.LONG_RANGE) == OK:
    sdk.restart()
    sdk.reconnect()                              # waits up to 60 s
```

---

## 8. Configuration API

The Config API runs over its own TCP connection (port 30500), independent of the data streams. All calls are synchronous. The radar serves one Config API client at a time and answers `BUSY` above 200 requests per second.

| | C++ | Python |
|---|-----|--------|
| Enable | `enable_config_api = true` (default). Otherwise calls return `kNotInitialized` | `sdk.init(config)` always connects. After a failed `init()`, call `reconnect()` |
| GET | `int GetX(AfiX& out)`: `kOk` or a negative `AfiError`; `out` is valid only on `kOk` | `get_x()` → `dict` (or `int` for range mode / position code), `None` on error |
| SET / command | `int SetX(...)` | `int`, except `set_network_config`, `set_ptp_config_with_result`, `reset_config_defaults` and the firmware calls that return data (`dict` or `None`) |
| Last outcome | `GetLastSensorResult()` → `AfiSensorResult` | `last_error`, `last_result` |
| Reconnect | `ReconnectConfigApi(timeout_ms)` | `reconnect(timeout_s, interval_s)` |

A timeout, a lost connection or a mismatched response closes the socket, and the next call reconnects to the same endpoint. A request body above 16,384 bytes returns `kInvalidParam` / `INVALID_PARAM` without being sent.

### Read (C++)

```cpp
afi::AfiSdkConfig cfg;
cfg.sensor_ip = "192.168.10.150";
cfg.enable_rdi = false;                          // Config API only

afi::AfiSdk sdk;
if (sdk.Init(cfg) != afi::kOk) return 1;

afi::AfiDeviceInfo info;                         // serial, model, HW/SW version, hashes, MAC
if (sdk.GetDeviceInfo(info) == afi::kOk)
    printf("Serial: %s  SW: %s\n", info.serial_number.c_str(), info.software_version.c_str());

afi::AfiSensorStatus status;       sdk.GetSensorStatus(status);      // state, temperatures, voltage, uptime
afi::AfiDiagnosticInfo diag;       sdk.GetDiagnosticInfo(diag);      // combined state, fault list
afi::AfiNetworkConfig net;         sdk.GetNetworkConfig(net);        // radar network + stream destinations
afi::AfiDetectionThreshold thresh; sdk.GetDetectionThreshold(thresh);
afi::AfiDetectionFilters filters;  sdk.GetDetectionFilters(filters);
afi::AfiPtpConfig ptp;             sdk.GetPtpConfig(ptp);            // profiles + live sync status
afi::AfiModeStatus mode_status;    sdk.GetMode(mode_status);

afi::AfiRangeMode mode;
if (sdk.GetRangeMode(mode) == afi::kOk)
    printf("Range mode: %s\n", afi::AfiRangeModeName(mode));          // "DR" .. "UDR"
```

### Write (C++)

```cpp
if (sdk.EnterMode(afi::AfiAccessMode::kDiagnostic) != afi::kOk) return 1;

sdk.SetRangeMode(afi::AfiRangeMode::kLongRange);

// One stream destination (stored in NVM, applies after Restart)
afi::AfiStreamDestination dst;
dst.ip       = "192.168.10.100";
dst.port     = 30509;
dst.protocol = "udp";                            // "tcp" or "udp"; empty is not sent
sdk.SetStreamDestination(afi::AfiStreamKind::kDetection, dst);

// Full network update: start from the stored values and change what you need
afi::AfiNetworkConfig net;
sdk.GetNetworkConfig(net);
net.health      = {"192.168.10.100", 30510, "udp"};
net.performance = {"192.168.10.100", 30511, "udp"};
bool restart_required = false;
sdk.SetNetworkConfig(net, &restart_required);    // always true

// Mounting (metres, degrees)
afi::AfiRotation rot;
sdk.GetRotation(rot);
rot.yaw = 90.0f;
sdk.SetRotation(rot);

sdk.Restart();
sdk.ReconnectConfigApi();
```

After a destination switches to UDP, receive with `stream_transport = "udp"` (or `rdi_transport` / `shi_transport` / `spi_transport`) in `AfiSdkConfig`. The host transport must match the protocol the radar sends.

### Read / Write (Python)

```python
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, RangeMode, OK

sdk = AfiSdk()
if sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150")) != OK:
    raise SystemExit("Config API connect failed")

info   = sdk.get_device_info()                   # dict, or None on error
status = sdk.get_sensor_status()
net    = sdk.get_network_config()                # flat keys: detection_ip, detection_port, ...
mode   = sdk.get_range_mode()                    # int (RangeMode.*), or None
if info and mode is not None:
    print(info["serial_number"], info["software_version"], RangeMode.name(mode))

if sdk.enter_mode(AccessMode.DIAGNOSTIC) == OK:
    sdk.set_range_mode(RangeMode.LONG_RANGE)
    sdk.set_stream_destination("detection", "192.168.10.100", 30509, protocol="udp")
    sdk.set_network_config(health_ip="192.168.10.100", health_port=30510)   # partial update
    rot = sdk.get_rotation()                     # {yaw_deg, pitch_deg, roll_deg}
    if rot:
        sdk.set_rotation(yaw=90.0, pitch=rot["pitch_deg"], roll=rot["roll_deg"])
    sdk.restart()
    sdk.reconnect()
sdk.close()
```

`set_stream_destination(stream, ip, port, protocol="tcp")` takes `stream` as `"detection"`, `"health"` or `"performance"` and sends `protocol` (default `"tcp"`) every time.

### Range Mode Values

| Value | C++ `afi::AfiRangeMode` | Python `RangeMode` | Name |
|-------|-------------------------|--------------------|------|
| 0 | `kDualRange` | `DUAL_RANGE` | DR |
| 1 | `kLongRange` | `LONG_RANGE` | LR |
| 2 | `kMidRange` | `MID_RANGE` | MR |
| 3 | `kShortRange` | `SHORT_RANGE` | SR |
| 4 | `kUltraLongRange` | `ULTRA_LONG_RANGE` | ULR |
| 5 | `kUltraShortRange` | `ULTRA_SHORT_RANGE` | USR |
| 6 | `kUltraDualRange` | `ULTRA_DUAL_RANGE` | UDR (two subcycles, like DR) |

Values above 6 return `kInvalidParam` / `INVALID_PARAM` without sending. `AfiRangeModeName(mode)` and `RangeMode.name(mode)` return the name.

### Detection Tuning

```cpp
// C++: SNR level and 19-entry CFAR tables (levels 1..9, not physical dB)
afi::AfiDetectionThreshold thresh;
sdk.GetDetectionThreshold(thresh);
thresh.snr_dB = 6.0f;                            // wire key snr_threshold_db; fractions allowed
thresh.cfar_table_first_frame.assign(afi::AfiDetectionThreshold::kCfarTableLength, 5);
sdk.SetDetectionThreshold(thresh);               // second-frame table is used in DR and UDR only

// Eight filter toggles; the SDK sends all eight
afi::AfiDetectionFilters filters;
sdk.GetDetectionFilters(filters);
filters.filter_ground_reflection = true;
filters.filter_multibounce_2x = true;
sdk.SetDetectionFilters(filters);
```

```python
# Python (any subset; values outside 1..9 or tables not of 19 entries give INVALID_PARAM)
sdk.set_detection_threshold(snr_db=6.0, cfar_table_first_frame=[5] * 19)
sdk.set_detection_filters(filter_ground_reflection=True, filter_multibounce_2x=True)
```

| Filter | Default |
|--------|---------|
| `filter_ego_specular` | `false` |
| `filter_ground_sidelobe` | `true` |
| `filter_upper_structure` | `false` |
| `filter_multibounce_2x` | `true` |
| `filter_structure_multipath` | `true` |
| `filter_specular_mirror` | `true` |
| `filter_wheel_microdoppler` | `false` |
| `filter_ground_reflection` | `false` (SR subcycle) |

Interface v1.1.0 firmware stores the SNR level but does not use it in detection. It stores `filter_ego_specular`, `filter_upper_structure` and `filter_wheel_microdoppler` but does not build those stages.

### Time Sync (PTP)

`SetPtpConfig(profile)` / `SetPtpConfig(profile, &restart_required)` stores `0` (gPTP), `1` (1588v2-L2) or `2` (1588v2-UDP); values above 2 give `kInvalidParam`. `GetPtpConfig` returns the running `profile`, the stored `configured_profile`, `restart_required`, the live `state` (`"INIT"`, `"SLAVE"`, `"NOT_SYNCED"`), `offset_ns`, `mean_path_delay_ns`, `master_clock_id`, and `grid_offset_ns` / `grid_state` (system time versus PTP time). Python: `get_ptp_config()`, `set_ptp_config(profile=0)`, `set_ptp_config_with_result(profile=0)`.

### Full Config API Method Catalog

| Category (Method IDs) | C++ | Python |
|-----------------------|-----|--------|
| Connection (SDK side) | `GetLastSensorResult`, `GetInterfaceGeneration`, `ReconnectConfigApi` | `last_error`, `last_result`, `interface_generation`, `reconnect`, `close`, `connected` |
| Access Mode (`0x00E0`–`0x00E1`) | `GetMode`, `SetMode`, `EnterMode` | `get_mode`, `set_mode`, `enter_mode` |
| Device & Status (`0x0010`–`0x0016`) | `GetDeviceInfo`, `GetSensorStatus`, `GetDiagnosticInfo`, `Restart`, `ResetAllSettings`, `GetCommandHistoryInfo` | `get_device_info`, `get_sensor_status`, `get_diagnostic_info`, `restart`, `reset_all_settings`, `get_command_history_info` |
| Network (`0x0020`–`0x0021`) | `GetNetworkConfig`, `SetNetworkConfig`, `SetStreamDestination` | `get_network_config`, `set_network_config`, `set_stream_destination` |
| Range Mode (`0x0030`–`0x0031`) | `GetRangeMode`, `SetRangeMode` | `get_range_mode`, `set_range_mode` |
| Detection (`0x0040`–`0x0043`) | `GetDetectionThreshold`, `SetDetectionThreshold`, `GetDetectionFilters`, `SetDetectionFilters` | `get_detection_threshold`, `set_detection_threshold`, `get_detection_filters`, `set_detection_filters` |
| Time Sync (`0x0050`–`0x0051`) | `GetPtpConfig`, `SetPtpConfig` | `get_ptp_config`, `set_ptp_config`, `set_ptp_config_with_result` |
| Mounting (`0x0070`–`0x0075`) | `GetMountingPosition`, `SetMountingPosition`, `GetRotation`, `SetRotation`, `GetSensorPosition`, `SetSensorPosition` | `get_mounting_position`, `set_mounting_position`, `get_rotation`, `set_rotation`, `get_sensor_position`, `set_sensor_position` |
| Firmware Update (`0x0080`–`0x0087`) | `FirmwareUpdateStart`, `FirmwareDataChunk`, `FirmwareUpdateCommit`, `FirmwareUpdateStatus`, `FirmwareUpdateAbort`, `FirmwareSwitchSlot`, `FirmwareUpdateReboot`, `GetBootInfo` | `firmware_update_start`, `firmware_data_chunk`, `firmware_update_commit`, `firmware_update_status`, `firmware_update_abort`, `firmware_switch_slot`, `firmware_update_reboot`, `get_boot_info` |
| Config Persistence (`0x0091`–`0x0092`) | `GetConfigStatus`, `ResetConfigDefaults` | `get_config_status`, `reset_config_defaults` |

Signatures and field tables are in [docs/api_reference.md](docs/api_reference.md).

---

## 9. Firmware Update

The firmware update chain runs in the programming access mode, which halts RDI, SHII and SPI until the radar reboots. The radar must be on interface v1.1.0.

| `update_type` | Components | Written to |
|---------------|------------|------------|
| `"firmware"` | `m7_0`, `m7_1`, `m7_2`, `a53` (all four required) | The inactive slot (`target_slot`), activated by `FirmwareSwitchSlot` |
| `"bootloader"` | `bootloader` | In place, with a backup copy. No slot switch |

| Step | C++ | Python (`AfiSdk`) | ID |
|------|-----|-------------------|----|
| Open a session | `FirmwareUpdateStart(const AfiOtaStartRequest&, AfiOtaStartResponse&)` | `firmware_update_start(update_type, component, total_size, crc32)` | `0x0080` |
| Send a chunk | `FirmwareDataChunk(const uint8_t*, size_t, AfiOtaChunkResult*)` | `firmware_data_chunk(data)` → `{received_bytes, total_bytes, ...}` | `0x0081` |
| Commit (verify, write) | `FirmwareUpdateCommit()` | `firmware_update_commit()` | `0x0082` |
| Poll status | `FirmwareUpdateStatus(AfiOtaStatus&)` | `firmware_update_status()` | `0x0083` |
| Abort | `FirmwareUpdateAbort()` | `firmware_update_abort()` | `0x0084` |
| Activate the written slot | `FirmwareSwitchSlot("firmware", AfiOtaSwitchSlotResult&)` | `firmware_switch_slot("firmware")` | `0x0085` |
| Reboot | `FirmwareUpdateReboot()` | `firmware_update_reboot()` | `0x0086` |
| Slot state | `GetBootInfo(AfiBootInfo&)` | `get_boot_info()` | `0x0087` |

Rules the radar enforces:

- Every step except `FirmwareUpdateStatus` and `GetBootInfo` requires the programming mode.
- The radar holds one update session. Commit and slot switch continue after they answer, so poll `FirmwareUpdateStatus` until `state` is `"idle"` before the next start, switch or reboot.
- A chunk may not exceed `max_chunk_size` (4096). A chunk may be accepted only in part: `received_bytes` is the next offset, and the tail is sent again. Use the C++ overload with `AfiOtaChunkResult`.
- The radar aborts a transferring session after 10 s without an update request.
- `crc32` is the IEEE CRC-32 of the image, sent as `"0x%08X"`. An empty C++ string or Python `None` omits the key and the radar skips the image check.
- Switch the slot only after all four firmware commits reached `"idle"`. Reboot only while the status is `"idle"`.
- On any failure after a start, call `FirmwareUpdateAbort()` and start that component again. A transfer interrupted before the slot switch leaves the radar booting the previous slot.
- `AfiOtaStatus::error_message` is empty from `FirmwareUpdateStatus`. The reason of a failed step is in `GetLastSensorResult().error_message` / `last_result["error_message"]`.

### C++ Sequence

```cpp
#include <afi/afi_sdk.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <vector>

// IEEE 802.3 CRC-32 (zlib), sent by FirmwareUpdateStart as "0x%08X".
static std::string Crc32Hex(const std::vector<uint8_t>& d) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    char s[11];
    std::snprintf(s, sizeof(s), "0x%08X", static_cast<unsigned>(~c));
    return s;
}

// Poll FirmwareUpdateStatus until "idle".
static int WaitIdle(afi::AfiSdk& sdk, int timeout_s) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    while (std::chrono::steady_clock::now() < deadline) {
        afi::AfiOtaStatus st;
        int rc = sdk.FirmwareUpdateStatus(st);
        if (rc != afi::kOk) return rc;
        if (st.state == "idle") return afi::kOk;
        if (st.state == "error") return afi::kSensorError;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return afi::kTimeout;
}

// Start, chunks (resume at received_bytes), commit, wait for idle. Aborts on failure.
static int Upload(afi::AfiSdk& sdk, const char* type, const char* component,
                  const std::vector<uint8_t>& image, const std::string& expected_slot) {
    afi::AfiOtaStartRequest req;
    req.update_type = type;
    req.component   = component;
    req.total_size  = static_cast<uint32_t>(image.size());
    req.crc32       = Crc32Hex(image);
    afi::AfiOtaStartResponse start;
    int rc = sdk.FirmwareUpdateStart(req, start);
    if (rc != afi::kOk) return rc;
    if (!expected_slot.empty() && start.target_slot != expected_slot) {
        sdk.FirmwareUpdateAbort();
        return afi::kInvalidState;
    }

    size_t sent = 0;
    int stalled = 0;
    while (sent < image.size()) {
        const size_t len = std::min<size_t>(start.max_chunk_size, image.size() - sent);
        afi::AfiOtaChunkResult chunk;
        rc = sdk.FirmwareDataChunk(image.data() + sent, len, &chunk);
        if (rc == afi::kOk && chunk.received_bytes > image.size()) rc = afi::kProtocolError;
        if (rc == afi::kOk && chunk.received_bytes <= sent && ++stalled > 200) rc = afi::kTimeout;
        if (rc != afi::kOk) { sdk.FirmwareUpdateAbort(); return rc; }
        if (chunk.received_bytes > sent) { sent = chunk.received_bytes; stalled = 0; }
    }
    rc = sdk.FirmwareUpdateCommit();
    if (rc == afi::kOk)   // budget: max(120 s, size at 50 KiB/s)
        rc = WaitIdle(sdk, std::max(120, static_cast<int>(image.size() / (50 * 1024))));
    if (rc != afi::kOk) sdk.FirmwareUpdateAbort();
    return rc;
}

// images holds "m7_0", "m7_1", "m7_2" and "a53" (all four are required).
int UpdateFirmware(afi::AfiSdk& sdk, const std::map<std::string, std::vector<uint8_t>>& images) {
    if (sdk.EnterMode(afi::AfiAccessMode::kProgramming) != afi::kOk) return 1;   // halts RDI/SHII/SPI

    afi::AfiBootInfo boot;
    if (sdk.GetBootInfo(boot) != afi::kOk) return 1;
    const std::string target = boot.fw_update_active == "A" ? "B"
                             : boot.fw_update_active == "B" ? "A" : "";
    if (target.empty()) return 1;

    for (const char* comp : {"m7_0", "m7_1", "m7_2", "a53"})
        if (Upload(sdk, "firmware", comp, images.at(comp), target) != afi::kOk) return 1;

    afi::AfiOtaSwitchSlotResult sw;                       // after all four commits reached idle
    if (sdk.FirmwareSwitchSlot("firmware", sw) != afi::kOk || WaitIdle(sdk, 60) != afi::kOk) return 1;
    if (sdk.GetBootInfo(boot) != afi::kOk || boot.fw_update_active != target) return 1;

    sdk.FirmwareUpdateReboot();                           // the connection drops after the reply
    if (sdk.ReconnectConfigApi(120000) != afi::kOk) return 1;

    afi::AfiDeviceInfo info;                              // the running image after the reboot
    if (sdk.GetBootInfo(boot) != afi::kOk || sdk.GetDeviceInfo(info) != afi::kOk) return 1;
    printf("active=%s status=%s fw_hash=%s\n", boot.fw_update_active.c_str(),
           boot.boot_info_status.c_str(), info.fw_hash.c_str());
    return (boot.fw_update_active == target && boot.boot_info_status == "ok") ? 0 : 1;
}
```

For a bootloader update, call `Upload(sdk, "bootloader", "bootloader", image, "")` in the programming mode before the firmware components. The bootloader update is complete at `"idle"`. To confirm that the reboot happened, compare the Discovery `uptime_seconds` of the radar before and after, as `afi_sdk.firmware.wait_for_restart()` does, and compare `GetDeviceInfo().fw_hash` with the expected hash.

### Python Sequence (`afi_sdk.firmware`)

`afi_sdk.firmware` runs the complete sequence on a connected `AfiSdk`. It takes component images as bytes; bundle extraction, decryption and version selection belong to the caller.

```python
from afi_sdk import AfiSdk, AfiSdkConfig, firmware

IP = "192.168.10.150"
EXPECTED_FW_HASH = None                          # git short hash of the release, if known

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip=IP))

images = {}
for name in firmware.FIRMWARE_COMPONENTS:        # "m7_0", "m7_1", "m7_2", "a53"
    with open(f"{name}.bin", "rb") as f:
        images[name] = f.read()

uptime = firmware.radar_uptime(IP)               # Discovery uptime before the update
result = firmware.update(sdk, firmware=images, bootloader=None, reboot=True,
                         on_progress=lambda step, state, pct, rx, total: print(step, state, pct))
if not result.ok:
    print("update failed:", result.error)        # "<step>: <reason>"
elif result.rebooted:
    if uptime is not None:
        firmware.wait_for_restart(IP, uptime)    # Discovery shows a lower uptime
    sdk.reconnect()
    problems = firmware.verify(sdk, result.target_face, fw_hash=EXPECTED_FW_HASH)
    print(problems or "verified")
sdk.close()
```

`update()` enters the programming mode through Diagnostic, uploads the bootloader first when given, uploads `m7_0`, `m7_1`, `m7_2`, `a53` to the inactive slot (checking `target_slot` per component), switches the slot once after every commit reached idle and confirms it on Boot-Info, and with `reboot=True` sends `FirmwareUpdateReboot` while the session is idle. It stops at the first failure. A `firmware` dict that does not hold exactly the four components is refused before anything is sent.

| Function / type | Description |
|-----------------|-------------|
| `update(sdk, firmware=None, bootloader=None, reboot=True, on_progress=None) -> UpdateResult` | The complete sequence above |
| `upload_component(sdk, update_type, component, image, crc32=None, on_progress=None) -> StepResult` | Start, chunks until `received_bytes` equals the size, commit, wait for idle; aborts on failure |
| `wait_idle(sdk, timeout_s, step="", on_progress=None, total=0) -> Optional[str]` | Polls status until `"idle"`; `None` or the reason |
| `radar_uptime(ip, timeout_s=1.5, discover=None) -> Optional[int]` | Discovery `scan_status.uptime_seconds` of the radar at `ip` |
| `wait_for_restart(ip, uptime_before, timeout_s=120.0, interval_s=0.5, discover=None) -> Optional[dict]` | Waits until Discovery reports a lower uptime; the Discovery record or `None` |
| `verify(sdk, target_face, fw_hash=None, bootloader_hash=None) -> List[str]` | Checks Boot-Info (active, valid, `boot_info_status` `"ok"`) and device hashes; empty list when all pass |
| `hashes_match(reported, expected) -> bool` | Git short hashes match on a common prefix of 7+ characters, ignoring `-dirty` and case |
| `image_crc32(image) -> int` | IEEE CRC-32 that `FirmwareUpdateStart` checks |
| `commit_timeout_s(size) -> float` | `max(120 s, size / 50 KiB/s)` |
| `StepResult` | `step`, `ok`, `error`, `target_slot` |
| `UpdateResult` | `ok`, `steps` (list of `StepResult`), `target_face` (`"A"`/`"B"`, `None` for bootloader only), `rebooted`, `error` (property: `"<step>: <reason>"`) |

`on_progress(step, state, percent, received_bytes, total_bytes)` is called during transfers and idle waits. Constants: `FIRMWARE_COMPONENTS`, `BOOTLOADER_COMPONENT`, `DEFAULT_MAX_CHUNK_SIZE` (4096), `MAX_STALLED_CHUNKS` (200), `STEP_MODE`, `STEP_BOOT_INFO`, `STEP_SWITCH`, `STEP_REBOOT`.

---

## 10. Vehicle Input (CSII)

`SendVehicleInput()` sends one CSII message (Event `0x8001`, 115-byte frame: 16 B SOME/IP + 20 B E2E + 79 B ISO payload) to `sensor_ip:csii_port` (default 30501). Call it cyclically, every **100 ms** recommended. The SDK fills the interface header, maintains the message and E2E counters, and creates the socket on first use.

| Item | Behaviour |
|------|-----------|
| Precondition | C++: after `Init()`; `Start()` is not needed. Python: after `init()` |
| Transport | One UDP datagram on interface v1.1.0, a TCP connection on v1.0.x, chosen from the interface generation. UDP while the generation is unknown. `csii_transport = "udp"` / `"tcp"` forces one |
| Threading | Runs on the caller's thread, serialized internally |
| Errors | C++: `kNotInitialized` before `Init()`, `kConnectFailed` when the socket cannot be created or connected or the send fails. Python: `NOT_INITIALIZED`, `DISCONNECTED` |
| Access mode | Accepted in every mode |
| Interface v1.1.0 firmware | Stores the received values. SOMC and the global timestamp do not change sensor behaviour |

```cpp
// C++
afi::AfiVehicleInput vi;                         // sensor_id 1, somc NORMAL_MODE, rear-axle frame
vi.velocity_x_mps = 10.0f;                       // m/s forward
vi.rotation_rate_yaw_rps = 0.05f;                // rad/s
vi.steering_angle_rad = 0.02f;
vi.gear_position = BTS_CSII_GP_FORWARD;
int rc = sdk.SendVehicleInput(vi);               // timestamp_ns = 0 uses the host clock
```

```python
# Python
from afi_sdk import streams

rc = sdk.send_vehicle_input(
    velocity_mps=(10.0, 0.0, 0.0),               # x, y, z (m/s)
    rotation_rate_rps=(0.05, 0.0, 0.0),          # yaw, pitch, roll (rad/s)
    steering_angle_rad=0.02,
    gear_position=streams.GEAR_FORWARD,
)
```

| `AfiVehicleInput` field | Python keyword | Default |
|-------------------------|----------------|---------|
| `sensor_id` | `sensor_id` | `1` |
| `timestamp_ns` | `timestamp_ns` | `0` (host clock) |
| `somc` | `somc` | `BTS_CSII_SOMC_NORMAL_MODE` / `streams.SOMC_NORMAL_MODE` |
| `global_timestamp_utc` | `global_timestamp_utc` | `0.0` |
| `coord_system` | `coord_system` | `BTS_CSII_VCS_REAR_AXLE` / `0` |
| `velocity_x_mps`, `velocity_y_mps`, `velocity_z_mps` | `velocity_mps=(x, y, z)` | `0` |
| `rotation_rate_yaw_rps`, `rotation_rate_pitch_rps`, `rotation_rate_roll_rps` | `rotation_rate_rps=(yaw, pitch, roll)` | `0` |
| `wheel_flp_rps`, `wheel_frp_rps`, `wheel_rlp_rps`, `wheel_rrp_rps` | `wheel_speeds_rps=(fl, fr, rl, rr)` | `0` |
| `steering_angle_rad` | `steering_angle_rad` | `0` |
| `gear_position` | `gear_position` | `BTS_CSII_GP_NEUTRAL` / `streams.GEAR_NEUTRAL` |

---

## 11. Sensor Discovery

Discovery broadcasts one request on UDP 30520 and collects responses for one timeout window (default 3 s). Each sensor answers by unicast and by limited broadcast; the SDK keeps one response per (source IP, serial number). A response can be lost, so scan again before treating a sensor as absent. On a multi-NIC host, pass the local IP of the NIC on the sensor subnet. Discovery answers in every access mode and needs no `Init()`.

```cpp
// C++: static, no instance needed. Discover(timeout_ms = 3000, port = 30520, bind_ip = "")
auto sensors = afi::AfiSdk::Discover(3000, 30520, "192.168.10.100");
for (const auto& s : sensors) {
    printf("%s  %s  sw=%s  gen=%d\n", s.serial_number.c_str(), s.ip.c_str(),
           s.software_version.c_str(), static_cast<int>(s.generation));   // 1 = v1.0.x, 2 = v1.1.0
}
```

```python
# Python: list of dicts as the sensor sent them, plus _source_ip, _source_port, _interface_generation
from afi_sdk import AfiSdk, INTERFACE_GEN_V1_0

for s in AfiSdk.discover(timeout_s=3.0, bind_ip="192.168.10.100"):
    print(s["_source_ip"], s.get("serial_number"), s.get("software_version"),
          s.get("ip_address"), (s.get("scan_status") or {}).get("uptime_seconds"),
          "v1.0.x" if s["_interface_generation"] == INTERFACE_GEN_V1_0 else "")
```

| Key `AfiSensorInfo` fields | Description |
|----------------------------|-------------|
| `serial_number`, `model`, `hw_version`, `software_version`, `mac_address` | Identity and unified software version |
| `bootloader_hash`, `fw_hash` | Git short hashes of the running images |
| `ip`, `source_ip` | Address the sensor reports (`source_ip` when absent) / address the response came from |
| `subnet_mask`, `gateway`, `port` | Radar network; `port` is the Config API port (30500) |
| `detection_port`, `health_port`, `performance_port` | RDI / SHII / SPI ports |
| `detection_protocol`, `health_protocol`, `performance_protocol` | `"tcp"` or `"udp"` |
| `detection_ip`, `health_ip`, `performance_ip`, `host_ip` | Stream destination IPs |
| `range_mode`, `range_mode_name` | Stored range mode |
| `position_code`, `offset_x/y/z`, `yaw_deg`, `pitch_deg`, `roll_deg` | Mounting values |
| `radar_active`, `scan_index`, `uptime_seconds` | Operation state, current MessageCounter, uptime (s) |
| `ptp_state`, `ptp_offset_ns` | PTP status |
| `generation` | `kV1_0` when the response holds `driver_hash`, `kV1_1` when it holds an `m72_*` key |

The Python dict carries the radar IP under `ip_address` and the uptime under `scan_status.uptime_seconds`.

---

## 12. Multi-Sensor Setup

The SDK supports up to **4 sensors** at the same time. Use one independent `AfiSdk` instance per sensor; each owns its sockets, threads and callbacks.

- **TCP streams (default):** each instance connects to its own sensor's IP, so every sensor keeps the default stream ports. Only `sensor_ip` differs.
- **UDP streams:** the host binds each stream port, so each sensor needs its own host ports. The convention is `+100` per sensor ([port table](#16-port--protocol-reference)). Write each sensor's destinations in the diagnostic mode, restart it, and set the same ports in that instance's `AfiSdkConfig`. Each UDP receiver accepts only its own sensor's datagrams.
- The Config API needs no offset: it is an outbound TCP connection to each sensor's port 30500.

```cpp
// C++: one instance per sensor (TCP, default ports)
#include <afi/afi_sdk.h>
#include <cstdio>
#include <memory>
#include <vector>

const char* ips[] = {"192.168.10.150", "192.168.10.151"};

std::vector<std::unique_ptr<afi::AfiSdk>> sdks;
for (const char* ip : ips) {
    afi::AfiSdkConfig cfg;
    cfg.sensor_ip = ip;
    cfg.enable_rdi = true;
    cfg.enable_config_api = false;
    // UDP instead: cfg.stream_transport = "udp"; cfg.rdi_port = 30509 + 100 * index; (same for shi_port, spi_port)

    auto sdk = std::make_unique<afi::AfiSdk>();
    sdk->RegRecvCallback([ip](const afi::AfiRdiFrame& f) {
        printf("[%s] detections=%u\n", ip, f.message.num_detections);
    });
    if (sdk->Init(cfg) == afi::kOk && sdk->Start() == afi::kOk)
        sdks.push_back(std::move(sdk));
}
```

```python
# Python: write per-sensor UDP destinations (+100 per sensor), then restart each sensor
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, OK

HOST_IP = "192.168.10.100"                       # host NIC on the sensor subnet
PORT_OFFSET_PER_SENSOR = 100

for idx, ip in enumerate(["192.168.10.150", "192.168.10.151"]):
    offset = idx * PORT_OFFSET_PER_SENSOR
    sdk = AfiSdk()
    if sdk.init(AfiSdkConfig(sensor_ip=ip)) != OK or sdk.enter_mode(AccessMode.DIAGNOSTIC) != OK:
        print(ip, "refused:", sdk.last_result)
        sdk.close()
        continue
    resp = sdk.set_network_config(               # partial update; NVM; applies after restart
        detection_ip=HOST_IP,   detection_port=30509 + offset,   detection_protocol="udp",
        health_ip=HOST_IP,      health_port=30510 + offset,      health_protocol="udp",
        performance_ip=HOST_IP, performance_port=30511 + offset, performance_protocol="udp",
    )
    if resp is not None:
        sdk.restart()
        sdk.reconnect(timeout_s=90)
    sdk.close()
```

---

## 13. Interface v1.0.x Compatibility

SDK 2.2.0 receives data from sensors on interface v1.0.x (firmware 3.0.0 to 3.1.0) as well as interface v1.1.0. The Config API and the firmware update are interface v1.1.0 only. Support for v1.0.x ends with this release.

| Function | Interface v1.1.0 | Interface v1.0.x (FW 3.0.0 to 3.1.0) |
|----------|-----------------------------|--------------------------------------|
| Discovery | Supported | Supported |
| RDI, SHII, SPI reception | Supported | Supported |
| E2E CRC | CRC-32 (IEEE 802.3) over the body; CRC high word 0 | CRC-64/XZ over Length, Counter, Data ID and the body, in bytes 0 to 7; accepted on receive |
| CSII | One UDP datagram to port 30501 | TCP connection to port 30501 |
| Config API | Supported | `kNotSupported` / `NOT_SUPPORTED`, nothing sent |
| Firmware update | Supported | `kNotSupported` / `NOT_SUPPORTED`, nothing sent |

How the SDK learns the generation:

- Each Config API connection opens with a read-only GetDeviceInfo request. A v1.0.x answer closes the connection, and every later Config API call returns `kNotSupported` without sending until the next connect. `GetLastSensorResult()` then reads `NOT_SUPPORTED` with an `error_message`, and Python `connected` is `False`.
- With the Config API off, the generation comes from the last stream frame that passed the E2E check.
- `GetInterfaceGeneration()` (C++) / `interface_generation` (Python) report `kUnknown` (0), `kV1_0` (1) or `kV1_1` (2) / `INTERFACE_GEN_UNKNOWN`, `INTERFACE_GEN_V1_0`, `INTERFACE_GEN_V1_1`.
- Per frame: `generation` in `AfiRdiFrame`, `AfiStreamMeta` and `AfiWirePayloadFrame`; Python `strip_e2e()` returns `e2e["generation"]`. Per sensor: `AfiSensorInfo::generation` and the Python Discovery key `_interface_generation` (`discovery_generation(info)`).
- With the Config API off and before the first stream frame, the generation is unknown and CSII uses UDP. Set `csii_transport = "tcp"` to reach a v1.0.x sensor from the first call.

The stream layouts are the same in both generations and the SDK passes every value through unchanged. These fields mean different things on v1.0.x; the Common Header interface version (`1.0.0` or `1.1.0`) tells the generations apart.

| Field | Interface v1.0.x | Interface v1.1.0 |
|-------|------------------|------------------|
| RDI `existence_probability` | Marker 100, 60 or 40 | Probability × 100; augmented detections 95 or 90 |
| RDI DataQualifier | 0, 5 (test mode), 6 | 0, 1, 6 |
| RDI MessageCounter | Send counter | Scan index; repeats on a DataQualifier 6 resend |
| RDI RecognisedDetectionsCapability | 4096 (FW 3.0.x) or 8192 | 2048 |
| RDI radial velocity domain | ±50 m/s (FW 3.0.0) | From −111.11 m/s |
| SHII sensor operation mode | `150` can occur | `6` (UDR) can occur |
| SHII input signal statuses | 1 entry | 4 entries |
| E2E Counter | Per stream, from 1 on each TCP connection | Send slot shared by RDI, SHII and SPI |

---

## 14. Key Data Structures

### `AfiSdkConfig` (C++)

| Field | Default | Description |
|-------|---------|-------------|
| `sensor_ip` | `"192.168.10.150"` | Sensor IP. UDP receivers accept datagrams only from this address |
| `config_port` | `30500` | Config API TCP port |
| `rdi_port` | `30509` | RDI port (TCP: sensor port to connect to; UDP: host port to bind) |
| `shi_port` | `30510` | SHII port |
| `spi_port` | `30511` | SPI port |
| `csii_port` | `30501` | CSII destination port |
| `stream_transport` | `"tcp"` | Default stream transport, `"tcp"` or `"udp"` |
| `rdi_transport`, `shi_transport`, `spi_transport` | empty | Per-stream override; empty uses `stream_transport` |
| `csii_transport` | empty | Empty picks it from the generation (UDP on v1.1.0, TCP on v1.0.x, UDP while unknown); `"udp"` or `"tcp"` forces one |
| `enable_rdi` | `true` | Receive RDI |
| `enable_shi` | `false` | Receive SHII |
| `enable_spi` | `false` | Receive SPI |
| `enable_config_api` | `true` | Connect the Config API on `Init()`. A failed connect is not fatal; later calls connect on demand |
| `validate_e2e` | `true` | Check the E2E CRC of received frames (Length and Data ID are always checked) |
| `e2e_strict` | `false` | Drop frames that fail the E2E check; otherwise warn and deliver |
| `socket_buffer_size` | `16 MiB` | `SO_RCVBUF` for TCP and UDP receivers |
| `connect_timeout_ms` | `5000` | TCP connect timeout |
| `request_timeout_ms` | `5000` | Config API response timeout |

The stream ports and transports describe where the host receives. The radar sends to the destinations stored by `SetNetworkConfig` / `SetStreamDestination`, which apply after `Restart()`.

The Python `AfiSdkConfig` dataclass has `sensor_ip`, `config_port`, `csii_port`, `rdi_port`, `shi_port`, `spi_port`, `timeout` (`5.0` s, Config API connect and response), `stream_transport`, `validate_e2e` and `csii_transport`. `AfiSdk` uses `sensor_ip`, `config_port`, `csii_port`, `timeout` and `csii_transport`; the stream fields are for the caller's own receive code.

### `AfiRdiFrame`

| Field | Type | Description |
|-------|------|-------------|
| `message` | `bts_rdi_message_t` | ISO 23150 RDI message; `message.detections` points into `detections_storage` |
| `recv_timestamp_ns` | `uint64_t` | Host receive time (steady clock) |
| `frame_seq` | `uint32_t` | SDK frame sequence number |
| `detections_storage` | `std::vector<bts_rdi_detection_t>` | Owned detections |
| `e2e_counter` | `uint32_t` | E2E Counter (send slot) |
| `e2e_checked` | `bool` | `validate_e2e` was on |
| `e2e_valid` | `bool` | Length, Data ID and CRC checks passed |
| `generation` | `AfiInterfaceGeneration` | Sender's generation from the E2E header |

`message` holds the Common Header (`interface_version`, `sensor_id`, `timestamp` in ns, `message_counter`, `interface_cycle_time`, `data_qualifier`), `ambiguity_domain.radial_velocity_begin` / `radial_velocity_end`, `recognised_detections_capability`, `num_detections` and `detections`. An RDI payload is a 36-byte header plus 51 bytes per detection, up to 8192 detections; interface v1.1.0 firmware sends at most 2048.

### Detection Fields (`bts_rdi_detection_t`, 51 bytes)

Positions are spherical. Convert to Cartesian in your own code if needed.

| C++ field | Python key (`parse_rdi_detection`) | Unit | Description |
|-----------|------------------------------------|------|-------------|
| `existence_probability` | `existence_probability` | % | 0 to 100 |
| `detection_id` | `detection_id` | | `0xFFFF` if unused |
| `object_id_reference` | `object_id_reference` | | Associated object ID (reserved) |
| `timestamp_difference` | `timestamp_difference` | s | Offset to the message timestamp |
| `radar_cross_section` | `rcs` | dBsm | Radar cross-section |
| `signal_to_noise_ratio` | `snr` | dB | |
| `ambiguity_grouping_id` | `ambiguity_grouping_id` | | `0xFFFF` if unused |
| `position_radial_distance` | `radial_distance` | m | Range |
| `position_azimuth` | `azimuth` | rad | |
| `position_elevation` | `elevation` | rad | |
| `position_radial_distance_error` | `radial_distance_error` | m | |
| `position_azimuth_error` | `azimuth_error` | rad | |
| `position_elevation_error` | `elevation_error` | rad | |
| `relative_velocity_radial` | `radial_velocity` | m/s | Radial velocity |
| `relative_velocity_radial_error` | `radial_velocity_error` | m/s | |

### `AfiShiMessage` (= `bts_shi_message_t`, sensor health)

Common Header fields, `num_valid_operation_modes` / `sensor_operation_modes[]` (DR..UDR `0..6`, Degradation `10`, Evaluation `50`, Calibration `100`, Initialising `200`, Test `201`), `sensor_defect_recognised`, `sensor_defect_reason`, `supply_voltage_status`, `sensor_temperature_status`, `num_valid_input_signal_statuses` / `sensor_input_signal_types[]` / `sensor_input_signal_statuses[]`, `sensor_time_sync`, and `num_valid_calibration_components` / `sensor_calibration_components[]` / `sensor_calibration_statuses[]` / `sensor_calibration_states[]`. Variable length.

### `AfiSpiMessage` (= `bts_spi_message_t`, sensor performance and pose)

| Field | Description |
|-------|-------------|
| `vehicle_coordinate_system` | Rear axle (0) or road level (1) |
| `sensor_pose.origin_point_x/y/z` | Mounting position (m) |
| `sensor_pose.orientation_yaw/pitch/roll` | Mounting orientation (rad) |
| `sensor_pose.orientation_error_yaw/pitch` | Orientation errors (rad) |
| `num_fov_segments`, `fov_segments[]` | Azimuth and elevation limits, `blockage_status` (`BTS_SPI_BS_NONE`, `_PARTIAL_BLOCKAGE`, `_FULL_BLOCKAGE`, `_UNKNOWN`) |
| `num_recognisable_object_types`, `recognisable_object_types[]` | Object class and detection range |
| `num_reference_target_types`, `reference_target_types[]` | RCS, detection range and SNR of reference targets |

The Config API rotation (`AfiRotation`, `get_rotation()`) is in degrees; the SPI pose is in radians.

### `AfiStreamMeta` / `AfiWirePayloadFrame`

| Field | `AfiStreamMeta` | `AfiWirePayloadFrame` | Description |
|-------|:---:|:---:|-------------|
| `stream` | | ✓ | `AfiStreamKind::kDetection`, `kHealth` or `kPerformance` |
| `recv_timestamp_ns` | ✓ | ✓ | Host receive time (steady clock) |
| `sensor_timestamp_ns`, `message_counter` | | ✓ | ISO Common Header timestamp and MessageCounter |
| `e2e_counter`, `e2e_checked`, `e2e_valid` | ✓ | ✓ | E2E Counter and verdict |
| `payload` | | ✓ | E2E header plus ISO payload, exactly as reassembled |
| `generation` | ✓ | ✓ | Sender's generation |

### Config API Result Types

| Type | Fields |
|------|--------|
| `AfiModeStatus` | `access_mode` (`AfiAccessMode`), `access_mode_name` (`"normal"` / `"diagnostic"` / `"programming"`), `elapsed_ms`, `streams_halted` |
| `AfiSensorResult` | `return_code` (`-1` when no response arrived), `return_name` (for example `"SESSION_REQUIRED"`), `error_message` (firmware update methods), `e2e_ok` |
| `AfiInterfaceGeneration` | `kUnknown` (0), `kV1_0` (1), `kV1_1` (2) |
| `AfiDeviceInfo` | `serial_number`, `mac_address`, `model_name`, `hw_version`, `software_version`, `bootloader_hash`, `fw_hash` |
| `AfiSensorStatus` | `operation_state`, `operation_state_code`, `temperature_mcu`, `temperature_rf`, `voltage`, `uptime_sec` |
| `AfiNetworkConfig` | `ip_address`, `subnet_mask`, `gateway`, `api_port`, `vlan_id`, `detection`, `health`, `performance` (`AfiStreamDestination`: `ip`, `port`, `protocol`) |
| `AfiBootInfo` | `fw_update_active`, `fw_update_valid_a`, `fw_update_valid_b`, `fw_update_boot_count`, `boot_info_status` |
| `AfiOtaStatus` | `state` (`"idle"`, `"transferring"`, `"verifying"`, `"writing"`, `"error"`), `progress_pct`, `current_component`, `error_message` |

---

## 15. Error Codes

C++ codes are the `afi::AfiError` enum in `afi_error.h`. Python constants have the same values; compare by name.

| C++ (`afi::`) | Value | Python | Meaning |
|---------------|-------|--------|---------|
| `kOk` | `0` | `OK` | Success |
| `kNotInitialized` | `-1` | `NOT_INITIALIZED` | `Init()` not called, Config API disabled, or after `Stop()` |
| `kAlreadyRunning` | `-2` | (none) | `Init()` while initialized or running; call `Stop()` first |
| `kConnectFailed` | `-3` | `CONNECT_FAILED` | Stream or CSII socket could not connect; `ReconnectConfigApi` / `reconnect` timed out; Python `init()` could not connect |
| `kTimeout` | `-4` | `TIMEOUT` | No Config API response within the timeout; the next call reconnects |
| `kDisconnected` | `-5` | `DISCONNECTED` | Config API not connected, connect or send failed, or connection lost; Python CSII socket error |
| `kInvalidParam` | `-6` | `INVALID_PARAM` | Argument rejected by the SDK or the radar (`INVALID_PARAM`), or request body above 16,384 B |
| `kProtocolError` | `-7` | `PROTOCOL_ERROR` | Response does not answer the request, or its content is malformed |
| `kSensorError` | `-8` | `SENSOR_ERROR` | Other radar return code (for example `INTERNAL_ERROR`) |
| `kInternalError` | `-9` | `INTERNAL_ERROR` | Internal SDK error |
| `kNotSupported` | `-10` | `NOT_SUPPORTED` | Radar `NOT_SUPPORTED`, or an interface v1.0.x sensor (nothing sent) |
| `kBusy` | `-11` | `BUSY` | Radar `BUSY`: more than 200 requests per second |
| `kSessionRequired` | `-12` | `SESSION_REQUIRED` | Radar `SESSION_REQUIRED`: method not open in the current access mode |
| `kAccessDenied` | `-13` | `ACCESS_DENIED` | Radar `ACCESS_DENIED`: mode re-entry or a step back; reboot to leave Programming |
| `kInvalidState` | `-14` | `INVALID_STATE` | Radar `INVALID_STATE`: step out of order, for example update session not idle |

| Radar return code | C++ | Python |
|-------------------|-----|--------|
| `SUCCESS` | `kOk` | `OK` |
| `INVALID_PARAM` | `kInvalidParam` | `INVALID_PARAM` |
| `NOT_SUPPORTED` | `kNotSupported` | `NOT_SUPPORTED` |
| `BUSY` | `kBusy` | `BUSY` |
| `SESSION_REQUIRED` | `kSessionRequired` | `SESSION_REQUIRED` |
| `ACCESS_DENIED` | `kAccessDenied` | `ACCESS_DENIED` |
| `INVALID_STATE` | `kInvalidState` | `INVALID_STATE` |
| Any other code | `kSensorError` | `SENSOR_ERROR` |

- C++: `afi::AfiErrorStr(code)` returns a readable message. `GetLastSensorResult()` holds the radar's `return_code`, `return_name`, `error_message` and `e2e_ok`. `afi::AfiErrorFromReturnCode(rc)` and `afi::AfiReturnCodeName(rc)` map and name radar return codes.
- Python: `sdk.last_error` holds the code of the last Config API call, and `sdk.last_result` holds `{return_code, return_name, error_message, data, e2e_ok}`. For a transport failure, `return_code` is negative and `return_name` is `"TRANSPORT"`, `"TIMEOUT"`, `"PROTOCOL"` or `"TOO_LARGE"`. `error_from_result(rc)` maps a `ConfigClient` result to the SDK codes.

---

## 16. Port & Protocol Reference

| Port | Proto | Dir | Event ID | E2E Data ID | Purpose |
|------|-------|-----|----------|-------------|---------|
| 30500 | TCP | ↔ | | `0x6000 << 16 \| Method ID` | Config API (SOME/IP + JSON; raw binary firmware chunks), Method IDs `0x0010` to `0x00E1` |
| 30501 | UDP (v1.1.0), TCP (v1.0.x) | → | `0x8001` | `0x60008001` | CSII vehicle input |
| 30509 | TCP/UDP | ← | `0x8002` | `0x60008002` | RDI point cloud |
| 30510 | TCP/UDP | ← | `0x8003` | `0x60008003` | SHII health |
| 30511 | TCP/UDP | ← | `0x8004` | `0x60008004` | SPI performance |
| 30520 | UDP broadcast | ↔ | | (none) | Discovery |

Over TCP (default) the host connects to the sensor's stream port. Over UDP the host binds the stream port. SOME/IP Service ID is `0x6000`. The radar sends one RDI, SHII and SPI message per 100 ms send slot.

**Protocol stack (data streams):**
```
User callback / afi_sdk.streams parser
    ↑
ISO 23150 payload (binary, little-endian)
    ↑
E2E header (20 B, big-endian: CRC high 0 | CRC-32 | Length | Counter | Data ID)
    ↑
SOME/IP header (16 B, big-endian)  [+ 4 B SOME/IP-TP header on segmented UDP RDI]
    ↑
TCP / UDP
```

| E2E offset | Size | Field | Description |
|------------|------|-------|-------------|
| 0 | 4 | CRC high | `0x00000000` on interface v1.1.0 |
| 4 | 4 | CRC | IEEE 802.3 CRC-32 over the body only (KAT `0xCBF43926`). A zero CRC never passes |
| 8 | 4 | Length | Body length, excluding the 20-byte header |
| 12 | 4 | Counter | Data streams: send slot. Config API: sequence |
| 16 | 4 | Data ID | `0x6000 << 16` plus the Event ID or Method ID |

**Multi-sensor host receive-port convention (UDP, +100 per sensor):**

| Sensor | RDI | SHII | SPI |
|--------|-----|------|-----|
| 1 | 30509 | 30510 | 30511 |
| 2 | 30609 | 30610 | 30611 |
| 3 | 30709 | 30710 | 30711 |
| 4 | 30809 | 30810 | 30811 |

Over TCP each sensor keeps the default ports, so no offset is needed.

---

## 17. Troubleshooting Checklist

| Symptom | Check |
|---------|-------|
| `ping` fails | Host IP in `192.168.10.x`? Cable, NIC and firewall |
| `Init()` returns `kConnectFailed` | A TCP stream could not connect or a UDP port could not be bound. Check `sensor_ip`, cable, subnet, and other processes on the port |
| `Init()` returns `kInvalidParam` | A transport string other than `"tcp"` or `"udp"` (`csii_transport` also accepts empty) |
| `Start()` succeeds but no callbacks | `GetNetworkConfig` destination IP, port and protocol must match `AfiSdkConfig`. Over UDP, datagrams from an address other than `sensor_ip` are dropped. A unicast destination IP also limits TCP stream clients to that IP |
| Callbacks fire but `num_detections == 0` | Object in the FoV? Range mode (`GetRangeMode`)? DataQualifier `1` means the content must not be used |
| A SET returns `kSessionRequired` | Call `EnterMode(kDiagnostic)` before the SET |
| `SetMode` / `EnterMode` returns `kAccessDenied` | Re-entry, a step back, or the radar is in Programming. Restart the radar |
| A SET succeeded but nothing changed | Settings apply after `Restart()`; then `ReconnectConfigApi()` |
| Data streams stopped | The programming mode halts RDI, SHII and SPI until reboot (`GetMode().streams_halted`) |
| Every Config API call returns `kNotInitialized` | `enable_config_api = false`, before `Init()`, or after `Stop()` |
| Every Config API call returns `kNotSupported` | Interface v1.0.x sensor: `GetInterfaceGeneration() == kV1_0`. Data streams and CSII still work |
| `kTimeout` / `kDisconnected` on Config API calls | The radar serves one Config API client; close other clients. The next call reconnects |
| `kBusy` | More than 200 requests per second |
| CSII has no effect on a v1.0.x sensor with the Config API off | Generation unknown, so CSII used UDP. Set `csii_transport = "tcp"` |
| E2E warnings in the log | Check `e2e_valid` per frame; `e2e_strict = true` drops failing frames |
| RDI loss callback reports loss | Over UDP, raise `socket_buffer_size` or use TCP. A gap also counts slots not sent to this host (before the first scan, TCP backlog) |
| Discovery returns nothing | Same subnet? Pass `bind_ip`; UDP 30520 not firewalled; scan again |
| Test the receive path without a radar | `python tools/rdi_udp_sender.py 127.0.0.1 30509 50 10` with `sensor_ip = "127.0.0.1"`, `stream_transport = "udp"`, `enable_config_api = false` |

---

## Quick Reference Card

```
INIT      → sdk.Init(cfg)                          one AfiSdk per sensor
STREAM    → sdk.RegRecvCallback(cb)                RDI / SHII / SPI (+ AfiStreamMeta) / loss
          → sdk.RegWirePayloadCallback(cb)         E2E header + ISO payload before parsing
RUN       → sdk.Start()   |   STOP → sdk.Stop()
CONFIG    → GetX(out) / SetX(in)                   int: kOk or negative AfiError
MODE      → sdk.EnterMode(afi::AfiAccessMode::kDiagnostic)      before any SET
APPLY     → sdk.Restart(); sdk.ReconnectConfigApi();             no save call
FIRMWARE  → EnterMode(kProgramming) → Start/Chunk/Commit/idle x4 → SwitchSlot → Reboot
VEHICLE   → sdk.SendVehicleInput(vi)               after Init, every 100 ms
DISCOVER  → afi::AfiSdk::Discover()                static, no Init needed
DIAGNOSE  → sdk.GetLastSensorResult(), sdk.GetInterfaceGeneration()
PYTHON    → afi_sdk.AfiSdk (Config API, CSII, discover), afi_sdk.streams (parsers),
            afi_sdk.firmware.update() (complete firmware update)
```

**Key files to read next:**
- [examples/cpp/sample_pointcloud.cpp](examples/cpp/sample_pointcloud.cpp)
- [examples/cpp/sample_data_stream.cpp](examples/cpp/sample_data_stream.cpp)
- [examples/cpp/sample_config.cpp](examples/cpp/sample_config.cpp)
- [python/examples/sample_data_stream.py](python/examples/sample_data_stream.py)
- [python/examples/sample_multi_sensor.py](python/examples/sample_multi_sensor.py)
- [docs/api_reference.md](docs/api_reference.md) (complete API reference)
