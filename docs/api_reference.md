# API Reference

## Protocol Stack

The AFI920 SDK communicates with the sensor using SOME/IP over TCP and TCP/UDP:

```
┌──────────────────────────────────────────────────────────────┐
│                   Application (User Code)                    │
├──────────────────┬─────────────────────────┬─────────────────┤
│   Config API     │   Data Streams (Rx)     │  Vehicle Input  │
│   (TCP:30500)    │   RDI / SHII / SPI      │  CSII (Tx)      │
│                  │  (TCP/UDP:30509-30511)  │  (TCP:30501)    │
├──────────────────┼─────────────────────────┴─────────────────┤
│        SOME/IP Protocol (Service ID: 0x6000)                 │
│        Header: Big-Endian (16 bytes)                         │
├──────────────────┼───────────────────────────────────────────┤
│  JSON Payloads   │  E2E Profile 7 Header (20 bytes, BE)      │
│  (Config API)    │  + ISO 23150 Binary Payloads (LE)         │
└──────────────────┴───────────────────────────────────────────┘
```

- **Config API** uses JSON-encoded payloads inside SOME/IP frames over TCP (port 30500). Method IDs occupy 10-unit category blocks `0x0010`–`0x0092`.
- **Data Streams** (RDI, SHII, SPI) use ISO 23150 binary payloads inside SOME/IP frames over TCP or UDP (configurable via `stream_transport`). Every stream payload carries a 20-byte AUTOSAR E2E Profile 7 header between the SOME/IP header and the ISO 23150 payload — see [E2E Protection](#e2e-protection-autosar-profile-7).
- **CSII (Vehicle Input)** is the only Host → Radar stream: the SDK publishes vehicle dynamics to the sensor over TCP port 30501.
- **SOME/IP headers** and **E2E headers** are big-endian. **Data payloads** (ISO 23150 structs) are little-endian.
- Large RDI frames (up to 4096 detections) are segmented using SOME/IP-TP and reassembled automatically by the SDK.

### Data Stream Event IDs

| Stream | Direction | Event ID | E2E Data ID | Default Port |
|--------|-----------|----------|-------------|--------------|
| CSII (Vehicle Input) | Host → Radar | `0x8001` | `0x60008001` | 30501 (TCP only) |
| RDI (Radar Detections) | Radar → Host | `0x8002` | `0x60008002` | 30509 |
| SHII (Sensor Health) | Radar → Host | `0x8003` | `0x60008003` | 30510 |
| SPI (Sensor Performance) | Radar → Host | `0x8004` | `0x60008004` | 30511 |

---

## E2E Protection (AUTOSAR Profile 7)

Every RDI/SHII/SPI/CSII payload carries a 20-byte E2E Profile 7 header (big-endian) between the SOME/IP header and the ISO 23150 payload:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 8 | CRC | CRC-64/XZ over frame bytes `[8..end)` (header tail + ISO payload) |
| 8 | 4 | Length | Protected ISO 23150 payload length |
| 12 | 4 | Counter | Per-stream sequence counter |
| 16 | 4 | Data ID | Stream MessageID32 (`0x6000xxxx`, see table above) |

On receive, the SDK strips the E2E header and verifies the CRC-64/XZ before invoking your callback:

- `AfiSdkConfig::validate_e2e` (default `true`) — verify the CRC on every received frame.
- `AfiSdkConfig::e2e_strict` (default `false`) — when `true`, frames with a failing CRC are **dropped**; when `false`, they are delivered with a warning log.

On send (CSII), the SDK computes the CRC and maintains the counter automatically.

In Python, E2E parsing/building lives in the `afi_sdk.streams` module — see [Python Stream Helpers](#python-stream-helpers-afi_sdkstreams).

---

## Class: `afi::AfiSdk`

Main facade class for interacting with an AFI920 sensor.
Each instance represents one sensor connection.

### Lifecycle

```cpp
AfiSdk();
~AfiSdk();

int Init(const AfiSdkConfig& config);
int Start();
int Stop();
bool IsRunning() const;
```

- `Init()` — Establishes TCP connection for Config API (if `enable_config_api` is true) and prepares data stream sockets (TCP or UDP, per `stream_transport` setting). Does not start receiver threads.
- `Start()` — Spawns receiver threads for enabled streams. Non-blocking.
- `Stop()` — Stops threads, closes all connections.
- `IsRunning()` — Returns true between `Start()` and `Stop()`.

### Data Callbacks

Register before calling `Start()`. Callbacks are invoked from receiver threads.

```cpp
void RegRecvCallback(std::function<void(const AfiRdiFrame&)> cb);
void RegRecvCallback(std::function<void(const AfiShiMessage&)> cb);
void RegRecvCallback(std::function<void(const AfiSpiMessage&)> cb);
void RegRecvCallback(std::function<void(uint32_t total, uint32_t lost)> cb);
```

### Vehicle Input (CSII, Host → Radar)

```cpp
int SendVehicleInput(const AfiVehicleInput& input);
```

Publishes one CSII message (Event `0x8001`) to the sensor over TCP port 30501 as an E2E-protected 115-byte frame (16 B SOME/IP + 20 B E2E + 79 B ISO payload).

- Call cyclically — **100 ms recommended**.
- The SDK maintains the message/E2E counters and connects lazily on first use.
- Runs synchronously on the caller's thread.

See `AfiVehicleInput` in [Data Types](#data-types).

### Discovery

Static method — no instance needed.

```cpp
static std::vector<AfiSensorInfo> Discover(int timeout_ms = 3000, uint16_t port = 30520,
                                           const std::string& bind_ip = "");
```

- `bind_ip` — Local IP to bind for broadcast. Empty string uses OS default. Useful for multi-NIC hosts.
- Sensors reply with both a unicast and a limited-broadcast response (wrong-subnet recovery); the SDK deduplicates them automatically.
- The response includes the unified `software_version`, per-component hashes (bootloader/driver/fw), the current range mode, and radar/PTP status. See `AfiSensorInfo` in [Data Types](#data-types).

### Config API

Synchronous methods — called on the caller's thread via TCP.
Requires `enable_config_api = true` in config and successful TCP connection.

#### Device Info & Status (0x001x)
```cpp
int GetDeviceInfo(AfiDeviceInfo& out);
int GetSensorStatus(AfiSensorStatus& out);
int GetDiagnosticInfo(AfiDiagnosticInfo& out);
int ClearFaultLog();
int Restart();
int ResetAllSettings();
int GetCommandHistoryInfo(AfiCommandHistory& out);
```

- `GetDeviceInfo` — Serial, model, HW version, unified software version, and per-component git hashes (bootloader/driver/fw).
- `GetDiagnosticInfo` — Flat fault-array schema: combined A53+M7 state, maximum fault level, and a per-fault list (source, level, active).
- `Restart` — Restarts the sensor; the connection is lost.
- `ResetAllSettings` — Factory reset; the sensor restarts.

#### Network (0x002x)
```cpp
int GetNetworkConfig(AfiNetworkConfig& out);
int SetNetworkConfig(const AfiNetworkConfig& config, bool* restart_required = nullptr);
int SetStreamDestination(AfiStreamKind kind, const AfiStreamDestination& dst);
```

The unified network config covers both the radar's own network (IP, subnet, gateway, API port, VLAN) and all three data stream destinations:

- `SetNetworkConfig` — Full update. The sensor reports `restart_required = true` when radar network fields change — apply them with `Restart()`. Stream destination changes apply immediately.
- `SetStreamDestination` — Partial update of one stream destination (RDI/SHII/SPI); applies immediately, no restart.

#### Range Mode (0x003x)
```cpp
int GetRangeMode(AfiRangeMode& out);
int SetRangeMode(AfiRangeMode mode);
```

Range mode registry: `0=DR, 1=LR, 2=MR, 3=SR, 4=ULR, 5=USR`. See `AfiRangeMode` in [Data Types](#data-types).

#### Detection (0x004x)
```cpp
int GetDetectionThreshold(AfiDetectionThreshold& out);
int SetDetectionThreshold(const AfiDetectionThreshold& threshold);
int GetDetectionFilters(AfiDetectionFilters& out);
int SetDetectionFilters(const AfiDetectionFilters& filters);
int GetDetectionDensity(AfiDetectionDensity& out);
int SetDetectionDensity(const AfiDetectionDensity& density);
```

- `Get/SetDetectionThreshold` exposes `snr_threshold_db` plus `cfar_table_first_frame` and `cfar_table_second_frame`, each a 19-element CFAR level table. The legacy scalar `cfar_threshold_db` is no longer part of protocol v3.0.
- `Get/SetDetectionFilters` — Nine detection filter toggles, including the APS sidelobe noise filter. See `AfiDetectionFilters`.
- `Get/SetDetectionDensity` — Range/angle measurement density augmentation toggles. See `AfiDetectionDensity`.

#### Time Sync (0x005x)
```cpp
int GetPtpConfig(AfiPtpConfig& out);
int SetPtpConfig(uint8_t profile);
```

- `GetPtpConfig` — Returns the PTP profile **plus live sync status** (state, offset, mean path delay, master clock ID) in one call.
- `SetPtpConfig` — Only the profile is configurable (currently profile `0` only).

#### Mounting (0x007x)
```cpp
int GetMountingPosition(AfiMountingPosition& out);
int SetMountingPosition(const AfiMountingPosition& pos);
int GetRotation(AfiRotation& out);
int SetRotation(const AfiRotation& rot);
int GetSensorPosition(AfiSensorPosition& out);
int SetSensorPosition(const AfiSensorPosition& pos);
```

#### Config Persistence (0x009x)
```cpp
int SaveConfig();
int GetConfigStatus(AfiConfigStatus& out);
int ResetConfigDefaults(const std::vector<std::string>& keys,
                        AfiResetConfigDefaultsResult* out = nullptr);
```

- `SaveConfig` — Persist current sensor configuration to non-volatile storage. Configuration changes made via Set methods are lost on reboot unless saved.
- `GetConfigStatus` — Check the status of the last config load operation. See `AfiConfigStatus` in [Data Types](#data-types).
- `ResetConfigDefaults` — Reset listed config keys to factory defaults in RAM only. Call `SaveConfig` separately if the reset should persist.

---

## Threading Model

```
RDI  receiver thread  →  rdi_callback()
SHII receiver thread  →  shi_callback()
SPI  receiver thread  →  spi_callback()
Main thread           →  Config API calls (synchronous)
                      →  SendVehicleInput() (synchronous, lazy CSII connect)
```

- Data callbacks are invoked from **receiver threads**, not the main thread.
- Config API calls and `SendVehicleInput()` are **synchronous** — they block on the calling thread.
- Each `AfiSdk` instance is fully independent (thread-safe across instances).

### Thread Safety

- **Across instances:** Fully independent. Multiple `AfiSdk` instances can run in parallel without synchronization.
- **Within one instance:** Config API methods are synchronous and not thread-safe. Do not call Config API methods concurrently on the same instance from multiple threads. `SendVehicleInput()` is internally serialized and may be called from a dedicated cyclic thread. Data callbacks run on their respective receiver threads.

### Callback Data Lifetime

> **Important:** Data pointers in callbacks (e.g., `AfiRdiFrame::message::detections`) are valid **only during callback execution**. The internal buffer is reused for the next frame. Copy any data you need to retain beyond the callback scope.

---

## Struct: `afi::AfiSdkConfig`

```cpp
struct AfiSdkConfig {
    std::string sensor_ip       = "192.168.10.150";  // Sensor IP address
    uint16_t config_port        = 30500;     // TCP Config API port
    uint16_t rdi_port           = 30509;     // RDI (Radar Detection Interface)
    uint16_t shi_port           = 30510;     // SHII (Sensor Health Information Interface)
    uint16_t spi_port           = 30511;     // SPI (Sensor Performance Interface)
    uint16_t csii_port          = 30501;     // CSII vehicle input (Host → Radar, TCP only)
    std::string stream_transport = "tcp";    // "tcp" or "udp" (default is TCP)
    bool enable_rdi             = true;      // Enable RDI data stream reception
    bool enable_shi             = false;     // Enable SHII data stream reception
    bool enable_spi             = false;     // Enable SPI data stream reception
    bool enable_config_api      = true;      // Auto-connect Config API (TCP) on Init()
    bool validate_e2e           = true;      // Verify E2E Profile 7 CRC-64 on received streams
    bool e2e_strict             = false;     // Drop frames with failing E2E CRC (otherwise warn and deliver)
    uint32_t socket_buffer_size = 16 * 1024 * 1024;  // SO_RCVBUF for both TCP and UDP
    int connect_timeout_ms      = 5000;      // TCP connect timeout (ms)
    int request_timeout_ms      = 5000;      // Config API request timeout (ms)
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sensor_ip` | `string` | `"192.168.10.150"` | Sensor IP address |
| `config_port` | `uint16_t` | `30500` | TCP port for SOME/IP Config API |
| `rdi_port` | `uint16_t` | `30509` | Port for RDI (Radar Detection Interface) |
| `shi_port` | `uint16_t` | `30510` | Port for SHII (Sensor Health Information Interface) |
| `spi_port` | `uint16_t` | `30511` | Port for SPI (Sensor Performance Interface) |
| `csii_port` | `uint16_t` | `30501` | Port for CSII vehicle input (Host → Radar, TCP only) |
| `stream_transport` | `string` | `"tcp"` | Data stream transport: `"tcp"` or `"udp"` |
| `enable_rdi` | `bool` | `true` | Enable RDI data stream reception |
| `enable_shi` | `bool` | `false` | Enable SHII data stream reception |
| `enable_spi` | `bool` | `false` | Enable SPI data stream reception |
| `enable_config_api` | `bool` | `true` | Auto-connect Config API on `Init()` |
| `validate_e2e` | `bool` | `true` | Verify the E2E Profile 7 CRC-64 on received data streams |
| `e2e_strict` | `bool` | `false` | Drop frames whose E2E CRC does not match (otherwise warn and deliver) |
| `socket_buffer_size` | `uint32_t` | `16 MB` | Socket receive buffer size (SO_RCVBUF) |
| `connect_timeout_ms` | `int` | `5000` | TCP connect timeout in milliseconds |
| `request_timeout_ms` | `int` | `5000` | Config API request timeout in milliseconds |

---

## Data Types

### `AfiRdiFrame`
```cpp
struct AfiRdiFrame {
    bts_rdi_message_t message;           // ISO 23150 RDI message
    uint64_t          recv_timestamp_ns; // Host receive timestamp
    uint32_t          frame_seq;         // SDK frame sequence
    std::vector<bts_rdi_detection_t> detections_storage;
};
```

### `bts_rdi_detection_t` (51 bytes)

| Field | Type | Description |
|-------|------|-------------|
| `existence_probability` | `uint8_t` | Existence probability (0-100%) |
| `detection_id` | `uint16_t` | Detection ID (`0xFFFF` if unused) |
| `object_id_reference` | `uint16_t` | Associated object ID (reserved) |
| `timestamp_difference` | `float` | Time offset to message timestamp (s) |
| `radar_cross_section` | `float` | RCS in dBsm |
| `signal_to_noise_ratio` | `float` | SNR in dB |
| `ambiguity_grouping_id` | `uint16_t` | Ambiguity group (`0xFFFF` if unused) |
| `position_radial_distance` | `float` | Range in meters |
| `position_azimuth` | `float` | Azimuth angle in radians |
| `position_elevation` | `float` | Elevation angle in radians |
| `position_radial_distance_error` | `float` | Range error (m) |
| `position_azimuth_error` | `float` | Azimuth error (rad) |
| `position_elevation_error` | `float` | Elevation error (rad) |
| `relative_velocity_radial` | `float` | Radial velocity in m/s |
| `relative_velocity_radial_error` | `float` | Radial velocity error (m/s) |

Up to **4096 detections per frame** (36-byte detection header + 51 bytes per detection). The ambiguity domain covers radial velocity only (`radial_velocity_begin` / `radial_velocity_end` in the header). Large RDI frames are segmented using SOME/IP-TP and reassembled automatically by the SDK.

### `AfiShiMessage` / `AfiSpiMessage`

- `AfiShiMessage` (= `bts_shi_message_t`) — Sensor health: operation modes (non-contiguous enum incl. ULR/USR/Degradation/Evaluation/Calibration/Initialising/Test), defect recognised + reason (12 reasons), supply voltage/temperature status, CSII-based input signal types/statuses, time sync status, and the sensor calibration section. The wire payload is variable-length.
- `AfiSpiMessage` (= `bts_spi_message_t`) — Sensor performance: vehicle coordinate system, 32-byte sensor pose (origin x/y/z, yaw/pitch/roll, yaw/pitch errors), and per-FoV-segment blockage status (incl. `UNKNOWN`).

### `AfiRangeMode`
```cpp
enum class AfiRangeMode : uint8_t {
    kDualRange       = 0,  // DR
    kLongRange       = 1,  // LR
    kMidRange        = 2,  // MR
    kShortRange      = 3,  // SR
    kUltraLongRange  = 4,  // ULR
    kUltraShortRange = 5,  // USR
};
```

### `AfiVehicleInput` (CSII)

Host-provided vehicle dynamics sent via `SendVehicleInput()`. The SDK fills the interface header (timestamp, counter) automatically; set `timestamp_ns` to override the measurement timestamp (`0` = host clock).

| Field | Type | Description |
|-------|------|-------------|
| `sensor_id` | `uint8_t` | Target sensor ID |
| `timestamp_ns` | `uint64_t` | Measurement time (0 = host clock now) |
| `somc` | `bts_csii_somc_t` | Sensor operation mode command (default `BTS_CSII_SOMC_NORMAL_MODE`) |
| `global_timestamp_utc` | `double` | UTC seconds (environmental time) |
| `coord_system` | `bts_csii_vehicle_coord_system_t` | Vehicle coordinate system (default rear axle) |
| `velocity_x_mps`, `velocity_y_mps`, `velocity_z_mps` | `float` | Vehicle velocity (m/s) |
| `rotation_rate_yaw_rps`, `rotation_rate_pitch_rps`, `rotation_rate_roll_rps` | `float` | Rotation rates (rad/s) |
| `wheel_flp_rps`, `wheel_frp_rps`, `wheel_rlp_rps`, `wheel_rrp_rps` | `float` | Wheel speeds FL/FR/RL/RR (rad/s) |
| `steering_angle_rad` | `float` | Steering angle (rad) |
| `gear_position` | `bts_csii_gear_position_t` | Gear position (default neutral) |

### `AfiSensorInfo` (Discovery)

| Field | Type | Description |
|-------|------|-------------|
| `serial_number` | `string` | Sensor serial number |
| `mac_address` | `string` | MAC address |
| `model` | `string` | Sensor model |
| `hw_version` | `string` | Hardware version |
| `software_version` | `string` | Unified SW version (bootloader/driver/fw) |
| `bootloader_hash` | `string` | Bootloader git short hash (8 hex) |
| `driver_hash` | `string` | Driver git short hash |
| `fw_hash` | `string` | Firmware git short hash |
| `ip` | `string` | Sensor IP address |
| `subnet_mask` | `string` | Subnet mask |
| `gateway` | `string` | Gateway |
| `port` | `uint16_t` | Config API TCP port (default: 30500) |
| `detection_port` | `uint16_t` | RDI port (default: 30509) |
| `health_port` | `uint16_t` | SHII port (default: 30510) |
| `performance_port` | `uint16_t` | SPI port (default: 30511) |
| `detection_protocol` | `string` | RDI protocol (`"udp"` or `"tcp"`) |
| `health_protocol` | `string` | SHII protocol |
| `performance_protocol` | `string` | SPI protocol |
| `host_ip` | `string` | Host destination IP |
| `detection_ip` | `string` | RDI destination IP |
| `health_ip` | `string` | SHII destination IP |
| `performance_ip` | `string` | SPI destination IP |
| `position_code` | `uint8_t` | Mounting position code |
| `offset_x`, `offset_y`, `offset_z` | `float` | Mounting offset (meters) |
| `yaw_deg`, `pitch_deg`, `roll_deg` | `float` | Rotation angles (degrees) |
| `range_mode` | `AfiRangeMode` | Current range mode |
| `range_mode_name` | `string` | Range mode name (`"DR"`, `"LR"`, ...) |
| `radar_active` | `bool` | Radar operation active |
| `radar_mode` | `string` | `"normal"` or `"dummy"` (build config) |
| `scan_index` | `uint32_t` | Current MessageCounter |
| `uptime_seconds` | `uint32_t` | Sensor uptime (seconds) |
| `ptp_state` | `string` | PTP state (`"INIT"` / `"SLAVE"` / `"MASTER"`) |
| `ptp_offset_ns` | `int64_t` | PTP clock offset (nanoseconds) |

### Config API Response Structs

#### `AfiDeviceInfo`

| Field | Type | Description |
|-------|------|-------------|
| `serial_number` | `string` | Sensor serial number |
| `mac_address` | `string` | MAC address |
| `model_name` | `string` | Sensor model name |
| `hw_version` | `string` | Hardware version |
| `software_version` | `string` | Unified SW version |
| `bootloader_hash` | `string` | Bootloader git short hash (8 hex) |
| `driver_hash` | `string` | Driver git short hash |
| `fw_hash` | `string` | Firmware git short hash |

#### `AfiSensorStatus`

| Field | Type | Description |
|-------|------|-------------|
| `operation_state` | `string` | Operation state name (e.g., "Normal") |
| `operation_state_code` | `uint8_t` | Numeric state code |
| `temperature_mcu` | `float` | MCU temperature (deg C) |
| `temperature_rf` | `float` | RF board temperature (deg C) |
| `voltage` | `float` | Supply voltage (V) |
| `uptime_sec` | `uint32_t` | Sensor uptime (seconds) |

#### `AfiDiagnosticInfo`

| Field | Type | Description |
|-------|------|-------------|
| `combined_state` | `string` | A53+M7 worst-case state |
| `combined_state_code` | `uint8_t` | Numeric combined state code |
| `max_fault_level` | `string` | Maximum fault level name |
| `max_fault_level_code` | `uint8_t` | 0=NONE, 1=RECORD, 2=RESET |
| `trigger_fault_id` | `string` | Triggering fault ID (hex string) |
| `active_fault_count` | `uint16_t` | Number of active faults |
| `m7_snapshot_stale` | `bool` | M7 fault snapshot is stale |
| `m7_stale_cycles` | `uint32_t` | Stale cycle count |
| `faults` | `vector<AfiFaultEntry>` | Fault entries (id, name, source, level, active) |

#### `AfiFaultEntry`

| Field | Type | Description |
|-------|------|-------------|
| `id` | `string` | Fault ID (hex string, e.g. `"0x012"`) |
| `name` | `string` | Fault name |
| `source` | `string` | `"A53"` or `"M7"` |
| `level` | `string` | Fault level name |
| `level_code` | `uint8_t` | Numeric fault level |
| `active` | `bool` | Fault currently active |

#### `AfiCommandHistory`

| Field | Type | Description |
|-------|------|-------------|
| `total_commands` | `uint32_t` | Total Config API commands processed |
| `success_count` | `uint32_t` | Successful commands |
| `fail_count` | `uint32_t` | Failed commands |

#### `AfiNetworkConfig`

Unified network configuration: the radar's own network plus all stream destinations. Radar network fields apply after a sensor restart; stream destinations apply immediately.

| Field | Type | Description |
|-------|------|-------------|
| `ip_address` | `string` | Radar IP address (applies after restart) |
| `subnet_mask` | `string` | Subnet mask (applies after restart) |
| `gateway` | `string` | Gateway (applies after restart) |
| `api_port` | `uint16_t` | Config API port (default: 30500, applies after restart) |
| `vlan_id` | `uint16_t` | VLAN ID (0 = disabled, applies after restart) |
| `detection` | `AfiStreamDestination` | RDI destination (default port 30509, applies immediately) |
| `health` | `AfiStreamDestination` | SHII destination (default port 30510, applies immediately) |
| `performance` | `AfiStreamDestination` | SPI destination (default port 30511, applies immediately) |

#### `AfiStreamDestination` / `AfiStreamKind`

| Field | Type | Description |
|-------|------|-------------|
| `ip` | `string` | Destination IP |
| `port` | `uint16_t` | Destination port |
| `protocol` | `string` | `"tcp"` or `"udp"` |

```cpp
enum class AfiStreamKind : uint8_t {
    kDetection = 0,    // RDI
    kHealth = 1,       // SHII
    kPerformance = 2,  // SPI
};
```

#### `AfiDetectionThreshold`

| Field | Type | Description |
|-------|------|-------------|
| `snr_dB` | `float` | SNR threshold (dB) |
| `cfar_table_first_frame` | `vector<uint8_t>` | First-frame CFAR levels, exactly 19 values in the range 1..9 |
| `cfar_table_second_frame` | `vector<uint8_t>` | Second-frame CFAR levels, exactly 19 values in the range 1..9 |

#### `AfiDetectionFilters`

Nine detection filter toggles. `SetDetectionFilters` accepts any subset on the wire; the SDK sends all fields.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `filter_fov` | `bool` | `true` | Field-of-view filter |
| `filter_aps_noise` | `bool` | `false` | APS sidelobe noise filter |
| `filter_ego_specular` | `bool` | `false` | Ego specular reflection filter |
| `filter_ground_sidelobe` | `bool` | `false` | Ground sidelobe filter |
| `filter_upper_structure` | `bool` | `false` | Upper structure filter |
| `filter_multibounce_2x` | `bool` | `true` | 2x multibounce filter |
| `filter_structure_multipath` | `bool` | `false` | Structure multipath filter |
| `filter_specular_mirror` | `bool` | `true` | Specular mirror filter |
| `filter_wheel_microdoppler` | `bool` | `true` | Wheel micro-Doppler filter |

#### `AfiDetectionDensity`

Measurement density augmentation toggles (AddMsmt).

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `range_density` | `bool` | `true` | Range measurement density augmentation |
| `angle_density` | `bool` | `true` | Angle measurement density augmentation |

#### `AfiPtpConfig`

PTP configuration plus live sync status. `SetPtpConfig` only accepts `profile` (currently `0` only); the remaining fields are read-only status sourced from ptp4l.

| Field | Type | Description |
|-------|------|-------------|
| `profile` | `uint8_t` | PTP profile index (0 only) |
| `profile_name` | `string` | Profile name (`"default"`) |
| `domain` | `uint8_t` | PTP domain |
| `priority1` | `uint8_t` | Priority 1 (default: 128) |
| `priority2` | `uint8_t` | Priority 2 (default: 128) |
| `state` | `string` | Sync state (`"INIT"` / `"SLAVE"` / `"MASTER"`) |
| `state_code` | `uint8_t` | Numeric state code |
| `offset_ns` | `int64_t` | Clock offset (nanoseconds) |
| `mean_path_delay_ns` | `int64_t` | Mean path delay (nanoseconds) |
| `master_clock_id` | `string` | Master clock identifier |

#### `AfiMountingPosition`

| Field | Type | Description |
|-------|------|-------------|
| `x` | `float` | X offset (meters) |
| `y` | `float` | Y offset (meters) |
| `z` | `float` | Z offset (meters) |

#### `AfiRotation`

| Field | Type | Description |
|-------|------|-------------|
| `roll` | `float` | Roll angle (degrees) |
| `pitch` | `float` | Pitch angle (degrees) |
| `yaw` | `float` | Yaw angle (degrees) |

#### `AfiSensorPosition`

| Field | Type | Description |
|-------|------|-------------|
| `position_code` | `uint8_t` | Mounting position code |

### Config Persistence Structs

#### `AfiConfigStatus`

| Field | Type | Description |
|-------|------|-------------|
| `load_status` | `uint8_t` | 0=LOAD_OK, 1=BACKUP, 2=DEFAULTS |
| `load_status_name` | `string` | Human-readable status name |

#### `AfiResetConfigDefaultsResult`

| Field | Type | Description |
|-------|------|-------------|
| `reset_fields` | `uint32_t` | Number of fields reset to defaults |

---

## Error Codes

```cpp
enum AfiError {
    kOk             =  0,
    kNotInitialized = -1,
    kAlreadyRunning = -2,
    kConnectFailed  = -3,
    kTimeout        = -4,
    kDisconnected   = -5,
    kInvalidParam   = -6,
    kProtocolError  = -7,
    kSensorError    = -8,
    kInternalError  = -9,
    kNotSupported   = -10,
};

const char* AfiErrorStr(int code);
```

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| 0 | `kOk` | Success | -- |
| -1 | `kNotInitialized` | `Init()` not called | Call `Init()` first |
| -2 | `kAlreadyRunning` | `Start()` called twice | Call `Stop()` before `Start()` |
| -3 | `kConnectFailed` | TCP connection failed | Check sensor IP and network |
| -4 | `kTimeout` | Request timed out | Increase `request_timeout_ms` or retry |
| -5 | `kDisconnected` | Connection lost | Reconnect with `Stop()` + `Init()` + `Start()` |
| -6 | `kInvalidParam` | Invalid parameter | Check parameter values |
| -7 | `kProtocolError` | SOME/IP protocol error | Check firmware compatibility |
| -8 | `kSensorError` | Sensor returned error | Check sensor logs |
| -9 | `kInternalError` | Internal SDK error | Report bug |
| -10 | `kNotSupported` | Feature not supported | Check sensor firmware version |

`AfiErrorStr(int code)` returns a human-readable string for any error code. Returns `"Unknown error"` for unrecognized codes.

### Python Error Code Mapping

Python error code values **differ** from C++ error codes:

| Name | Python Value | C++ Value | C++ Name |
|------|-------------|-----------|----------|
| `OK` | `0` | `0` | `kOk` |
| `DISCONNECTED` | `-1` | `-5` | `kDisconnected` |
| `TIMEOUT` | `-2` | `-4` | `kTimeout` |
| `PROTOCOL_ERROR` | `-3` | `-7` | `kProtocolError` |
| `SENSOR_ERROR` | `-8` | `-8` | `kSensorError` |
| `NOT_SUPPORTED` | `-9` | `-10` | `kNotSupported` |

The Python SDK exposes a simplified set of error codes. The C++ SDK has additional codes (`kNotInitialized`, `kAlreadyRunning`, `kConnectFailed`, `kInvalidParam`, `kInternalError`) that are not exposed in the Python SDK.

```python
from afi_sdk import OK, DISCONNECTED, TIMEOUT, PROTOCOL_ERROR, SENSOR_ERROR, NOT_SUPPORTED
```

---

## Python API

The Python SDK provides a high-level interface that mirrors the C++ API. It supports Config API (TCP) methods, CSII vehicle input, and discovery. Data stream parsing (E2E + ISO 23150) lives in the `afi_sdk.streams` module.

### Python `AfiSdkConfig`

```python
from afi_sdk import AfiSdkConfig

@dataclass
class AfiSdkConfig:
    sensor_ip: str = "192.168.10.150"
    config_port: int = 30500
    csii_port: int = 30501            # CSII vehicle input (Host → Radar, TCP only)
    rdi_port: int = 30509
    shi_port: int = 30510
    spi_port: int = 30511
    timeout: float = 5.0              # Request timeout in seconds
    stream_transport: str = "tcp"     # "tcp" or "udp"
    validate_e2e: bool = True         # Verify E2E CRC-64 when parsing streams
```

### Python `AfiSdk` Class

```python
class AfiSdk:
    def init(self, config: AfiSdkConfig) -> int       # Connect to sensor
    def close(self)                                     # Disconnect
    def connected -> bool                               # Connection status (property)

    @staticmethod
    def discover(timeout_s: float = 3.0,
                 bind_ip: str = "") -> List[dict]       # Find sensors on network
```

- `discover()` returns sensor info dicts (`serial_number`, `software_version`, `range_mode`, ports, ...). Unicast/broadcast duplicate responses are deduplicated.

#### Context Manager Support

The `AfiSdk` class supports the Python context manager protocol for automatic cleanup:

```python
with AfiSdk() as sdk:
    sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))
    info = sdk.get_device_info()
# Automatically calls close() on exit
```

### Python Method Overview

All GET methods return `Optional[dict]` (`None` on error). All SET methods return `int` (`0` = OK, negative = error), unless noted otherwise.

#### Vehicle Input (CSII)

| Method | Returns |
|--------|---------|
| `send_vehicle_input(somc=..., velocity_mps=(x, y, z), rotation_rate_rps=(yaw, pitch, roll), wheel_speeds_rps=(fl, fr, rl, rr), steering_angle_rad=0.0, gear_position=..., global_timestamp_utc=0.0, coord_system=0, sensor_id=0, timestamp_ns=0)` | `int` |

Sends one CSII message (TCP 30501, Event `0x8001`). Call cyclically (100 ms recommended); connects lazily on first use, and the SDK maintains the message/E2E counters. SOMC and gear constants live in `afi_sdk.streams` (`SOMC_NORMAL_MODE`, `GEAR_NEUTRAL`, ...).

#### Device Info & Status (0x001x)

| Method | Returns |
|--------|---------|
| `get_device_info()` | `{serial_number, mac_address, model_name, hw_version, software_version, bootloader_hash, driver_hash, fw_hash}` |
| `get_sensor_status()` | `{operation_state, operation_state_code, temperature_mcu, temperature_rf, voltage, uptime_sec}` |
| `get_diagnostic_info()` | `{combined_state, combined_state_code, max_fault_level, active_fault_count, faults[], ...}` |
| `clear_fault_log()` | `int` |
| `restart()` | `int` (connection lost) |
| `reset_all_settings()` | `int` (factory reset) |
| `get_command_history_info()` | `{total_commands, success_count, fail_count}` |

#### Network (0x002x)

| Method | Returns |
|--------|---------|
| `get_network_config()` | `{ip_address, subnet_mask, gateway, api_port, vlan_id, detection_ip, detection_port, detection_protocol, health_..., performance_...}` |
| `set_network_config(**fields)` | `Optional[dict]` (contains `restart_required`) |
| `set_stream_destination(stream, ip, port, protocol="tcp")` | `int` |

- `set_network_config` performs a partial update — only the given keyword fields change. Radar network fields (`ip_address`, `subnet_mask`, `gateway`, `api_port`, `vlan_id`) require a sensor restart; stream destination fields apply immediately.
- `set_stream_destination` updates one stream destination; `stream` is `"detection"`, `"health"`, or `"performance"`.

#### Range Mode (0x003x)

| Method | Returns |
|--------|---------|
| `get_range_mode()` | `Optional[int]` (0=DR, 1=LR, 2=MR, 3=SR, 4=ULR, 5=USR) |
| `set_range_mode(mode)` | `int` |

#### Detection (0x004x)

| Method | Returns |
|--------|---------|
| `get_detection_threshold()` | `{snr_threshold_db, cfar_table_first_frame, cfar_table_second_frame}` |
| `set_detection_threshold(snr_db=None, cfar_table_first_frame=None, cfar_table_second_frame=None)` | `int` |
| `get_detection_filters()` | `{filter_fov, filter_aps_noise, filter_ego_specular, filter_ground_sidelobe, filter_upper_structure, filter_multibounce_2x, filter_structure_multipath, filter_specular_mirror, filter_wheel_microdoppler}` |
| `set_detection_filters(**filters)` | `int` (any subset of the nine toggles) |
| `get_detection_density()` | `{range_density, angle_density}` |
| `set_detection_density(range_density=None, angle_density=None)` | `int` (any subset) |

#### Time Sync (0x005x)

| Method | Returns |
|--------|---------|
| `get_ptp_config()` | `{profile, profile_name, domain, priority1, priority2, state, state_code, offset_ns, mean_path_delay_ns, master_clock_id}` |
| `set_ptp_config(profile=0)` | `int` (only profile 0 is supported) |

#### Mounting (0x007x)

| Method | Returns |
|--------|---------|
| `get_mounting_position()` | `{offset_x_m, offset_y_m, offset_z_m}` |
| `set_mounting_position(x, y, z)` | `int` |
| `get_rotation()` | `{yaw_deg, pitch_deg, roll_deg}` |
| `set_rotation(yaw, pitch, roll)` | `int` |
| `get_sensor_position()` | `Optional[int]` (position code) |
| `set_sensor_position(position_code)` | `int` |

#### Config Persistence (0x009x)

| Method | Returns |
|--------|---------|
| `save_config()` | `int` |
| `get_config_status()` | `{load_status, load_status_name}` |
| `reset_config_defaults(keys)` | `{reset_fields}` |

### Python `RangeMode`

```python
from afi_sdk import RangeMode

RangeMode.DUAL_RANGE         # 0 (DR)
RangeMode.LONG_RANGE         # 1 (LR)
RangeMode.MID_RANGE          # 2 (MR)
RangeMode.SHORT_RANGE        # 3 (SR)
RangeMode.ULTRA_LONG_RANGE   # 4 (ULR)
RangeMode.ULTRA_SHORT_RANGE  # 5 (USR)

RangeMode.name(1)            # "LR"
```

### Python Stream Helpers (`afi_sdk.streams`)

The `afi_sdk.streams` module parses and builds the protocol v3.0 wire format (`SOME/IP 16B + E2E 20B + ISO 23150 payload`):

```python
from afi_sdk.streams import strip_e2e, parse_rdi_frame, parse_rdi_detection

e2e, iso = strip_e2e(someip_payload)     # verify CRC-64/XZ, strip 20B header
if e2e and e2e["crc_ok"]:
    header = parse_rdi_frame(iso)        # 36B RDI header
    for i in range(header["num_detections"]):
        det = parse_rdi_detection(iso, header["det_start"], i)  # 51B each
```

| Function | Description |
|----------|-------------|
| `crc64_xz(data)` | AUTOSAR CRC-64/XZ (KAT-verified) |
| `parse_e2e(frame)` | Parse the 20B E2E header → `{crc, length, counter, data_id}` |
| `strip_e2e(frame, validate=True)` | Strip + CRC-verify the E2E header → `(e2e_dict_with_crc_ok, iso_payload)` |
| `build_e2e_header(data_id, counter, payload)` | Build a 20B E2E header with computed CRC |
| `parse_rdi_frame(payload)` | Parse the 36B RDI header (returns `num_detections`, `det_start`, ...) |
| `parse_rdi_detection(payload, det_start, index)` | Parse one 51B RDI detection |
| `parse_shi(payload)` | Parse a variable-length SHII payload |
| `parse_spi(payload)` | Parse an SPI payload (32B pose + FoV/blockage segments) |
| `build_csii_payload(...)` | Serialize a 79B CSII ISO payload |
| `build_csii_frame(payload, counter, session_id)` | Wrap a CSII payload with E2E + SOME/IP headers (115B frame) |

The module also exports CSII constants: `SOMC_*` (sensor operation mode commands), `GEAR_*` (gear positions), and `E2E_DATA_ID_*`.

### Low-Level SOME/IP Client

For advanced use cases, the `ConfigClient` class provides direct SOME/IP access:

```python
from afi_sdk.someip import ConfigClient, METHOD_GET_DEVICE_INFO

with ConfigClient("192.168.10.150") as client:
    rc, data = client.request(METHOD_GET_DEVICE_INFO)
    if rc == 0:
        print(data)
```

This is useful when you need to call SOME/IP methods not yet wrapped by `AfiSdk`, or when integrating with custom tooling. All protocol v3.0 method ID constants (`METHOD_*`, `0x0010`–`0x0092`) are exported by `afi_sdk.someip`.

---

## Multi-Sensor Usage

Create one `AfiSdk` instance per sensor. Each is fully independent.

```cpp
afi::AfiSdk sdk1, sdk2;

afi::AfiSdkConfig c1, c2;
c1.sensor_ip = "192.168.10.150";
c2.sensor_ip = "192.168.10.151";

sdk1.Init(c1);
sdk2.Init(c2);

sdk1.RegRecvCallback([](const afi::AfiRdiFrame& f) { /* sensor 1 */ });
sdk2.RegRecvCallback([](const afi::AfiRdiFrame& f) { /* sensor 2 */ });

sdk1.Start();
sdk2.Start();

// ...

sdk1.Stop();
sdk2.Stop();
```
