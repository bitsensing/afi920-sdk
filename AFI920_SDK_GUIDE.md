# AFI920 SDK — AI Integration Guide

> **Who this is for:** AI agents, LLM-driven automation, and developers who need the fastest path to receiving data from a bitsensing AFI920 4D imaging radar.  
> **SDK version:** 2.1.0 · **Protocol:** AFI920 v3.0 · **License:** BSD-3-Clause

---

## Table of Contents

1. [What the SDK Does](#1-what-the-sdk-does)
2. [Repository Layout](#2-repository-layout)
3. [Network Prerequisites](#3-network-prerequisites)
4. [Build & Install](#4-build--install)
5. [Quickstart: Receive Point Cloud Data](#5-quickstart-receive-point-cloud-data)
6. [All Three Data Streams](#6-all-three-data-streams)
7. [Configuration API](#7-configuration-api)
8. [Vehicle Input (CSII)](#8-vehicle-input-csii)
9. [Sensor Discovery](#9-sensor-discovery)
10. [Multi-Sensor Setup](#10-multi-sensor-setup)
11. [Key Data Structures](#11-key-data-structures)
12. [Error Codes](#12-error-codes)
13. [Port & Protocol Reference](#13-port--protocol-reference)
14. [Troubleshooting Checklist](#14-troubleshooting-checklist)

---

## 1. What the SDK Does

The AFI920 SDK lets a host computer communicate with one or more bitsensing AFI920 radars over Ethernet.

| Direction | Channel | What travels |
|-----------|---------|-------------|
| Radar → Host | RDI (TCP/UDP 30509) | Point cloud detections (range, azimuth, elevation, velocity, RCS, SNR…) |
| Radar → Host | SHII (TCP/UDP 30510) | Sensor health info |
| Radar → Host | SPI (TCP/UDP 30511) | Sensor performance & mounting pose |
| Host → Radar | Config API (TCP 30500) | ~30 read/write config methods (7 categories) |
| Host → Radar | CSII (TCP 30501) | Vehicle dynamics input |
| Host ↔ Radar | Discovery (UDP 30520) | Find sensors on LAN |

All data streams use **SOME/IP framing + AUTOSAR E2E Profile 7 (CRC-64/XZ)**. The SDK handles all of this transparently.

---

## 2. Repository Layout

```
afi920-sdk/
├── include/afi/
│   ├── afi_sdk.h        ← main C++ API (AfiSdk class)
│   ├── afi_types.h      ← all data structures
│   ├── afi_config.h     ← AfiSdkConfig struct
│   └── afi_error.h      ← error codes
├── src/                 ← C++ implementation (build artifact: libafi_sdk.a)
├── python/src/afi_sdk/  ← Python package
│   ├── _sdk.py          ← AfiSdk Python class (Config API)
│   ├── someip.py        ← SOME/IP framing + Config client + discovery
│   └── streams.py       ← low-level E2E / ISO 23150 stream parsers
├── examples/cpp/        ← 7 ready-to-run C++ programs
├── python/examples/     ← 6 ready-to-run Python programs
└── docs/api_reference.md← complete API reference
```

> The ROS 2 driver lives in a **separate** repository (`afi920-ros2-driver`), not inside this SDK tree.

---

## 3. Network Prerequisites

| Item | Default value |
|------|--------------|
| Sensor IP | `192.168.10.150` |
| Host IP | `192.168.10.100` |
| Subnet | `255.255.255.0` |

**Required before running any code:**
1. Connect host to radar over Ethernet.
2. Assign host a static IP in the `192.168.10.x` subnet.
3. Verify connectivity: `ping 192.168.10.150`

---

## 4. Build & Install

### C++ (CMake 3.16+, C++17)

```bash
cd afi920-sdk
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_EXAMPLES=ON        # optional: builds examples/
cmake --build . -j$(nproc)
# Output: build/src/libafi_sdk.a
```

**Link in your own CMakeLists.txt:**
```cmake
add_subdirectory(afi920-sdk)
target_link_libraries(my_app PRIVATE afi_sdk)
```

### Python (3.8+, no external dependencies)

```bash
cd afi920-sdk/python
pip install .
# or for development:
pip install -e .
```

---

## 5. Quickstart: Receive Point Cloud Data

### C++

```cpp
#include <afi/afi_sdk.h>
#include <cstdio>
#include <thread>
#include <chrono>

int main() {
    afi::AfiSdkConfig cfg;
    cfg.sensor_ip = "192.168.10.150";   // default sensor IP
    cfg.enable_rdi = true;
    cfg.enable_config_api = false;      // data-only mode

    afi::AfiSdk sdk;

    // Register callback — called on every RDI frame (~10 Hz).
    // Positions are spherical (range/azimuth/elevation), not Cartesian.
    sdk.RegRecvCallback([](const afi::AfiRdiFrame& frame) {
        const auto& m = frame.message;
        printf("Frame %u  detections: %u\n", frame.frame_seq, m.num_detections);
        for (uint16_t i = 0; i < m.num_detections; ++i) {
            const auto& d = m.detections[i];
            printf("  [%u] r=%.2f m  az=%.3f rad  el=%.3f rad  vel=%.2f m/s  rcs=%.1f dBsm\n",
                   i,
                   d.position_radial_distance,
                   d.position_azimuth,
                   d.position_elevation,
                   d.relative_velocity_radial,
                   d.radar_cross_section);
        }
    });

    if (sdk.Init(cfg) != afi::kOk) return 1;
    sdk.Start();

    std::this_thread::sleep_for(std::chrono::seconds(10));
    sdk.Stop();
}
```

### Python

> The Python `AfiSdk` class provides the **Config API only**. Data streams are
> received with a plain socket and decoded by the `afi_sdk.streams` /
> `afi_sdk.someip` parsers. (See `python/examples/sample_pointcloud.py` for the
> full TCP + UDP version with SOME/IP-TP reassembly.)

```python
import socket, struct
from afi_sdk.someip import SOMEIP_HEADER_SIZE, DEFAULT_RDI_PORT
from afi_sdk.streams import strip_e2e, parse_rdi_frame, parse_rdi_detection

def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("192.168.10.150", DEFAULT_RDI_PORT))   # TCP RDI stream

while True:
    hdr = recv_exact(sock, 8)                          # SOME/IP MessageID(4)+Length(4)
    if len(hdr) < 8:
        break
    length = struct.unpack(">I", hdr[4:8])[0]
    data = hdr + recv_exact(sock, length)
    _, iso = strip_e2e(data[SOMEIP_HEADER_SIZE:])      # strip the 20B E2E header
    frame = parse_rdi_frame(iso)
    if not frame:
        continue
    print(f"detections={frame['num_detections']}  counter={frame['message_counter']}")
    if frame["num_detections"] > 0:
        d = parse_rdi_detection(iso, frame["det_start"], 0)
        print(f"  r={d['radial_distance']:.2f} m  az={d['azimuth']:.3f} rad  "
              f"el={d['elevation']:.3f} rad  v={d['radial_velocity']:.2f} m/s  "
              f"rcs={d['rcs']:.1f} dBsm")
```

---

## 6. All Three Data Streams

### Register All Callbacks (C++)

`RegRecvCallback` is **overloaded** — the SDK picks the right stream from the
callback's argument type. Enable the streams you want in the config first
(`enable_rdi` / `enable_shi` / `enable_spi`).

```cpp
// RDI — point cloud (up to 4096 detections / frame)
sdk.RegRecvCallback([](const afi::AfiRdiFrame& f) {
    /* process f.message.detections[0 .. f.message.num_detections-1] */
});

// SHII — sensor health
sdk.RegRecvCallback([](const afi::AfiShiMessage& msg) {
    /* msg.sensor_defect_recognised, msg.supply_voltage_status, … */
});

// SPI — sensor performance & pose
sdk.RegRecvCallback([](const afi::AfiSpiMessage& msg) {
    /* msg.sensor_pose.origin_point_x, msg.sensor_pose.orientation_yaw,
       msg.num_fov_segments, msg.fov_segments[0].blockage_status, … */
});

// Optional: packet-loss notification — (total, lost)
sdk.RegRecvCallback([](uint32_t total, uint32_t lost) {
    std::fprintf(stderr, "Lost %u / %u packets\n", lost, total);
});
```

> **⚠️ Callback threading & data lifetime**
> - Callbacks are invoked from **receiver threads** (one per stream), not the
>   thread that called `Start()`. Synchronize any shared state you touch.
> - The detection array (`frame.message.detections`) points to an **internal
>   buffer that is reused on the next frame**. It is valid **only during the
>   callback**. Copy anything you need to keep beyond the callback scope.
> - Config API methods and `SendVehicleInput()` are synchronous and run on the
>   caller's thread; do not call Config API methods concurrently on the same
>   `AfiSdk` instance.

### Receiving All Streams (Python)

The Python `AfiSdk` class has **no streaming callbacks**. Each stream (RDI 30509,
SHII 30510, SPI 30511) is received on its own socket and parsed with
`afi_sdk.streams` — typically one background thread per stream. See
`python/examples/sample_data_stream.py`, which wraps this in a reusable
`StreamReceiver` class for both TCP and UDP (with SOME/IP-TP reassembly).

---

## 7. Configuration API

All config calls are synchronous (blocking). The Config API runs over its own
TCP connection (port 30500) and is independent of the data streams — keep
`enable_config_api = true` (the default). Every C++ getter **returns an `int`
error code and writes the result into an out-parameter**.

> The examples below show a representative subset. The full Config API is **~30
> methods across 7 categories** (Device/Status, Network, Range Mode, Detection,
> Time Sync, Mounting, Persistence) — see the complete catalog at the end of this
> section and full signatures in [docs/api_reference.md](docs/api_reference.md).

### Read (C++)

```cpp
afi::AfiSdk sdk;
sdk.Init(cfg);

afi::AfiDeviceInfo info;
if (sdk.GetDeviceInfo(info) == afi::kOk)        // serial, sw version, …
    printf("Serial: %s\n", info.serial_number.c_str());

afi::AfiSensorStatus status;
sdk.GetSensorStatus(status);                    // op state, temps, voltage

afi::AfiNetworkConfig net;
sdk.GetNetworkConfig(net);                       // radar IP + stream destinations

afi::AfiRangeMode mode;
sdk.GetRangeMode(mode);                          // DR / LR / MR / SR / ULR / USR

afi::AfiDetectionThreshold thresh;
sdk.GetDetectionThreshold(thresh);

afi::AfiPtpConfig ptp;
sdk.GetPtpConfig(ptp);
```

### Write (C++)

```cpp
sdk.SetRangeMode(afi::AfiRangeMode::kLongRange);

// Point one stream's destination at this host (applies immediately, no restart)
afi::AfiStreamDestination dst;
dst.ip       = "192.168.10.100";
dst.port     = 30509;
dst.protocol = "udp";                            // "tcp" or "udp"
sdk.SetStreamDestination(afi::AfiStreamKind::kDetection, dst);

sdk.SaveConfig();           // persist to NVM
```

### Read / Write (Python)

```python
from afi_sdk import AfiSdk, AfiSdkConfig, RangeMode

info   = sdk.get_device_info()          # returns dict (or None on error)
status = sdk.get_sensor_status()
mode   = sdk.get_range_mode()           # returns int (see RangeMode)

sdk.set_range_mode(RangeMode.LONG_RANGE)   # takes an int constant
sdk.save_config()
```

### Range Mode Values

| Constant | Meaning |
|----------|---------|
| `DR` | Dual Range |
| `LR` | Long Range |
| `MR` | Mid Range |
| `SR` | Short Range |
| `ULR` | Ultra Long Range |
| `USR` | Ultra Short Range |

### Detection Tuning (C++)

```cpp
// SNR threshold + 19-level CFAR tables
afi::AfiDetectionThreshold thresh;
sdk.GetDetectionThreshold(thresh);
thresh.snr_dB = 14.0f;                          // CFAR levels are 1..9
sdk.SetDetectionThreshold(thresh);

// Nine clutter/multipath filter toggles
afi::AfiDetectionFilters filters;
sdk.GetDetectionFilters(filters);
filters.filter_fov = true;
filters.filter_multibounce_2x = true;
sdk.SetDetectionFilters(filters);

// Range/angle measurement density augmentation
afi::AfiDetectionDensity density{ /*range*/ true, /*angle*/ true };
sdk.SetDetectionDensity(density);
```

```python
# Python
sdk.set_detection_threshold(snr_db=14.0)
sdk.set_detection_filters(filter_fov=True, filter_multibounce_2x=True)  # any subset
sdk.set_detection_density(range_density=True, angle_density=True)
```

### Full Config API Method Catalog

All getters return `int` + out-param (C++) / `Optional[dict]` (Python); setters
return `int`. See [docs/api_reference.md](docs/api_reference.md) for signatures and field tables.

| Category (Method ID) | Methods |
|----------------------|---------|
| Device & Status (0x001x) | `GetDeviceInfo`, `GetSensorStatus`, `GetDiagnosticInfo`, `ClearFaultLog`, `Restart`, `ResetAllSettings`, `GetCommandHistoryInfo` |
| Network (0x002x) | `GetNetworkConfig`, `SetNetworkConfig` (reports `restart_required`), `SetStreamDestination` |
| Range Mode (0x003x) | `GetRangeMode`, `SetRangeMode` |
| Detection (0x004x) | `GetDetectionThreshold`, `SetDetectionThreshold`, `GetDetectionFilters`, `SetDetectionFilters`, `GetDetectionDensity`, `SetDetectionDensity` |
| Time Sync (0x005x) | `GetPtpConfig`, `SetPtpConfig` (profile 0 only) |
| Mounting (0x007x) | `GetMountingPosition`, `SetMountingPosition`, `GetRotation`, `SetRotation`, `GetSensorPosition`, `SetSensorPosition` |
| Persistence (0x009x) | `SaveConfig`, `GetConfigStatus`, `ResetConfigDefaults` |

> `Restart`, `ResetAllSettings`, and `SetNetworkConfig` (radar network fields)
> drop the connection / require a sensor restart. `SaveConfig` persists changes
> to NVM — Set* changes are otherwise lost on reboot.

---

## 8. Vehicle Input (CSII)

Send vehicle dynamics from your host to the radar for ego-motion compensation.
Call cyclically — **100 ms (10 Hz) is recommended**. The SDK connects lazily on
first use and maintains the message/E2E counters automatically.

```cpp
// C++
afi::AfiVehicleInput vi{};
vi.velocity_x_mps = 10.0f;          // m/s forward
vi.velocity_y_mps = 0.0f;
vi.velocity_z_mps = 0.0f;
vi.rotation_rate_yaw_rps = 0.05f;   // rad/s
sdk.SendVehicleInput(vi);
```

```python
# Python
sdk.send_vehicle_input(
    velocity_mps=(10.0, 0.0, 0.0),
    rotation_rate_rps=(0.0, 0.0, 0.05)
)
```

---

## 9. Sensor Discovery

Discovers all AFI920 sensors on the local network (UDP broadcast, default 3 s timeout).

```cpp
// C++ — discover on all interfaces
auto sensors = afi::AfiSdk::Discover();
for (const auto& s : sensors) {
    std::cout << s.serial_number << "  " << s.ip
              << "  sw=" << s.software_version << "\n";
}
```

```python
# Python — returns a list of dicts
sensors = AfiSdk.discover(timeout_s=3.0, bind_ip="")
for s in sensors:
    print(s.get("serial_number"), s.get("ip_address"), s.get("software_version"))
```

Key `AfiSensorInfo` fields: `serial_number`, `ip`, `model`, `hw_version`, `software_version`, `range_mode`, `detection_port`, `health_port`, `performance_port`, `ptp_state`, `ptp_offset_ns`. (Python discovery dicts use the same keys, with the radar IP under `ip_address`.)

---

## 10. Multi-Sensor Setup

Use one independent `AfiSdk` instance per sensor (each has its own sockets,
threads, and callbacks). Over **TCP** the host side uses ephemeral ports, so no
port offset is needed — just a different `sensor_ip` per instance. Over **UDP**
each sensor must stream to a distinct host receive port; the SDK's convention is
`+100 per sensor` (see `sample_multi_sensor.py`).

```cpp
// C++ — one instance per sensor (TCP, default ports)
const char* ips[] = {"192.168.10.150", "192.168.10.151"};

std::vector<std::unique_ptr<afi::AfiSdk>> sdks;
for (const char* ip : ips) {
    afi::AfiSdkConfig cfg;
    cfg.sensor_ip = ip;
    cfg.enable_rdi = true;
    cfg.enable_config_api = false;

    auto sdk = std::make_unique<afi::AfiSdk>();
    sdk->RegRecvCallback([ip](const afi::AfiRdiFrame& f) {
        printf("[%s] detections=%u\n", ip, f.message.num_detections);
    });
    sdk->Init(cfg);
    sdk->Start();
    sdks.push_back(std::move(sdk));
}
```

```python
# Python — assign distinct UDP receive ports per sensor (+100), then
# point each sensor's stream destination there via the Config API.
PORT_OFFSET_PER_SENSOR = 100
for idx, ip in enumerate(["192.168.10.150", "192.168.10.151"]):
    sdk = AfiSdk()
    sdk.init(AfiSdkConfig(sensor_ip=ip))
    offset = idx * PORT_OFFSET_PER_SENSOR
    sdk.set_network_config(
        detection_ip=host_ip, detection_port=30509 + offset,
        health_ip=host_ip,    health_port=30510 + offset,
        performance_ip=host_ip, performance_port=30511 + offset,
    )
    sdk.close()
```

**Supported:** up to 4 sensors simultaneously.

---

## 11. Key Data Structures

### `AfiSdkConfig` (C++) / `AfiSdkConfig` (Python dict/dataclass)

| Field | Default | Description |
|-------|---------|-------------|
| `sensor_ip` | `"192.168.10.150"` | Radar IP |
| `config_port` | `30500` | Config API TCP port |
| `rdi_port` | `30509` | RDI receive port on host |
| `shi_port` | `30510` | SHII receive port |
| `spi_port` | `30511` | SPI receive port |
| `csii_port` | `30501` | Vehicle input TCP port |
| `stream_transport` | `"tcp"` | `"tcp"` or `"udp"` (string) |
| `enable_rdi` | `true` | Receive the RDI data stream |
| `enable_shi` | `false` | Receive the SHII data stream |
| `enable_spi` | `false` | Receive the SPI data stream |
| `enable_config_api` | `true` | Auto-connect Config API (TCP) on `Init()` |
| `validate_e2e` | `true` | Verify CRC on incoming frames |
| `e2e_strict` | `false` | Drop frame on CRC failure (else warn + deliver) |
| `socket_buffer_size` | `16 MiB` | `SO_RCVBUF` for TCP and UDP |
| `connect_timeout_ms` | `5000` | TCP connect timeout |
| `request_timeout_ms` | `5000` | Config API request timeout |

> The Python `AfiSdkConfig` dataclass mirrors these but collapses the two
> timeouts into one `timeout` field (default `5.0` s) and omits the `enable_*`,
> `e2e_strict`, and `socket_buffer_size` fields (Python streaming is done with
> your own sockets). It keeps `validate_e2e`.

### `AfiRdiFrame`

| Field | Type | Description |
|-------|------|-------------|
| `message` | `bts_rdi_message_t` | Deserialized RDI message (see below) |
| `recv_timestamp_ns` | uint64 | Host receive timestamp (ns since epoch) |
| `frame_seq` | uint32 | SDK internal frame sequence number |

The detection count and array live **inside `message`**:
`frame.message.num_detections` (uint16, 0–4096), `frame.message.message_counter`,
`frame.message.timestamp` (ns), and `frame.message.detections[i]`.

### Detection fields (per `bts_rdi_detection_t`, 51 bytes)

Positions are **spherical** (range/azimuth/elevation) — convert to Cartesian
yourself if needed.

| Field | Unit | Description |
|-------|------|-------------|
| `existence_probability` | % | 0–100 |
| `detection_id` | uint16 | `0xFFFF` if unused |
| `object_id_reference` | uint16 | Associated object ID (reserved) |
| `timestamp_difference` | s | |
| `radar_cross_section` | dBsm | Radar cross-section |
| `signal_to_noise_ratio` | dB | |
| `ambiguity_grouping_id` | uint16 | `0xFFFF` if unused |
| `position_radial_distance` | m | |
| `position_azimuth` | rad | |
| `position_elevation` | rad | |
| `position_radial_distance_error` | m | |
| `position_azimuth_error` | rad | |
| `position_elevation_error` | rad | |
| `relative_velocity_radial` | m/s | Radial velocity |
| `relative_velocity_radial_error` | m/s | |

### `AfiSpiMessage` (= `bts_spi_message_t`, sensor performance & pose)

| Field | Description |
|-------|-------------|
| `sensor_pose.origin_point_x/y/z` | Mounting position (m) |
| `sensor_pose.orientation_yaw/pitch/roll` | Mounting orientation (rad) |
| `num_fov_segments`, `fov_segments[]` | FoV segments; each has `blockage_status` (`NONE`/`PARTIAL`/`FULL`/`UNKNOWN`) |
| `num_recognisable_object_types`, `recognisable_object_types[]` | Object classes + detection range |
| `num_reference_target_types`, `reference_target_types[]` | RCS / SNR / range of reference targets |

---

## 12. Error Codes

C++ codes are the `afi::AfiError` enum (`afi_error.h`). The Python constants
share the names below but **not** the numeric values, so always compare by name.

| C++ (`afi::`) | C++ value | Python constant | Python value | Meaning |
|---------------|-----------|-----------------|--------------|---------|
| `kOk` | `0` | `OK` | `0` | Success |
| `kNotInitialized` | `-1` | — | — | `Init()` not called |
| `kAlreadyRunning` | `-2` | — | — | Already started |
| `kConnectFailed` | `-3` | — | — | TCP connect failed |
| `kTimeout` | `-4` | `TIMEOUT` | `-2` | No response within timeout |
| `kDisconnected` | `-5` | `DISCONNECTED` | `-1` | TCP connection lost |
| `kInvalidParam` | `-6` | — | — | Bad argument |
| `kProtocolError` | `-7` | `PROTOCOL_ERROR` | `-3` | Malformed SOME/IP / E2E failure |
| `kSensorError` | `-8` | `SENSOR_ERROR` | `-8` | Sensor-side fault |
| `kInternalError` | `-9` | — | — | Internal SDK error |
| `kNotSupported` | `-10` | `NOT_SUPPORTED` | `-9` | Not supported by this firmware |

Use `afi::AfiErrorStr(code)` to get a human-readable C++ message.

---

## 13. Port & Protocol Reference

| Port | Proto | Dir | Purpose |
|------|-------|-----|---------|
| 30500 | TCP | ↔ | Config API (SOME/IP + JSON) |
| 30501 | TCP | → | CSII — Vehicle Input |
| 30509 | TCP/UDP | ← | RDI — Point Cloud |
| 30510 | TCP/UDP | ← | SHII — Health Info |
| 30511 | TCP/UDP | ← | SPI — Performance Info |
| 30520 | UDP bcast | ↔ | Discovery |

**Protocol stack (all data streams):**
```
User callback
    ↑
ISO 23150 payload (binary, little-endian)
    ↑
AUTOSAR E2E Profile 7 header (20 B · CRC-64/XZ)
    ↑
SOME/IP header (16 B · big-endian)  [+ 4 B TP offset header on segmented UDP]
    ↑
TCP / UDP
```

**Multi-sensor host receive-port convention (+100 per sensor, UDP):**

| Sensor | RDI | SHII | SPI |
|--------|-----|------|-----|
| 1 | 30509 | 30510 | 30511 |
| 2 | 30609 | 30610 | 30611 |
| 3 | 30709 | 30710 | 30711 |
| 4 | 30809 | 30810 | 30811 |

(Over TCP each sensor uses an ephemeral host port, so no offset is needed.)

---

## 14. Troubleshooting Checklist

| Symptom | Check |
|---------|-------|
| `ping` fails | Host IP in `192.168.10.x` subnet? Firewall open? |
| `Init()` times out | Sensor powered on? `config_port` 30500 reachable? |
| `Start()` returns, no callbacks | `sensor_ip` correct? RDI port 30509 open on host? |
| Callbacks fire but `num_detections == 0` | Object in radar FoV? Check range mode with `GetRangeMode()` |
| Packet loss callbacks firing | Switch to TCP (`stream_transport = "tcp"`) |
| E2E errors in logs | Enable `validate_e2e=true`; disable `e2e_strict` to keep frames |
| Config API timeout | Keep `enable_config_api=true`; check port 30500 is reachable |
| Discovery returns nothing | Run on same subnet; check UDP 30520 not firewalled |

---

## Quick Reference Card

```
INIT  → AfiSdk::Init(cfg)
CONFIG → GetX(out) / SetX() — return int, result in out-param
STREAM → RegRecvCallback(cb) — overloaded for RDI / SHII / SPI / loss
RUN   → sdk.Start()   |   STOP → sdk.Stop()
VEHICLE INPUT → sdk.SendVehicleInput(vi)   (any time after Start)
DISCOVER → AfiSdk::Discover()             (static, no Init needed)
```

**Key files to read next:**
- [examples/cpp/sample_pointcloud.cpp](afi920-sdk/examples/cpp/sample_pointcloud.cpp)
- [examples/cpp/sample_data_stream.cpp](afi920-sdk/examples/cpp/sample_data_stream.cpp)
- [python/examples/sample_data_stream.py](afi920-sdk/python/examples/sample_data_stream.py)
- [docs/api_reference.md](docs/api_reference.md) — complete API reference
