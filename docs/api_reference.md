# API Reference

This reference describes SDK v2.2.0, which implements AFI920 interface v1.1.0.

## Protocol Stack

The SDK talks to the sensor with SOME/IP over TCP and UDP:

```
┌──────────────────────────────────────────────────────────────┐
│                   Application (User Code)                    │
├──────────────────┬─────────────────────────┬─────────────────┤
│   Config API     │   Data Streams (Rx)     │  Vehicle Input  │
│   (TCP:30500)    │    RDI / SHII / SPI     │  CSII (Tx)      │
│                  │  (TCP/UDP:30509-30511)  │ (UDP/TCP:30501) │
├──────────────────┴─────────────────────────┴─────────────────┤
│        SOME/IP Protocol (Service ID: 0x6000)                 │
│        Header: Big-Endian (16 bytes)                         │
├──────────────────────────────────────────────────────────────┤
│        E2E Header (20 bytes, BE, CRC-32)                     │
├──────────────────┬───────────────────────────────────────────┤
│  JSON Payloads   │  ISO 23150 Binary Payloads (LE)           │
│  (Config API)    │                                           │
└──────────────────┴───────────────────────────────────────────┘
```

- **Config API** carries JSON bodies inside SOME/IP frames over TCP port 30500. Firmware image chunks are sent as raw binary bodies. Method IDs span `0x0010` to `0x00E1`. Requests and responses both carry the 20-byte E2E header.
- **Data streams** (RDI, SHII, SPI) carry ISO 23150 binary payloads inside SOME/IP frames over TCP or UDP. `stream_transport` sets the default and `rdi_transport` / `shi_transport` / `spi_transport` override it per stream. Every stream payload carries a 20-byte E2E header, see [E2E Protection](#e2e-protection).
- **CSII (vehicle input)** is the only Host to Radar stream. An interface v1.1.0 sensor accepts it on UDP 30501 only, a v1.0.x sensor on TCP 30501 only; see [Vehicle Input](#vehicle-input-csii-host--radar).
- **SOME/IP headers** and **E2E headers** are big-endian. **ISO 23150 payloads** are little-endian.
- UDP RDI frames (up to 8192 detections) use SOME/IP-TP when they exceed one datagram. TCP always carries one complete SOME/IP frame.
- **Discovery** uses UDP port 30520 without an E2E header.

### Data Stream Event IDs

| Stream | Direction | Event ID | E2E Data ID | Default Port |
|--------|-----------|----------|-------------|--------------|
| CSII (Vehicle Input) | Host → Radar | `0x8001` | `0x60008001` | 30501 (UDP on v1.1.0, TCP on v1.0.x) |
| RDI (Radar Detections) | Radar → Host | `0x8002` | `0x60008002` | 30509 |
| SHII (Sensor Health) | Radar → Host | `0x8003` | `0x60008003` | 30510 |
| SPI (Sensor Performance) | Radar → Host | `0x8004` | `0x60008004` | 30511 |

The sensor sends one RDI, SHII and SPI message per 100 ms send slot.

---

## E2E Protection

Every RDI, SHII, SPI and CSII payload carries a 20-byte E2E header (big-endian) between the SOME/IP header and the ISO 23150 payload. Config API requests and responses carry it before the JSON or binary body.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | CRC high | `0x00000000` on interface v1.1.0 |
| 4 | 4 | CRC | IEEE 802.3 CRC-32 (zlib, KAT `0xCBF43926`) over the body only |
| 8 | 4 | Length | Protected body length (excludes the 20-byte header) |
| 12 | 4 | Counter | Data streams: send slot number. Config API: response sequence |
| 16 | 4 | Data ID | `0x6000 << 16` plus the Event ID (streams) or the Method ID (Config API) |

Interface v1.0.x (firmware 3.0.0 to 3.1.0) keeps the field layout and puts a CRC-64/XZ (KAT `0x995DC9BBDF1939FA`) in bytes 0 to 7, computed over Length, Counter, Data ID and the body. A non-zero CRC high word selects that check on receive. The verdict reports the sender in `generation` (`kV1_1` when the high word is 0, `kV1_0` otherwise). Acceptance of the v1.0.x CRC is limited to this SDK release.

### Receive disposition

The SDK checks every received stream frame: the declared `Length`, the expected `Data ID`, and, with `validate_e2e`, the CRC-32. A zero CRC field never passes. Frames shorter than the E2E header are dropped.

- A failed check is logged as a rate-limited warning and the frame is **still delivered**. The verdict travels with it: `AfiWirePayloadFrame::e2e_valid`, `AfiRdiFrame::e2e_valid`, and `AfiStreamMeta::e2e_valid` for SHII and SPI.
- `e2e_valid` is `true` only when `validate_e2e` is on and the Length, Data ID and CRC checks all passed. `e2e_checked` reports whether `validate_e2e` was on.
- With `validate_e2e = false`, Length and Data ID are still checked and the CRC is not.
- With `e2e_strict = true`, a frame that fails any check is dropped before any callback runs.

Config API responses are checked the same way. A failed check is logged and the response is still used. The verdict is `GetLastSensorResult().e2e_ok` in C++ and `last_result["e2e_ok"]` in Python.

On send (CSII and Config API requests), the SDK computes the CRC and maintains the counter.

In Python, E2E parsing and building live in `afi_sdk.streams`, see [Python Stream Helpers](#python-stream-helpers-afi_sdkstreams).

---

## Access Modes and Persistence

### Access modes

The sensor accepts each Config API method only in specific access modes. A method called outside its modes answers `SESSION_REQUIRED`, which the SDK reports as `kSessionRequired` (C++) or `SESSION_REQUIRED` (Python).

| Mode | Value | Opens | Data streams |
|------|-------|-------|--------------|
| Normal | 0 | GET methods, `Restart`, `SetMode` | Sent |
| Diagnostic | 1 | Every config SET, `ResetAllSettings`, `ResetConfigDefaults` | Sent. Up to about 1 s late while the sensor writes NVM |
| Programming | 2 | Firmware update chain | RDI, SHII and SPI stop. CSII is still accepted |

- Entry is one way: Normal to Diagnostic to Programming. Programming is reachable only from Diagnostic. Only a reboot returns to Normal.
- Entering Programming closes the Diagnostic methods. Finish configuration before entering Programming.
- `SetMode` answers `ACCESS_DENIED` for re-entry of the current mode and for a step back from Programming (reboot first), `SESSION_REQUIRED` for Programming requested from Normal, and `INVALID_PARAM` for mode 0.
- `EnterMode(target)` / `enter_mode(target)` reads the current mode and performs the remaining steps.
- The sensor does not authenticate mode entry. Keep the Config API link inside the trusted network.
- Discovery answers in every mode.

| Method | Modes |
|--------|-------|
| `GetDeviceInfo`, `GetSensorStatus`, `GetDiagnosticInfo`, `GetCommandHistoryInfo` | Normal, Diagnostic, Programming |
| `Restart` | Normal, Diagnostic, Programming |
| `ResetAllSettings` | Diagnostic |
| `GetNetworkConfig` | Normal, Diagnostic, Programming |
| `SetNetworkConfig`, `SetStreamDestination` | Diagnostic |
| `GetRangeMode` | Normal, Diagnostic, Programming |
| `SetRangeMode` | Diagnostic |
| `GetDetectionThreshold`, `GetDetectionFilters` | Normal, Diagnostic, Programming |
| `SetDetectionThreshold`, `SetDetectionFilters` | Diagnostic |
| `GetPtpConfig` | Normal, Diagnostic, Programming |
| `SetPtpConfig` | Diagnostic |
| `GetMountingPosition`, `GetRotation`, `GetSensorPosition` | Normal, Diagnostic, Programming |
| `SetMountingPosition`, `SetRotation`, `SetSensorPosition` | Diagnostic |
| `FirmwareUpdateStart`, `FirmwareDataChunk`, `FirmwareUpdateCommit`, `FirmwareUpdateAbort`, `FirmwareSwitchSlot`, `FirmwareUpdateReboot` | Programming |
| `FirmwareUpdateStatus`, `GetBootInfo` | Normal, Diagnostic, Programming |
| `GetConfigStatus` | Normal, Diagnostic, Programming |
| `ResetConfigDefaults` | Diagnostic |
| `GetMode`, `SetMode` | Normal, Diagnostic, Programming |

### Persistence

- Every SET, `ResetAllSettings` and `ResetConfigDefaults` writes NVM before it answers. `SUCCESS` means the value is stored. There is no separate save call.
- Stored values take effect after `Restart()`. Nothing applies at runtime.
- A GET returns the stored value, which can differ from the running value until the restart. `GetPtpConfig` reports both (`configured_profile` and `profile`).
- A SET that fails changes neither RAM nor NVM. An NVM write failure answers `INTERNAL_ERROR` (`kSensorError`).
- A SET takes longer than a GET (about 175 ms for the NVM write).
- `SetNetworkConfig` and `SetRangeMode` always report `restart_required = true`. `SetPtpConfig` reports it when the stored profile differs from the running one. `SetStreamDestination` returns no flag; its change also applies after `Restart()`.

### Restart and reconnect

`Restart()` and `FirmwareUpdateReboot()` answer before the sensor resets, so the Config API connection drops after the reply. Call `ReconnectConfigApi(timeout_ms)` (C++) or `reconnect(timeout_s)` (Python) to wait for the sensor. After a reboot the sensor is in Normal mode.

---

## Class: `afi::AfiSdk`

Main facade class. Each instance represents one sensor. The class is not copyable.

### Lifecycle

```cpp
AfiSdk();
~AfiSdk();

int Init(const AfiSdkConfig& config);
int Start();
int Stop();
bool IsRunning() const;
```

- `Init()` stores the configuration, connects the Config API (if `enable_config_api`), and opens the enabled stream sockets. It does not start receiver threads.
  - A failed Config API connect is not fatal. The endpoint is kept and later Config API calls connect on demand.
  - An invalid transport string returns `kInvalidParam`. A stream socket that cannot be opened returns `kConnectFailed`. In both cases everything opened during the attempt is released and `Init()` can be called again.
  - Calling `Init()` while initialized or running returns `kAlreadyRunning`. `Init()` works again after `Stop()`.
- `Start()` spawns one receiver thread per enabled stream and returns. It returns `kNotInitialized` unless `Init()` succeeded.
- `Stop()` stops the threads and closes every connection, including the Config API.
- `IsRunning()` returns `true` between `Start()` and `Stop()`.

### Data Callbacks

Register callbacks before `Start()`. They run on the receiver threads.

```cpp
void RegRecvCallback(std::function<void(const AfiRdiFrame&)> cb);
void RegRecvCallback(std::function<void(const AfiShiMessage&)> cb);
void RegRecvCallback(std::function<void(const AfiSpiMessage&)> cb);
void RegRecvCallback(std::function<void(const AfiShiMessage&, const AfiStreamMeta&)> cb);
void RegRecvCallback(std::function<void(const AfiSpiMessage&, const AfiStreamMeta&)> cb);
void RegRecvCallback(std::function<void(uint32_t total, uint32_t lost)> cb);
void RegWirePayloadCallback(std::function<void(const AfiWirePayloadFrame&)> cb);
```

- The RDI callback receives `AfiRdiFrame`, which carries the E2E counter and verdict.
- The SHII and SPI messages are the interface's C structs. The overloads that take `AfiStreamMeta` pass the receive time, E2E counter and verdict beside them. When both the plain and the `AfiStreamMeta` overload are registered for a stream, only the `AfiStreamMeta` one is called.
- A typed callback runs only when the typed parser accepts the payload.

#### RDI loss callback

The `(total, lost)` overload reports RDI send-slot loss.

- Loss is counted from gaps in the RDI E2E Counter, which advances once per 100 ms send slot. MessageCounter is not used because it repeats when the sensor resends the previous scan (DataQualifier 6).
- Only frames whose E2E check passed move the baseline, because the Counter is outside the CRC. With `validate_e2e = false` every frame does.
- A repeated Counter is not loss. A decreasing Counter (sensor restart) starts a new baseline.
- The callback is called when a gap is found, with the running totals of counted frames and lost slots.
- A gap includes slots the sensor did not send to this host, such as slots before the first scan after boot or while a TCP connection was backlogged.

#### Parser-independent payload callback

`RegWirePayloadCallback()` runs after SOME/IP envelope validation, UDP SOME/IP-TP reassembly and the E2E check, and before the typed parser. It therefore receives payloads of a future ISO schema that the typed parser rejects. `AfiWirePayloadFrame::payload` holds the complete E2E header plus ISO payload.

TCP receive buffers are sized from each SOME/IP Length field, UDP receivers accept a complete datagram, and RDI TP reassembly uses a generic 64 MiB per-frame ceiling. None of these limits depends on the typed schema.

#### UDP source filter

UDP receivers accept datagrams only from `AfiSdkConfig::sensor_ip`. Datagrams from any other address are dropped, so a second sensor sending to the same port does not mix into the stream or its SOME/IP-TP reassembly. TCP streams connect to `sensor_ip` directly.

### Vehicle Input (CSII, Host → Radar)

```cpp
int SendVehicleInput(const AfiVehicleInput& input);
```

Sends one CSII message (Event `0x8001`) to `sensor_ip:csii_port` (default 30501). The frame is 115 bytes: 16 B SOME/IP, 20 B E2E, 79 B ISO payload.

- Interface v1.1.0 receives CSII on UDP only and v1.0.x on TCP only. The SDK sends one UDP datagram, or the frame on a TCP connection, by `GetInterfaceGeneration()`. UDP is used while the generation is unknown. `csii_transport` (`"udp"` or `"tcp"`) overrides the choice.

- Call cyclically, every **100 ms** recommended.
- The SDK maintains the message and E2E counters and creates the socket on first use.
- Runs synchronously on the caller's thread and is internally serialized.
- Returns `kNotInitialized` before `Init()` and `kConnectFailed` when the socket cannot be created or connected, or the send fails. A failed TCP send closes the connection and the next call opens it again.
- The sensor accepts CSII in every access mode.

See `AfiVehicleInput` in [Data Types](#data-types).

### Discovery

Static method, no instance needed.

```cpp
static std::vector<AfiSensorInfo> Discover(int timeout_ms = 3000, uint16_t port = 30520,
                                           const std::string& bind_ip = "");
```

- Broadcasts one request and collects responses for one window of `timeout_ms`.
- Each sensor answers twice (unicast and limited broadcast, so that a host on the wrong subnet still sees it). The SDK keeps one response per (source IP, serial number).
- `AfiSensorInfo::source_ip` is the address the response came from. `ip` is the address the sensor reports, or `source_ip` when the response omits it.
- `AfiSensorInfo::generation` comes from the response keys: `driver_hash` exists only on interface v1.0.x and the `m72_*` keys only on v1.1.0.
- `bind_ip` selects the local interface on multi-NIC hosts. Empty uses the OS default.
- Discovery is UDP and a response can be lost. When a sensor is missing from one scan, scan again before concluding it is absent.
- On Windows, an ICMP port-unreachable reported during the window does not end the scan.

### Config API

Synchronous methods that run on the caller's thread. They need `enable_config_api = true`; otherwise, and before `Init()` or after `Stop()`, they return `kNotInitialized`. Every method returns `kOk` or a negative `AfiError`. Output parameters are valid only on `kOk`.

#### Connection, errors and last result

```cpp
AfiSensorResult GetLastSensorResult() const;
AfiInterfaceGeneration GetInterfaceGeneration() const;
int ReconnectConfigApi(int timeout_ms = 60000);
```

- Every Config API connection, including the one `Init()` opens, starts with a read-only GetDeviceInfo request. Its answer decides the interface generation. A v1.0.x sensor answers JSON without the E2E header. The SDK then closes the connection, and every later call returns `kNotSupported` without sending until `ReconnectConfigApi()`. A v1.0.x sensor runs some v1.1.0 requests, such as Restart, ResetAllSettings, FirmwareSwitchSlot and FirmwareUpdateReboot, even though it cannot parse them.
- `GetInterfaceGeneration()` returns the generation from that request, else from the last stream frame that passed the E2E check, else `kUnknown`.

- `GetLastSensorResult()` returns the sensor's outcome of the last Config API call: return code and name (for example `SESSION_REQUIRED`), `data.error_message` (firmware update methods) and the response E2E verdict. `return_code` is `-1` when no response arrived.
- `ReconnectConfigApi()` reconnects to the remembered endpoint, retrying every 500 ms until `timeout_ms`. It returns `kOk`, `kNotInitialized`, or `kConnectFailed` after the timeout. Use it after `Restart()` or `FirmwareUpdateReboot()`.

Transport behaviour:

- A response must answer its request: same Message ID, Client ID and Session ID, protocol and interface version `0x01`, RESPONSE message type, and room for the E2E header. Anything else is `kProtocolError`.
- Any timeout, lost connection or mismatched response closes the socket, so a late reply cannot be read as the answer to the next request. The next call reconnects to the remembered endpoint. A connect that fails at `Init()` is retried the same way.
- `request_timeout_ms` bounds each response and `connect_timeout_ms` each connect.
- Request bodies above 16,384 bytes are refused before sending with `kInvalidParam`. The sensor closes the connection without a reply for such a request.
- The sensor serves one Config API client at a time. A second connection is reset as soon as it is accepted.
- The sensor answers `BUSY` (`kBusy`) above 200 requests per second. `FirmwareDataChunk` is not counted.

| Outcome | C++ error |
|---------|-----------|
| Sensor `SUCCESS` | `kOk` |
| No response within `request_timeout_ms` | `kTimeout` |
| Not connected, connect or send failed, connection closed | `kDisconnected` |
| Response does not answer the request, or the body is not JSON | `kProtocolError` |
| Request body above 16,384 B, or an argument the SDK rejects before sending | `kInvalidParam` |
| Sensor on interface v1.0.x (nothing sent) | `kNotSupported` |
| Sensor `INVALID_PARAM` | `kInvalidParam` |
| Sensor `NOT_SUPPORTED` | `kNotSupported` |
| Sensor `BUSY` | `kBusy` |
| Sensor `SESSION_REQUIRED` | `kSessionRequired` |
| Sensor `ACCESS_DENIED` | `kAccessDenied` |
| Sensor `INVALID_STATE` | `kInvalidState` |
| Any other sensor return code | `kSensorError` |

#### Access Mode (0x00Ex)

```cpp
int GetMode(AfiModeStatus& out);
int SetMode(AfiAccessMode mode, AfiModeStatus* out = nullptr);
int EnterMode(AfiAccessMode target);
```

- `GetMode`: current access mode, time in it, and whether data streams are halted.
- `SetMode`: enters the next mode (`kDiagnostic` or `kProgramming`). `kNormal` returns `kInvalidParam` without sending. Entering `kProgramming` halts RDI, SHII and SPI until the next reboot.
- `EnterMode`: reaches `target` from the current mode with `GetMode` and `SetMode` steps. Returns `kOk` when already there and `kAccessDenied` when the sensor is in Programming and `target` is Diagnostic (reboot first). `kNormal` returns `kInvalidParam`.

See [Access Modes and Persistence](#access-modes-and-persistence).

#### Device Info & Status (0x001x)

```cpp
int GetDeviceInfo(AfiDeviceInfo& out);
int GetSensorStatus(AfiSensorStatus& out);
int GetDiagnosticInfo(AfiDiagnosticInfo& out);
int Restart();
int ResetAllSettings();
int GetCommandHistoryInfo(AfiCommandHistory& out);
```

- `GetDeviceInfo`: serial, MAC, model, HW version, unified software version, bootloader and firmware hashes.
- `GetDiagnosticInfo`: combined state, maximum fault level, and the fault list (source, level, active).
- `Restart`: restarts the sensor in any access mode. The connection drops after the reply.
- `ResetAllSettings`: writes the config defaults to NVM and restarts the sensor (diagnostic mode). The sensor keeps its current IP address, subnet mask and gateway.

#### Network (0x002x)

```cpp
int GetNetworkConfig(AfiNetworkConfig& out);
int SetNetworkConfig(const AfiNetworkConfig& config, bool* restart_required = nullptr);
int SetStreamDestination(AfiStreamKind kind, const AfiStreamDestination& dst);
```

The network configuration covers the sensor's own network (IP, subnet, gateway, API port, VLAN) and the RDI, SHII and SPI destinations. Every field is stored in NVM and applies after `Restart()`.

- `SetNetworkConfig`: full update. It sends every radar network field and all three destinations, so start from `GetNetworkConfig()` and change what you need. `restart_required` is always `true`.
- `SetStreamDestination`: updates one destination (`kDetection`, `kHealth` or `kPerformance`). An empty `protocol` is not sent. Applies after `Restart()`.

#### Range Mode (0x003x)

```cpp
int GetRangeMode(AfiRangeMode& out);
int SetRangeMode(AfiRangeMode mode);
int SetRangeMode(AfiRangeMode mode, bool* restart_required);
```

Range mode registry: `0=DR, 1=LR, 2=MR, 3=SR, 4=ULR, 5=USR, 6=UDR`. `SetRangeMode` refuses values above 6 with `kInvalidParam`. The mode is stored in NVM and takes effect after `Restart()`. The two-argument overload reports `restart_required` (always `true`). `AfiRangeModeName()` returns the sensor's name for a mode.

#### Detection (0x004x)

```cpp
int GetDetectionThreshold(AfiDetectionThreshold& out);
int SetDetectionThreshold(const AfiDetectionThreshold& threshold);
int GetDetectionFilters(AfiDetectionFilters& out);
int SetDetectionFilters(const AfiDetectionFilters& filters);
```

- The threshold and CFAR tables belong to the current range mode.
- `snr_threshold_db` is a level in `1..9`. Fractions are stored as sent and the GET answers with two decimals. Interface v1.1.0 firmware stores the value but does not use it in detection.
- `cfar_table_first_frame` holds 19 levels in `1..9`. `cfar_table_second_frame` exists only in DR (0) and UDR (6). In single modes the GET omits it and the SET ignores it.
- `SetDetectionThreshold` validates the level and tables before sending and returns `kInvalidParam` on a violation. `GetDetectionThreshold` returns `kProtocolError` when the response is out of range.
- `Get/SetDetectionFilters`: the eight filter toggles of `AfiDetectionFilters`. The SDK sends all eight.

#### Time Sync (0x005x)

```cpp
int GetPtpConfig(AfiPtpConfig& out);
int SetPtpConfig(uint8_t profile);
int SetPtpConfig(uint8_t profile, bool* restart_required);
```

- `GetPtpConfig`: running `profile`, stored `configured_profile`, `restart_required`, live sync status, and the system-time grid (`grid_offset_ns`, `grid_state`).
- `SetPtpConfig`: stores profile `0=gPTP`, `1=1588v2-L2` or `2=1588v2-UDP`. Values above 2 return `kInvalidParam`. The profile applies after `Restart()`. The two-argument overload reports whether a restart is required.

#### Mounting (0x007x)

```cpp
int GetMountingPosition(AfiMountingPosition& out);
int SetMountingPosition(const AfiMountingPosition& pos);
int GetRotation(AfiRotation& out);
int SetRotation(const AfiRotation& rot);
int GetSensorPosition(AfiSensorPosition& out);
int SetSensorPosition(const AfiSensorPosition& pos);
```

All three SETs are stored in NVM and apply after `Restart()`.

#### Firmware Update (0x008x)

```cpp
int FirmwareUpdateStart(const AfiOtaStartRequest& req, AfiOtaStartResponse& out);
int FirmwareDataChunk(const uint8_t* data, size_t len);
int FirmwareDataChunk(const uint8_t* data, size_t len, AfiOtaChunkResult* out);
int FirmwareUpdateCommit();
int FirmwareUpdateStatus(AfiOtaStatus& out);
int FirmwareUpdateAbort();
int FirmwareSwitchSlot(const std::string& update_type, AfiOtaSwitchSlotResult& out);
int FirmwareUpdateReboot();
int GetBootInfo(AfiBootInfo& out);
```

These are the individual protocol steps. Python callers can use the complete sequence in [`afi_sdk.firmware`](#python-firmware-update-afi_sdkfirmware).

Rules the sensor enforces:

- Every step except `FirmwareUpdateStatus` and `GetBootInfo` requires the programming access mode. Entering it halts RDI, SHII and SPI until the reboot.
- Two domains exist. `update_type = "firmware"` covers components `m7_0`, `m7_1`, `m7_2` and `a53`. `update_type = "bootloader"` covers component `bootloader`. Any other `update_type` is refused.
- The sensor holds one update session. Commit and slot switch continue after they answer, so poll `FirmwareUpdateStatus` until `state` is `"idle"` before the next start.
- A chunk may not exceed `max_chunk_size` (4096). A chunk may be accepted only in part: `AfiOtaChunkResult::received_bytes` is the next offset, and the unaccepted tail must be sent again. The one-argument overload discards this value, so use the overload with `AfiOtaChunkResult` for transfers.
- The sensor aborts a transferring session after 10 s without an update request.
- `AfiOtaStartRequest::crc32` is sent as `"0x%08X"`. The SDK adds the `0x` prefix to 8 bare hex digits, because the sensor reads a string without `0x` as decimal. An empty `crc32` omits the key, the SDK logs a warning, and the sensor skips the image check.

Update sequence:

1. `EnterMode(AfiAccessMode::kProgramming)`.
2. Bootloader (only when given): start with `update_type "bootloader"`, send the chunks, commit, and poll until `"idle"`. The bootloader has no A/B slot and no slot switch; the update is complete at `"idle"`.
3. Firmware: read `GetBootInfo`; the target is the face opposite `fw_update_active`. For each of `m7_0`, `m7_1`, `m7_2` and `a53`: start (check that `target_slot` is that face), send the chunks, commit, and poll until `"idle"`. All four components are required; a subset leaves a face that mixes old and new images.
4. After all four commits reached `"idle"`: call `FirmwareSwitchSlot("firmware", out)` once, poll until `"idle"`, and confirm with `GetBootInfo` that `fw_update_active` is the written face. Never switch after a failed commit.
5. Call `FirmwareUpdateReboot()` only while the status is `"idle"`; a reboot during a commit or switch cuts the NVM write. Then `ReconnectConfigApi()`.
6. Verify: `GetBootInfo` reports the written face active and valid with `boot_info_status` `"ok"`, and `GetDeviceInfo` reports the expected hashes.

On any failure after a start, call `FirmwareUpdateAbort()` and start that component again. A transfer interrupted before the slot switch leaves the sensor booting the previous face.

#### Config Persistence (0x009x)

```cpp
int GetConfigStatus(AfiConfigStatus& out);
int ResetConfigDefaults(const std::vector<std::string>& keys,
                        AfiResetConfigDefaultsResult* out = nullptr);
```

- `GetConfigStatus`: how the config record loaded at boot (normal, backup copy, or defaults).
- `ResetConfigDefaults`: restores the listed config keys to their defaults and writes NVM, like any other SET (diagnostic mode). Applies after `Restart()`. An empty list or an empty key returns `kInvalidParam`.

---

## Threading Model

```
RDI  receiver thread  →  wire payload callback → RDI callback, loss callback
SHII receiver thread  →  wire payload callback → SHII callback
SPI  receiver thread  →  wire payload callback → SPI callback
Caller's thread       →  Config API calls (synchronous)
                      →  SendVehicleInput() (synchronous)
```

- Data callbacks run on the receiver threads, not on the caller's thread.
- Config API calls and `SendVehicleInput()` block the calling thread.

### Thread Safety

- **Across instances:** fully independent. Several `AfiSdk` instances can run in parallel.
- **Within one instance:** Config API calls are serialized internally, but `GetLastSensorResult()` reflects whichever call finished last. Issue Config API calls from one thread. `SendVehicleInput()` is serialized and may run on its own cyclic thread.

### Callback Data Lifetime

The frame or message reference passed to a callback is valid only during the callback. `AfiRdiFrame` owns its detection vector: its copy and move operations carry `message`, `recv_timestamp_ns`, `frame_seq` and `detections_storage`, and rebind `message.detections` to the destination's storage.

---

## Struct: `afi::AfiSdkConfig`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sensor_ip` | `string` | `"192.168.10.150"` | Sensor IP address. UDP receivers accept datagrams only from this address |
| `config_port` | `uint16_t` | `30500` | Config API TCP port |
| `rdi_port` | `uint16_t` | `30509` | RDI port |
| `shi_port` | `uint16_t` | `30510` | SHII port |
| `spi_port` | `uint16_t` | `30511` | SPI port |
| `csii_port` | `uint16_t` | `30501` | CSII destination port |
| `stream_transport` | `string` | `"tcp"` | Default stream transport: `"tcp"` or `"udp"` |
| `rdi_transport`, `shi_transport`, `spi_transport` | `string` | empty | Per-stream override; empty uses `stream_transport` |
| `enable_rdi` | `bool` | `true` | Receive RDI |
| `enable_shi` | `bool` | `false` | Receive SHII |
| `enable_spi` | `bool` | `false` | Receive SPI |
| `enable_config_api` | `bool` | `true` | Connect the Config API on `Init()` |
| `validate_e2e` | `bool` | `true` | Check the E2E CRC of received frames: CRC-32, or CRC-64/XZ from an interface v1.0.x sensor (Length and Data ID are always checked) |
| `e2e_strict` | `bool` | `false` | Drop frames that fail the E2E check; otherwise warn and deliver |
| `socket_buffer_size` | `uint32_t` | `16 MiB` | SO_RCVBUF for TCP and UDP receivers |
| `connect_timeout_ms` | `int` | `5000` | TCP connect timeout (ms) |
| `request_timeout_ms` | `int` | `5000` | Config API response timeout (ms) |
| `csii_transport` | `string` | empty | CSII transport. Empty picks it from the interface generation (UDP on v1.1.0, TCP on v1.0.x, UDP while unknown); `"udp"` or `"tcp"` forces one |

The stream ports and transports describe where the host receives. The sensor sends to the destinations stored by `SetNetworkConfig` / `SetStreamDestination`, which apply after `Restart()`.

---

## Data Types

### `AfiRdiFrame`

| Field | Type | Description |
|-------|------|-------------|
| `message` | `bts_rdi_message_t` | ISO 23150 RDI message; `message.detections` points into `detections_storage` |
| `recv_timestamp_ns` | `uint64_t` | Host receive time (steady clock) |
| `frame_seq` | `uint32_t` | SDK frame sequence number |
| `detections_storage` | `vector<bts_rdi_detection_t>` | Owned detections |
| `e2e_counter` | `uint32_t` | E2E Counter (send slot) |
| `e2e_checked` | `bool` | `validate_e2e` was on |
| `e2e_valid` | `bool` | Length, Data ID and CRC checks passed |
| `generation` | `AfiInterfaceGeneration` | Sender's generation from the E2E header |

Call `RebindStorage()` after resizing or replacing `detections_storage` directly. A constructor taking `(message, recv_timestamp_ns, frame_seq, detections_storage)` binds the pointer on construction.

### `bts_rdi_detection_t` (51 bytes)

| Field | Type | Description |
|-------|------|-------------|
| `existence_probability` | `uint8_t` | Existence probability (0-100 %) |
| `detection_id` | `uint16_t` | Detection ID (`0xFFFF` if unused) |
| `object_id_reference` | `uint16_t` | Associated object ID (reserved) |
| `timestamp_difference` | `float` | Time offset to the message timestamp (s) |
| `radar_cross_section` | `float` | RCS (dBsm) |
| `signal_to_noise_ratio` | `float` | SNR (dB) |
| `ambiguity_grouping_id` | `uint16_t` | Ambiguity group (`0xFFFF` if unused) |
| `position_radial_distance` | `float` | Range (m) |
| `position_azimuth` | `float` | Azimuth (rad) |
| `position_elevation` | `float` | Elevation (rad) |
| `position_radial_distance_error` | `float` | Range error (m) |
| `position_azimuth_error` | `float` | Azimuth error (rad) |
| `position_elevation_error` | `float` | Elevation error (rad) |
| `relative_velocity_radial` | `float` | Radial velocity (m/s) |
| `relative_velocity_radial_error` | `float` | Radial velocity error (m/s) |

An RDI payload is a 36-byte header plus 51 bytes per detection, up to **8192 detections** (interface v1.1.0 firmware sends at most 2048). The ambiguity domain covers radial velocity only (`radial_velocity_begin` / `radial_velocity_end`).

### `AfiShiMessage` / `AfiSpiMessage` / `AfiStreamMeta`

- `AfiShiMessage` (`bts_shi_message_t`): sensor health. Operation modes (DR..UDR `0..6`, Degradation `10`, Evaluation `50`, Calibration `100`, Initialising `200`, Test `201`), defect recognised and reason (12 reasons), supply voltage and temperature status, CSII input signal types and statuses, time sync status, and the sensor calibration section. Variable length.
- `AfiSpiMessage` (`bts_spi_message_t`): sensor performance. Vehicle coordinate system, 32-byte sensor pose (origin x/y/z, yaw/pitch/roll, yaw and pitch errors), FoV segments with blockage status (including `UNKNOWN`), recognisable object types, and reference target types.

| `AfiStreamMeta` field | Type | Description |
|-------|------|-------------|
| `recv_timestamp_ns` | `uint64_t` | Host receive time (steady clock) |
| `e2e_counter` | `uint32_t` | E2E Counter (send slot) |
| `e2e_checked` | `bool` | `validate_e2e` was on |
| `e2e_valid` | `bool` | Length, Data ID and CRC checks passed |
| `generation` | `AfiInterfaceGeneration` | Sender's generation from the E2E header |

### `AfiWirePayloadFrame`

| Field | Type | Description |
|-------|------|-------------|
| `stream` | `AfiStreamKind` | `kDetection`, `kHealth` or `kPerformance` |
| `recv_timestamp_ns` | `uint64_t` | Host receive time (steady clock) |
| `sensor_timestamp_ns` | `uint64_t` | ISO Common Header timestamp |
| `message_counter` | `uint32_t` | ISO Common Header MessageCounter |
| `e2e_counter` | `uint32_t` | E2E Counter (send slot) |
| `e2e_checked` | `bool` | `validate_e2e` was on |
| `e2e_valid` | `bool` | Length, Data ID and CRC checks passed |
| `payload` | `vector<uint8_t>` | E2E header plus ISO payload, exactly as reassembled |
| `generation` | `AfiInterfaceGeneration` | Sender's generation from the E2E header |

### `AfiInterfaceGeneration`

| Value | Meaning |
|-------|---------|
| `kUnknown` (0) | Not determined yet |
| `kV1_0` (1) | Interface v1.0.x, firmware 3.0.0 to 3.1.0: data streams and CSII only |
| `kV1_1` (2) | Interface v1.1.0 |

### `AfiRangeMode`

```cpp
enum class AfiRangeMode : uint8_t {
    kDualRange       = 0,  // DR
    kLongRange       = 1,  // LR
    kMidRange        = 2,  // MR
    kShortRange      = 3,  // SR
    kUltraLongRange  = 4,  // ULR
    kUltraShortRange = 5,  // USR
    kUltraDualRange  = 6,  // UDR (two subcycles, like DR)
};

const char* AfiRangeModeName(AfiRangeMode mode);  // "DR".."UDR"; "UNKNOWN" outside 0..6
```

### `AfiAccessMode` / `AfiModeStatus`

```cpp
enum class AfiAccessMode : uint8_t {
    kNormal      = 0,
    kDiagnostic  = 1,
    kProgramming = 2,
};
```

| `AfiModeStatus` field | Type | Description |
|-------|------|-------------|
| `access_mode` | `AfiAccessMode` | Current mode |
| `access_mode_name` | `string` | `"normal"` / `"diagnostic"` / `"programming"` |
| `elapsed_ms` | `uint32_t` | Time in the current mode (GetMode only) |
| `streams_halted` | `bool` | Data streams stopped (programming mode) |

### `AfiVehicleInput` (CSII)

Vehicle dynamics sent by `SendVehicleInput()`. The SDK fills the interface header (version 1.1.0, cycle time, counter).

| Field | Type | Description |
|-------|------|-------------|
| `sensor_id` | `uint8_t` | Common Header SensorID (default `1`) |
| `timestamp_ns` | `uint64_t` | Measurement time; `0` uses the host clock |
| `somc` | `bts_csii_somc_t` | Sensor operation mode command (default `BTS_CSII_SOMC_NORMAL_MODE`) |
| `global_timestamp_utc` | `double` | UTC seconds |
| `coord_system` | `bts_csii_vehicle_coord_system_t` | Vehicle coordinate system (default rear axle) |
| `velocity_x_mps`, `velocity_y_mps`, `velocity_z_mps` | `float` | Vehicle velocity (m/s) |
| `rotation_rate_yaw_rps`, `rotation_rate_pitch_rps`, `rotation_rate_roll_rps` | `float` | Rotation rates (rad/s) |
| `wheel_flp_rps`, `wheel_frp_rps`, `wheel_rlp_rps`, `wheel_rrp_rps` | `float` | Wheel speeds FL/FR/RL/RR (rad/s) |
| `steering_angle_rad` | `float` | Steering angle (rad) |
| `gear_position` | `bts_csii_gear_position_t` | Gear position (default neutral) |

Interface v1.1.0 firmware stores the received values; SOMC and the global timestamp do not change sensor behaviour.

### `AfiSensorInfo` (Discovery)

| Field | Type | Description |
|-------|------|-------------|
| `serial_number` | `string` | Serial number |
| `mac_address` | `string` | MAC address |
| `model` | `string` | Model name |
| `hw_version` | `string` | Hardware version |
| `software_version` | `string` | Unified software version |
| `bootloader_hash` | `string` | Bootloader git short hash |
| `fw_hash` | `string` | Firmware git short hash |
| `ip` | `string` | Address the sensor reports (`source_ip` when absent) |
| `source_ip` | `string` | Address the response came from |
| `subnet_mask` | `string` | Subnet mask |
| `gateway` | `string` | Gateway |
| `port` | `uint16_t` | Config API TCP port (default 30500) |
| `detection_port` | `uint16_t` | RDI port (default 30509) |
| `health_port` | `uint16_t` | SHII port (default 30510) |
| `performance_port` | `uint16_t` | SPI port (default 30511) |
| `detection_protocol` | `string` | RDI protocol (`"udp"` or `"tcp"`) |
| `health_protocol` | `string` | SHII protocol |
| `performance_protocol` | `string` | SPI protocol |
| `host_ip` | `string` | Host destination IP |
| `detection_ip` | `string` | RDI destination IP |
| `health_ip` | `string` | SHII destination IP |
| `performance_ip` | `string` | SPI destination IP |
| `position_code` | `uint8_t` | Mounting position code |
| `offset_x`, `offset_y`, `offset_z` | `float` | Mounting offset (m) |
| `yaw_deg`, `pitch_deg`, `roll_deg` | `float` | Rotation (deg) |
| `range_mode` | `AfiRangeMode` | Stored range mode |
| `range_mode_name` | `string` | Range mode name (`"DR"`, `"LR"`, ...) |
| `radar_active` | `bool` | Radar operation active |
| `radar_mode` | `string` | `"normal"` or `"dummy"` (build configuration) |
| `scan_index` | `uint32_t` | Current MessageCounter (`scan_status.scan_index`) |
| `uptime_seconds` | `uint32_t` | Sensor uptime in seconds (`scan_status.uptime_seconds`) |
| `ptp_state` | `string` | `"INIT"` / `"SLAVE"` / `"MASTER"` |
| `ptp_offset_ns` | `int64_t` | PTP clock offset (ns) |
| `generation` | `AfiInterfaceGeneration` | `kV1_0` when `driver_hash` is present, `kV1_1` when an `m72_*` key is present |

### Config API Response Structs

#### `AfiSensorResult`

| Field | Type | Description |
|-------|------|-------------|
| `return_code` | `int` | Sensor return code (`AfiReturnCode`); `-1` when no response arrived |
| `return_name` | `string` | Sensor's name for the code, e.g. `"SESSION_REQUIRED"` |
| `error_message` | `string` | `data.error_message` (firmware update methods) |
| `e2e_ok` | `bool` | Response E2E verdict |

#### `AfiDeviceInfo`

| Field | Type | Description |
|-------|------|-------------|
| `serial_number` | `string` | Serial number |
| `mac_address` | `string` | MAC address |
| `model_name` | `string` | Model name |
| `hw_version` | `string` | Hardware version |
| `software_version` | `string` | Unified software version |
| `bootloader_hash` | `string` | Bootloader git short hash |
| `fw_hash` | `string` | Firmware git short hash (running image) |

`software_version` and `fw_hash` describe the running image, so a firmware update shows its new values after the reboot. `bootloader_hash` changes when the bootloader update completes.

#### `AfiSensorStatus`

| Field | Type | Description |
|-------|------|-------------|
| `operation_state` | `string` | Operation state name |
| `operation_state_code` | `uint8_t` | Numeric state code |
| `temperature_mcu` | `float` | MCU temperature (°C) |
| `temperature_rf` | `float` | RF temperature (°C) |
| `voltage` | `float` | Supply voltage (V) |
| `uptime_sec` | `uint32_t` | Uptime (s) |

#### `AfiDiagnosticInfo` / `AfiFaultEntry`

| Field | Type | Description |
|-------|------|-------------|
| `combined_state` | `string` | Combined worst-case state |
| `combined_state_code` | `uint8_t` | Numeric combined state |
| `max_fault_level` | `string` | Maximum fault level name |
| `max_fault_level_code` | `uint8_t` | 0=NONE, 1=RECORD, 2=RESET |
| `trigger_fault_id` | `string` | Triggering fault ID (hex string) |
| `active_fault_count` | `uint16_t` | Number of active faults |
| `m7_snapshot_stale` | `bool` | Fault snapshot is stale |
| `m7_stale_cycles` | `uint32_t` | Stale cycle count |
| `faults` | `vector<AfiFaultEntry>` | Fault entries |

| `AfiFaultEntry` field | Type | Description |
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
| `total_commands` | `uint32_t` | Config API commands processed |
| `success_count` | `uint32_t` | Successful commands |
| `fail_count` | `uint32_t` | Failed commands |

#### `AfiNetworkConfig`

Every field is stored in NVM and applies after `Restart()`.

| Field | Type | Description |
|-------|------|-------------|
| `ip_address` | `string` | Sensor IP address |
| `subnet_mask` | `string` | Subnet mask |
| `gateway` | `string` | Gateway |
| `api_port` | `uint16_t` | Config API port (default 30500) |
| `vlan_id` | `uint16_t` | VLAN ID (0 = disabled) |
| `detection` | `AfiStreamDestination` | RDI destination (sensor default port 30509) |
| `health` | `AfiStreamDestination` | SHII destination (sensor default port 30510) |
| `performance` | `AfiStreamDestination` | SPI destination (sensor default port 30511) |

A value-initialized `AfiStreamDestination` has port `0`; fill it from `GetNetworkConfig()` before `SetNetworkConfig()`.

#### `AfiStreamDestination` / `AfiStreamKind`

| Field | Type | Description |
|-------|------|-------------|
| `ip` | `string` | Destination IP |
| `port` | `uint16_t` | Destination port |
| `protocol` | `string` | `"tcp"` or `"udp"` |

```cpp
enum class AfiStreamKind : uint8_t {
    kDetection   = 0,  // RDI
    kHealth      = 1,  // SHII
    kPerformance = 2,  // SPI
};
```

#### `AfiDetectionThreshold`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `snr_dB` | `float` | `5.0` | Level `1..9`, fractions allowed (wire key `snr_threshold_db`) |
| `cfar_table_first_frame` | `vector<uint8_t>` | 19 × `5` | First-frame CFAR levels, 19 values in `1..9` |
| `cfar_table_second_frame` | `vector<uint8_t>` | 19 × `5` | Second-frame CFAR levels, 19 values in `1..9` (DR and UDR) |
| `has_cfar_table_second_frame` | `bool` | `true` | GET: the response carried the second table. SET: send it |

#### `AfiDetectionFilters`

Eight filter toggles with the interface defaults. Interface v1.1.0 firmware stores `filter_ego_specular`, `filter_upper_structure` and `filter_wheel_microdoppler` but does not build those stages.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `filter_ego_specular` | `bool` | `false` | Ego specular reflection filter |
| `filter_ground_sidelobe` | `bool` | `true` | Ground sidelobe filter |
| `filter_upper_structure` | `bool` | `false` | Upper structure filter |
| `filter_multibounce_2x` | `bool` | `true` | 2x multibounce filter |
| `filter_structure_multipath` | `bool` | `true` | Structure multipath filter |
| `filter_specular_mirror` | `bool` | `true` | Specular mirror filter |
| `filter_wheel_microdoppler` | `bool` | `false` | Wheel micro-Doppler filter |
| `filter_ground_reflection` | `bool` | `false` | Ground reflection filter (SR subcycle) |

#### `AfiPtpConfig`

Profile values are `0=gPTP`, `1=1588v2-L2`, `2=1588v2-UDP`.

| Field | Type | Description |
|-------|------|-------------|
| `profile` | `uint8_t` | Running profile |
| `profile_name` | `string` | Running profile name |
| `configured_profile` | `uint8_t` | Stored profile for the next boot |
| `configured_profile_name` | `string` | Stored profile name |
| `restart_required` | `bool` | Stored profile differs from the running one |
| `domain` | `uint8_t` | PTP domain |
| `priority1`, `priority2` | `uint8_t` | Firmware constant `255` (slave only) |
| `state` | `string` | `"INIT"` / `"SLAVE"` / `"NOT_SYNCED"` (`"MASTER"` is defined but not reached by interface v1.1.0 firmware) |
| `state_code` | `uint8_t` | 1 = SLAVE, 2 = MASTER, 0 otherwise |
| `offset_ns` | `int64_t` | Clock offset (ns) |
| `mean_path_delay_ns` | `int64_t` | Mean path delay (ns) |
| `master_clock_id` | `string` | Master clock identifier |
| `grid_offset_ns` | `int64_t` | System time minus PTP time (ns). Non-zero means stream timestamps and the send grid differ from PTP time by this amount |
| `grid_state` | `string` | `"FREE"` / `"HOLD"` / `"CONVERGING"` / `"ALIGNED"` |

#### `AfiMountingPosition` / `AfiRotation` / `AfiSensorPosition`

| Struct | Field | Type | Description |
|--------|-------|------|-------------|
| `AfiMountingPosition` | `x`, `y`, `z` | `float` | Mounting offset (m) |
| `AfiRotation` | `roll`, `pitch`, `yaw` | `float` | Rotation (deg) |
| `AfiSensorPosition` | `position_code` | `uint8_t` | Mounting position code |

### Firmware Update Structs

#### `AfiOtaStartRequest`

| Field | Type | Description |
|-------|------|-------------|
| `update_type` | `string` | `"firmware"` or `"bootloader"` |
| `component` | `string` | `"m7_0"`, `"m7_1"`, `"m7_2"`, `"a53"` (firmware) or `"bootloader"` |
| `total_size` | `uint32_t` | Image size (bytes) |
| `crc32` | `string` | Image CRC-32 (IEEE) as `"0x%08X"`. 8 bare hex digits get the `0x` prefix. Empty omits the key and the sensor skips the image check |

#### `AfiOtaStartResponse`

| Field | Type | Description |
|-------|------|-------------|
| `max_chunk_size` | `uint32_t` | Chunk ceiling; larger chunks are refused. `4096` when the response omits it |
| `target_slot` | `string` | `"A"` or `"B"`. Firmware: the inactive face. Bootloader: always `"B"` (not a slot switch) |

#### `AfiOtaChunkResult`

| Field | Type | Description |
|-------|------|-------------|
| `received_bytes` | `uint32_t` | Bytes accepted so far; the next offset to send |
| `total_bytes` | `uint32_t` | `total_size` of the session |

#### `AfiOtaStatus`

| Field | Type | Description |
|-------|------|-------------|
| `state` | `string` | `"idle"`, `"transferring"`, `"verifying"`, `"writing"` or `"error"` |
| `progress_pct` | `uint8_t` | Transfer progress (0-100); 100 while verifying and writing after a commit |
| `current_component` | `string` | Component of the session, or `"none"` |
| `error_message` | `string` | Empty from `FirmwareUpdateStatus`, even in `"error"`. A failed update step reports its reason in `GetLastSensorResult().error_message` |

#### `AfiOtaSwitchSlotResult`

| Field | Type | Description |
|-------|------|-------------|
| `previous_active` | `string` | Active face before the switch (`"A"` or `"B"`) |
| `new_active` | `string` | Active face after the switch |

#### `AfiBootInfo`

| Field | Type | Description |
|-------|------|-------------|
| `fw_update_active` | `string` | Active firmware face (`"A"` / `"B"`) |
| `fw_update_valid_a` | `bool` | Face A holds a verified image |
| `fw_update_valid_b` | `bool` | Face B holds a verified image |
| `fw_update_boot_count` | `uint32_t` | Stored boot count (not incremented by interface v1.1.0 firmware) |
| `boot_info_status` | `string` | `"ok"` / `"backup_used"` / `"default"` |

### Config Persistence Structs

| Struct | Field | Type | Description |
|--------|-------|------|-------------|
| `AfiConfigStatus` | `load_status` | `uint8_t` | 0=LOAD_OK, 1=BACKUP, 2=DEFAULTS |
| `AfiConfigStatus` | `load_status_name` | `string` | Status name |
| `AfiResetConfigDefaultsResult` | `reset_fields` | `uint32_t` | Number of fields reset |

---

## Error Codes

```cpp
enum AfiError {
    kOk              =  0,
    kNotInitialized  = -1,
    kAlreadyRunning  = -2,
    kConnectFailed   = -3,
    kTimeout         = -4,
    kDisconnected    = -5,
    kInvalidParam    = -6,
    kProtocolError   = -7,
    kSensorError     = -8,
    kInternalError   = -9,
    kNotSupported    = -10,
    kBusy            = -11,
    kSessionRequired = -12,
    kAccessDenied    = -13,
    kInvalidState    = -14,
};

const char* AfiErrorStr(int code);
```

| Code | Name | Meaning | Recovery |
|------|------|---------|----------|
| 0 | `kOk` | Success | |
| -1 | `kNotInitialized` | `Init()` not called, Config API disabled, or after `Stop()` | Call `Init()` |
| -2 | `kAlreadyRunning` | `Init()` while initialized or running | Call `Stop()` first |
| -3 | `kConnectFailed` | Connect failed (stream socket, CSII socket, `ReconnectConfigApi` timeout) | Check IP, port and network |
| -4 | `kTimeout` | No Config API response in time | Retry; the next call reconnects |
| -5 | `kDisconnected` | Config API connection lost or not connected | Retry; the next call reconnects |
| -6 | `kInvalidParam` | Argument rejected by the SDK or the sensor (`INVALID_PARAM`), or request body above 16,384 B | Fix the argument |
| -7 | `kProtocolError` | Response does not answer the request, or its content is malformed | Check firmware compatibility |
| -8 | `kSensorError` | Other sensor return code (e.g. `INTERNAL_ERROR`) | Check `GetLastSensorResult()` |
| -9 | `kInternalError` | Internal SDK error | Report a bug |
| -10 | `kNotSupported` | Sensor `NOT_SUPPORTED` (method absent on this image or reserved) | Check the firmware image |
| -11 | `kBusy` | Sensor `BUSY`: more than 200 requests per second | Slow down |
| -12 | `kSessionRequired` | Sensor `SESSION_REQUIRED`: method not open in the current access mode | `EnterMode()` and retry |
| -13 | `kAccessDenied` | Sensor `ACCESS_DENIED`: mode re-entry or a step back from Programming | Reboot when leaving Programming |
| -14 | `kInvalidState` | Sensor `INVALID_STATE`: step out of order (e.g. update session not idle) | Poll status, abort, retry |

`AfiErrorStr()` returns a readable string, or `"Unknown error"` for unknown codes.

### Sensor return codes

```cpp
enum AfiReturnCode {
    kRcSuccess = 0x00, kRcInvalidParam = 0x01, kRcConnectionFail = 0x02,
    kRcNoData = 0x03, kRcMemoryError = 0x04, kRcNotSupported = 0x05,
    kRcInternalError = 0x07, kRcBusy = 0x08, kRcModeChangeFail = 0x09,
    kRcCalibrationInProgress = 0x0A, kRcTimeout = 0x0B,
    kRcSessionRequired = 0x0C, kRcAccessDenied = 0x0D, kRcInvalidState = 0x0E,
};

int AfiErrorFromReturnCode(int rc);       // AfiError for a sensor code (kSensorError when none)
const char* AfiReturnCodeName(int rc);    // "SUCCESS", "SESSION_REQUIRED", ... ("UNKNOWN")
```

### Python error codes

Python uses the same values as C++:

| Python | Value | C++ |
|--------|-------|-----|
| `OK` | `0` | `kOk` |
| `NOT_INITIALIZED` | `-1` | `kNotInitialized` |
| `CONNECT_FAILED` | `-3` | `kConnectFailed` |
| `TIMEOUT` | `-4` | `kTimeout` |
| `DISCONNECTED` | `-5` | `kDisconnected` |
| `INVALID_PARAM` | `-6` | `kInvalidParam` |
| `PROTOCOL_ERROR` | `-7` | `kProtocolError` |
| `SENSOR_ERROR` | `-8` | `kSensorError` |
| `INTERNAL_ERROR` | `-9` | `kInternalError` |
| `NOT_SUPPORTED` | `-10` | `kNotSupported` |
| `BUSY` | `-11` | `kBusy` |
| `SESSION_REQUIRED` | `-12` | `kSessionRequired` |
| `ACCESS_DENIED` | `-13` | `kAccessDenied` |
| `INVALID_STATE` | `-14` | `kInvalidState` |

`error_from_result(rc)` maps a `ConfigClient` result (sensor return code or `ERR_*`) to these values.

---

## Python API

The Python SDK (`afi_sdk`, Python 3.8+, standard library only) provides the Config API, CSII vehicle input, Discovery, the firmware update sequence (`afi_sdk.firmware`), and stream parsing helpers (`afi_sdk.streams`). It does not open stream receive sockets.

### Python `AfiSdkConfig`

```python
from afi_sdk import AfiSdkConfig

@dataclass
class AfiSdkConfig:
    sensor_ip: str = "192.168.10.150"
    config_port: int = 30500
    csii_port: int = 30501            # CSII destination
    rdi_port: int = 30509
    shi_port: int = 30510
    spi_port: int = 30511
    timeout: float = 5.0              # Config API connect and response timeout (s)
    stream_transport: str = "tcp"
    validate_e2e: bool = True
    csii_transport: str = ""          # "" by generation, "udp" or "tcp"
```

`AfiSdk` uses `sensor_ip`, `config_port`, `csii_port`, `timeout` and `csii_transport`. The stream fields are for the caller's own receive code.

### Python `AfiSdk` Class

```python
class AfiSdk:
    def init(self, config: AfiSdkConfig) -> int
    def reconnect(self, timeout_s: float = 60.0, interval_s: float = 1.0) -> int
    def close(self)
    connected: bool          # property
    interface_generation: int    # property, INTERFACE_GEN_*
    last_error: int          # property
    last_result: Optional[dict]  # property

    @staticmethod
    def discover(timeout_s: float = 3.0, bind_ip: str = "") -> List[dict]
```

- `init()` connects the Config API and returns `OK` or `CONNECT_FAILED` (`INVALID_PARAM` for an unknown `csii_transport`). After a failed `init()`, call `reconnect()`.
- `interface_generation` is `INTERFACE_GEN_V1_0`, `INTERFACE_GEN_V1_1` or `INTERFACE_GEN_UNKNOWN` from the GetDeviceInfo request that opens each connection. On `INTERFACE_GEN_V1_0` every Config API call returns `NOT_SUPPORTED` without sending, and `connected` is `False`.
- After a successful connect, any timeout, lost connection or mismatched response drops the socket and the next request reconnects. Request bodies above 16,384 bytes are refused before sending (`INVALID_PARAM`).
- `reconnect()` retries the connect every `interval_s` until `timeout_s` and returns `OK`, `NOT_INITIALIZED` or `CONNECT_FAILED`. Use it after `restart()` or `firmware_update_reboot()`.
- `close()` closes the Config API and CSII sockets. Later Config API calls return `NOT_INITIALIZED`.
- `last_error`: SDK error code of the last Config API call (`OK` on success).
- `last_result`: the sensor's outcome of the last call as `{return_code, return_name, error_message, data, e2e_ok}`. For a transport failure, `return_code` is the negative `ERR_*` value and `return_name` is `"TRANSPORT"`, `"TIMEOUT"`, `"PROTOCOL"` or `"TOO_LARGE"`.
- `discover()` returns one dict per sensor, as the sensor sent it (`serial_number`, `software_version`, `ip_address`, `range_mode`, ports, `scan_status: {scan_index, uptime_seconds}`, ...), plus `_source_ip`, `_source_port` and `_interface_generation` (`discovery_generation()`). Collection, deduplication and misses behave as in [C++ Discovery](#discovery).

`AfiSdk` supports the context manager protocol:

```python
with AfiSdk() as sdk:
    sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))
    info = sdk.get_device_info()
# close() runs on exit
```

### Python Method Overview

GET methods return `Optional[dict]` (`None` on error). SET and command methods return `int` (`OK` or a negative error code), unless noted otherwise. On failure, `last_error` and `last_result` describe the cause. Access modes and persistence are the same as in C++, see [Access Modes and Persistence](#access-modes-and-persistence).

#### Vehicle Input (CSII)

| Method | Returns |
|--------|---------|
| `send_vehicle_input(somc=SOMC_NORMAL_MODE, velocity_mps=(x, y, z), rotation_rate_rps=(yaw, pitch, roll), wheel_speeds_rps=(fl, fr, rl, rr), steering_angle_rad=0.0, gear_position=GEAR_NEUTRAL, global_timestamp_utc=0.0, coord_system=0, sensor_id=1, timestamp_ns=0)` | `int` |

Sends one CSII message to `csii_port` (Event `0x8001`): one UDP datagram on interface v1.1.0, one message on a TCP connection on v1.0.x (`csii_transport` overrides). Call cyclically (100 ms recommended). The socket is created on first use and the SDK maintains the message and E2E counters. Returns `NOT_INITIALIZED` before `init()` and `DISCONNECTED` on a socket error. SOMC and gear constants live in `afi_sdk.streams`.

#### Access Mode (0x00Ex)

| Method | Returns |
|--------|---------|
| `get_mode()` | `{access_mode, access_mode_name, elapsed_ms, streams_halted}` |
| `set_mode(mode)` | `int` (`AccessMode.DIAGNOSTIC` or `AccessMode.PROGRAMMING`; others give `INVALID_PARAM`) |
| `enter_mode(target)` | `int` (`OK` when already there; `ACCESS_DENIED` from Programming to Diagnostic) |

#### Device Info & Status (0x001x)

| Method | Returns |
|--------|---------|
| `get_device_info()` | `{serial_number, mac_address, model_name, hw_version, software_version, bootloader_hash, fw_hash}` |
| `get_sensor_status()` | `{operation_state, operation_state_code, temperature_mcu, temperature_rf, voltage, uptime_sec}` |
| `get_diagnostic_info()` | `{combined_state, combined_state_code, max_fault_level, max_fault_level_code, trigger_fault_id, active_fault_count, m7_snapshot_stale, m7_stale_cycles, faults[]}` |
| `restart()` | `int`; the connection drops after the reply |
| `reset_all_settings()` | `int`; diagnostic mode; writes defaults and restarts |
| `get_command_history_info()` | `{total_commands, success_count, fail_count}` |

#### Network (0x002x)

| Method | Returns |
|--------|---------|
| `get_network_config()` | `{ip_address, subnet_mask, gateway, api_port, vlan_id, detection_ip, detection_port, detection_protocol, health_..., performance_...}` |
| `set_network_config(**fields)` | `Optional[dict]` (contains `restart_required`) |
| `set_stream_destination(stream, ip, port, protocol="tcp")` | `int` |

- `set_network_config` is a partial update: only the given keyword fields are sent. Keys: `ip_address`, `subnet_mask`, `gateway`, `api_port`, `vlan_id`, `{detection,health,performance}_{ip,port,protocol}`.
- `set_stream_destination`: `stream` is `"detection"`, `"health"` or `"performance"`; other values give `INVALID_PARAM`.
- Both are stored in NVM and apply after `restart()`.

#### Range Mode (0x003x)

| Method | Returns |
|--------|---------|
| `get_range_mode()` | `Optional[int]` (0=DR ... 6=UDR) |
| `set_range_mode(mode)` | `int`; applies after `restart()` |

#### Detection (0x004x)

| Method | Returns |
|--------|---------|
| `get_detection_threshold()` | `{snr_threshold_db, cfar_table_first_frame[, cfar_table_second_frame]}` |
| `set_detection_threshold(snr_db=None, cfar_table_first_frame=None, cfar_table_second_frame=None)` | `int` (any subset) |
| `get_detection_filters()` | `{filter_ego_specular, filter_ground_sidelobe, filter_upper_structure, filter_multibounce_2x, filter_structure_multipath, filter_specular_mirror, filter_wheel_microdoppler, filter_ground_reflection}` |
| `set_detection_filters(**filters)` | `int` (any subset of the eight toggles) |

`snr_db` is a level in `1..9` (fractions kept); each CFAR table holds 19 integers in `1..9`. Values outside these ranges give `INVALID_PARAM` without sending. `get_detection_threshold` returns `None` with `PROTOCOL_ERROR` when the sensor's level is out of range.

#### Time Sync (0x005x)

| Method | Returns |
|--------|---------|
| `get_ptp_config()` | `{profile, profile_name, configured_profile, configured_profile_name, restart_required, domain, priority1, priority2, state, state_code, offset_ns, mean_path_delay_ns, master_clock_id, grid_offset_ns, grid_state}` |
| `set_ptp_config(profile=0)` | `int` (`0`, `1` or `2`; applies after restart) |
| `set_ptp_config_with_result(profile=0)` | `{profile, restart_required}` or `None` |

#### Mounting (0x007x)

| Method | Returns |
|--------|---------|
| `get_mounting_position()` | `{offset_x_m, offset_y_m, offset_z_m}` |
| `set_mounting_position(x, y, z)` | `int` |
| `get_rotation()` | `{yaw_deg, pitch_deg, roll_deg}` |
| `set_rotation(yaw, pitch, roll)` | `int` |
| `get_sensor_position()` | `Optional[int]` (position code) |
| `set_sensor_position(position_code)` | `int` |

#### Firmware Update (0x008x)

| Method | Returns |
|--------|---------|
| `firmware_update_start(update_type, component, total_size, crc32)` | `{max_chunk_size, target_slot, state, progress_pct, current_component, error_message}` or `None` |
| `firmware_data_chunk(data)` | `{received_bytes, total_bytes, state, ...}` or `None` |
| `firmware_update_commit()` | `int` |
| `firmware_update_status()` | `{state, progress_pct, current_component, error_message}` |
| `firmware_update_abort()` | `int` |
| `firmware_switch_slot(update_type)` | `{previous_active, new_active, ...}` or `None` |
| `firmware_update_reboot()` | `int` |
| `get_boot_info()` | `{fw_update_active, fw_update_valid_a, fw_update_valid_b, fw_update_boot_count, boot_info_status}` |

- `update_type` is `"firmware"` or `"bootloader"`.
- `crc32` is an `int` or a string. An `int` is sent as `"0x%08X"`; a string of 8 bare hex digits gets the `0x` prefix; `None` omits the key and the sensor skips the image check. An empty string or another type gives `INVALID_PARAM`.
- `firmware_data_chunk` may be accepted only in part: `received_bytes` is the next offset.
- The rules and order are those of the [C++ firmware update](#firmware-update-0x008x); `afi_sdk.firmware` implements them.

#### Config Persistence (0x009x)

| Method | Returns |
|--------|---------|
| `get_config_status()` | `{load_status, load_status_name}` |
| `reset_config_defaults(keys)` | `{reset_fields}` or `None`; diagnostic mode; writes NVM, applies after restart |

### Python `RangeMode` / `AccessMode`

```python
from afi_sdk import RangeMode, AccessMode

RangeMode.DUAL_RANGE         # 0 (DR)
RangeMode.LONG_RANGE         # 1 (LR)
RangeMode.MID_RANGE          # 2 (MR)
RangeMode.SHORT_RANGE        # 3 (SR)
RangeMode.ULTRA_LONG_RANGE   # 4 (ULR)
RangeMode.ULTRA_SHORT_RANGE  # 5 (USR)
RangeMode.ULTRA_DUAL_RANGE   # 6 (UDR)
RangeMode.name(1)            # "LR"

AccessMode.NORMAL            # 0
AccessMode.DIAGNOSTIC        # 1
AccessMode.PROGRAMMING       # 2
```

### Python Firmware Update (`afi_sdk.firmware`)

`afi_sdk.firmware` runs the complete update sequence on a connected `AfiSdk`. It takes component images as bytes; bundle extraction, decryption and version selection belong to the caller.

```python
from afi_sdk import AfiSdk, AfiSdkConfig, firmware

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))
uptime = firmware.radar_uptime("192.168.10.150")

result = firmware.update(sdk, firmware={"m7_0": m70, "m7_1": m71, "m7_2": m72, "a53": a53},
                         bootloader=None, reboot=True)
if result.ok and result.rebooted:
    firmware.wait_for_restart("192.168.10.150", uptime)
    sdk.reconnect()
    problems = firmware.verify(sdk, result.target_face, fw_hash="1a2b3c4d")
```

| Function | Description |
|----------|-------------|
| `update(sdk, firmware=None, bootloader=None, reboot=True, on_progress=None) -> UpdateResult` | Runs the sequence below. `firmware` maps `m7_0`, `m7_1`, `m7_2`, `a53` to images and must hold all four or be `None`; a subset is refused before anything is sent |
| `upload_component(sdk, update_type, component, image, crc32=None, on_progress=None) -> StepResult` | Start, chunks until `received_bytes` equals the size, commit, wait for idle. Any failure after the start sends `FirmwareUpdateAbort` |
| `wait_idle(sdk, timeout_s, step="", on_progress=None, total=0) -> Optional[str]` | Polls `FirmwareUpdateStatus` until `"idle"`. Returns `None`, or the reason (`"error"` state, status failure, timeout) |
| `radar_uptime(ip, timeout_s=1.5, discover=None) -> Optional[int]` | Discovery `scan_status.uptime_seconds` of the radar at `ip` (the value `wait_for_restart` compares), or `None` when it does not answer |
| `wait_for_restart(ip, uptime_before, timeout_s=120.0, interval_s=0.5, discover=None) -> Optional[dict]` | Sends Discovery to `ip` until it answers with an uptime lower than `uptime_before`. Returns the Discovery record, or `None` after `timeout_s` |
| `verify(sdk, target_face, fw_hash=None, bootloader_hash=None) -> List[str]` | After the restart: checks that `target_face` is active and valid with `boot_info_status` `"ok"`, and compares the device hashes. Returns the problems found (empty when all pass) |
| `hashes_match(reported, expected) -> bool` | Git short hashes match on a common prefix of 7 or more characters, ignoring a `-dirty` suffix and case |
| `image_crc32(image) -> int` | IEEE CRC-32 of an image, the value `FirmwareUpdateStart` checks |
| `commit_timeout_s(size) -> float` | Wait budget after a commit: `max(120 s, size / 50 KiB/s)` |

`update()` performs these steps and stops at the first failure:

1. `enter_mode(AccessMode.PROGRAMMING)`, through Diagnostic. Data streams stop until the reboot.
2. Bootloader, when given: `upload_component("bootloader", "bootloader", ...)`.
3. Firmware, when given: reads the active face from `get_boot_info()`, uploads `m7_0`, `m7_1`, `m7_2`, `a53` in that order, and checks that each start names the inactive face as `target_slot`. When the sensor moved the active marker at a commit, the module switches it back before the next component so that all four land on the same face.
4. One `firmware_switch_slot("firmware")` after every commit reached idle, a wait for idle, and a Boot-Info check that the written face is active.
5. With `reboot=True`, `firmware_update_reboot()`, sent while the session is idle. A lost connection or a timeout on this call counts as accepted.

Chunks use the `max_chunk_size` from the start answer (4096 when absent) and resume at `received_bytes`; 200 consecutive chunks without progress abort the component.

| Type | Fields |
|------|--------|
| `StepResult` | `step` (component or step name), `ok`, `error`, `target_slot` |
| `UpdateResult` | `ok`, `steps` (list of `StepResult`), `target_face` (`"A"`/`"B"`, `None` for bootloader only), `rebooted`, `error` (property: `"<step>: <reason>"` of the last failed step) |

`on_progress(step, state, percent, received_bytes, total_bytes)` is called during transfers and idle waits. Module constants: `FIRMWARE_COMPONENTS`, `BOOTLOADER_COMPONENT`, `DEFAULT_MAX_CHUNK_SIZE` (4096), `MAX_STALLED_CHUNKS` (200), and the step names `STEP_MODE`, `STEP_BOOT_INFO`, `STEP_SWITCH`, `STEP_REBOOT`.

### Python Stream Helpers (`afi_sdk.streams`)

`afi_sdk.streams` parses and builds the wire format `SOME/IP 16B + E2E 20B + ISO 23150 payload`:

```python
from afi_sdk.streams import (
    E2E_DATA_ID_RDI,
    strip_e2e,
    parse_rdi_frame,
    parse_rdi_detection,
)

e2e, iso = strip_e2e(someip_payload, expected_data_id=E2E_DATA_ID_RDI)
if e2e and e2e["ok"]:
    header = parse_rdi_frame(iso)        # 36B RDI header
    for i in range(header["num_detections"]):
        det = parse_rdi_detection(iso, header["det_start"], i)  # 51B each
```

| Function | Description |
|----------|-------------|
| `crc32_ieee(data)` | IEEE 802.3 CRC-32 used by the E2E header (KAT `0xCBF43926`) |
| `e2e_data_id(sub16)` | E2E Data ID `0x6000 << 16 | sub16` (Event ID or Method ID) |
| `crc64_xz(data)` | CRC-64/XZ used by the interface v1.0.x E2E header (KAT `0x995DC9BBDF1939FA`) |
| `parse_e2e(frame)` | Parses the 20B E2E header into `{crc_high, crc, length, counter, data_id}`, or `None` if too short |
| `strip_e2e(frame, validate=True, expected_data_id=None)` | Returns `(e2e, body)`. `e2e` adds `length_ok`, `data_id_ok`, `crc_zero`, `crc_ok` (with `validate`; CRC-64/XZ when the high word is not 0), `ok` and `generation` (`INTERFACE_GEN_*`). A failed check logs a warning and keeps the body |
| `build_e2e_header(data_id, counter, payload)` | Builds a 20B E2E header with the computed CRC |
| `SomeipTpReassembler(max_payload_size, max_sessions=8, max_segment_size=1392)` | Bounded, session-aware UDP SOME/IP-TP reassembly. `feed(tp_header, payload, session_id)` returns the complete payload or `b""`; `reset(session_id=None)` discards state |
| `parse_rdi_frame(payload)` | Parses the 36B RDI header (Common Header fields, `radial_velocity_begin/end`, `detections_capability`, `num_detections`, `det_start`). `None` when the length does not match the count |
| `parse_rdi_detection(payload, det_start, index)` | Parses one 51B detection |
| `parse_shi(payload)` | Parses a SHII payload: the whole Common Header (including `data_qualifier`) and the health fields |
| `parse_spi(payload)` | Parses an SPI payload: the whole Common Header, pose, FoV segments, recognisable object types and reference target types |
| `build_csii_payload(...)` | Serializes a 79B CSII payload (interface version 1.1.0, `sensor_id` default 1) |
| `build_csii_frame(payload, counter, session_id)` | Wraps a CSII payload with E2E and SOME/IP headers (115B frame) |

Constants: `E2E_HEADER_SIZE`, `E2E_DATA_ID_CSII` / `_RDI` / `_SHII` / `_SPI`, `CRC32_CHECK`, `RDI_HEADER_SIZE`, `RDI_DETECTION_SIZE`, `RDI_MAX_DETECTIONS`, `RDI_MAX_PAYLOAD_SIZE`, `CSII_PAYLOAD_SIZE`, `CSII_FRAME_SIZE`, `SOMC_*` (sensor operation mode commands) and `GEAR_*` (gear positions).

DataQualifier `0` marks new scan data, `6` a resend of the previous scan, and `1` content that must not be used.

### Low-Level SOME/IP Client (`afi_sdk.someip`)

`ConfigClient` gives direct access to Config API methods:

```python
from afi_sdk.someip import ConfigClient, METHOD_GET_DEVICE_INFO

with ConfigClient("192.168.10.150") as client:
    rc, data = client.request(METHOD_GET_DEVICE_INFO)
    if rc == 0:
        print(data)
```

| Member | Description |
|--------|-------------|
| `ConfigClient(host, port=30500, timeout=5.0)` | Client for one sensor. The context manager connects on entry and closes on exit |
| `connect() -> bool` | Connects, sends the GetDeviceInfo probe, and enables reconnect on demand. `True` for an interface v1.0.x sensor too; its connection is closed after the probe |
| `close()` | Closes; later requests fail until `connect()` |
| `request(method_id, payload=None) -> (rc, data)` | Sends a JSON request. `rc` is `0` on success, a positive sensor return code (`RC_*`), or a negative `ERR_*` transport result. `data` is the response `data` object |
| `request_raw(method_id, body) -> (rc, data)` | Sends a binary body (firmware chunks); the reply is JSON |
| `last_result` | `{return_code, return_name, error_message, data, e2e_ok}` of the last request |
| `last_response_e2e` | `{crc, length, counter, data_id, ok}` of the last response |
| `connected` | `True` while a socket is open |
| `interface_generation` | `INTERFACE_GEN_*` from the last probe. On `INTERFACE_GEN_V1_0`, `request()` and `request_raw()` return `(RC_NOT_SUPPORTED, None)` without sending until `connect()` |

Transport results: `ERR_TRANSPORT` (-1), `ERR_TIMEOUT` (-2), `ERR_PROTOCOL` (-3), `ERR_TOO_LARGE` (-4, body above `MAX_REQUEST_BODY` = 16,384, nothing sent). `afi_sdk.someip` also exports the Config API method IDs `METHOD_*` (`0x0010` to `0x00E1`), `RC_*` and `RETURN_CODE_NAMES`, `ACCESS_MODE_*`, `RANGE_MODE_*` and `RANGE_MODE_NAMES`, the `EVENT_*` and `DEFAULT_*_PORT` constants, `INTERFACE_GEN_UNKNOWN` / `_V1_0` / `_V1_1`, `discovery_generation(info)`, and `discover(timeout_s=3.0, broadcast_ip="255.255.255.255", port=30520, bind_ip="")`.

---

## Multi-Sensor Usage

Create one `AfiSdk` instance per sensor. Each instance is independent.

- TCP streams (default): each instance connects to its own sensor, so the sensors can use the same ports.
- UDP streams: give each sensor its own host port, both on the sensor (`SetNetworkConfig`, applies after `Restart()`) and in that instance's `AfiSdkConfig`. Each receiver accepts only its own sensor's datagrams.

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
