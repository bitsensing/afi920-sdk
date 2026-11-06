# Changelog

All notable changes to the AFI920 SDK will be documented in this file.

---

## v2.2.0 — Interface v1.1.0

Release tracking the AFI920 interface v1.1.0 (sensor firmware v3.1.1).
**Breaking** — the E2E CRC, the Config API framing and parts of the
public API change. Sensors on interface v1.0.x (firmware 3.0.0 to 3.1.0)
remain supported for data reception in this release only.

### Breaking Changes

- **E2E CRC-32** — the 20-byte E2E header keeps its layout but carries an
  IEEE 802.3 CRC-32 over the body only (CRC high word 0, KAT `0xCBF43926`),
  and Config API requests and responses carry it too (Data ID
  `0x6000 << 16 | Method ID`). Python `parse_e2e()` returns `crc_high` and
  a 32-bit `crc`; `build_e2e_header()` writes CRC-32.
- **Access modes** — config SETs, `ResetAllSettings` and
  `ResetConfigDefaults` require the Diagnostic mode and the firmware update
  chain the Programming mode; otherwise the sensor answers
  `SESSION_REQUIRED`. Entry is one way (Normal → Diagnostic → Programming),
  only a reboot returns to Normal, and Programming halts RDI/SHII/SPI.
- **Persistence** — every SET writes NVM before it answers and applies after
  `Restart()`, stream destinations included. There is no save call.
- **Removed** — `ClearFaultLog` (`0x0013`), `Get/SetDetectionDensity`
  (`0x0044` / `0x0045`, `AfiDetectionDensity`) and `SaveConfig` (`0x0090`)
  with their Python wrappers and `METHOD_*` constants; `driver_hash` in
  `AfiDeviceInfo` and `AfiSensorInfo`.
- **Detection filters** — eight toggles: `filter_ground_reflection` added,
  `filter_fov` and `filter_aps_noise` removed; `filter_ground_sidelobe` and
  `filter_structure_multipath` default on, `filter_wheel_microdoppler` off.
- **CSII over UDP** — on interface v1.1.0, `SendVehicleInput()` /
  `send_vehicle_input()` sends one UDP datagram per message to port 30501;
  `sensor_id` defaults to 1.
- **Error codes** — sensor answers formerly reported as `kSensorError` map
  to `kInvalidParam`, `kNotSupported`, `kBusy` (-11), `kSessionRequired`
  (-12), `kAccessDenied` (-13) or `kInvalidState` (-14). Python codes take
  the C++ values (`DISCONNECTED` -1 → -5, `TIMEOUT` -2 → -4,
  `PROTOCOL_ERROR` -3 → -7, `NOT_SUPPORTED` -9 → -10); Python argument
  checks return `INVALID_PARAM` and a failed `init()` `CONNECT_FAILED`.
- **Defaults** — `AfiDetectionThreshold::snr_dB` 5.0 (was 12.0),
  `AfiPtpConfig::priority1` / `priority2` 255 (was 128); `AfiRangeMode`
  gains value 6, so exhaustive `switch` statements need a case.

### Added

- **Access mode API** — `GetMode` / `SetMode` / `EnterMode` (`0x00E0` /
  `0x00E1`), Python `get_mode()` / `set_mode()` / `enter_mode()`,
  `AfiAccessMode` / `AccessMode` and `AfiModeStatus`.
- **Firmware update** — `FirmwareUpdateStart`, `FirmwareDataChunk`,
  `FirmwareUpdateCommit`, `FirmwareUpdateStatus`, `FirmwareUpdateAbort`,
  `FirmwareSwitchSlot`, `FirmwareUpdateReboot` and `GetBootInfo` (`0x0080`
  to `0x0087`) in C++ and Python; the `AfiOtaChunkResult` overload of
  `FirmwareDataChunk` reports `received_bytes` to resume a partial chunk.
- **Python `afi_sdk.firmware`** — `update()` enters the Programming mode,
  writes the bootloader when given and all four firmware components
  (`m7_0`, `m7_1`, `m7_2`, `a53`) to the inactive slot face, switches the
  slot with Boot-Info confirmation and reboots; `wait_for_restart()` and
  `verify()` confirm the result.
- **Sensor result and reconnect** — `GetLastSensorResult()`
  (`AfiSensorResult`) and Python `last_error` / `last_result` hold the
  sensor's return code and name, `error_message` and the response E2E
  verdict; `AfiReturnCode` and `AfiReturnCodeName()` name the codes.
  `ReconnectConfigApi()` / `reconnect()` wait for the sensor after
  `Restart()` or `FirmwareUpdateReboot()`.
- **Interface v1.0.x data compatibility (this release only)** — RDI, SHII
  and SPI with the v1.0.x CRC-64/XZ (non-zero CRC high word) are accepted,
  and CSII reaches those sensors over TCP 30501 (`csii_transport`
  overrides). On v1.0.x every Config API call returns `kNotSupported` /
  `NOT_SUPPORTED` without sending. `GetInterfaceGeneration()`, the
  `generation` fields and Python `interface_generation` report the sensor.
- **E2E verdict and wire payloads** — `AfiRdiFrame` gains `e2e_counter`,
  `e2e_checked` and `e2e_valid`; new SHII / SPI `RegRecvCallback` overloads
  pass `AfiStreamMeta`. `RegWirePayloadCallback()` emits the validated E2E
  header plus ISO payload (`AfiWirePayloadFrame`) before typed decoding.
- **Range mode and PTP** — UDR (`kUltraDualRange` / `ULTRA_DUAL_RANGE`) and
  `AfiRangeModeName()`; PTP profiles 0 to 2 with `configured_profile`,
  `restart_required`, `grid_offset_ns` and `grid_state`. `SetRangeMode` and
  `SetPtpConfig` overloads report `restart_required`.
- **CMake package** — `cmake --install` installs the library and headers,
  and `find_package(afi_sdk)` provides the `afi::afi_sdk` target.
- **Other additions** — per-stream `rdi_transport`, `shi_transport` and
  `spi_transport` overrides; Python `crc32_ieee()`, `e2e_data_id()`,
  `SomeipTpReassembler`, `RC_*`, `ERR_*` and `ACCESS_MODE_*`.

### Changed

- **Config API transport** — a response must echo its request's Message,
  Client and Session ID; a timeout, lost connection or mismatch closes the
  socket and the next call reconnects. Request bodies above 16,384 B are
  refused before sending.
- **Detection threshold** — `snr_threshold_db` is a level in 1..9;
  `cfar_table_second_frame` exists only in DR and UDR
  (`has_cfar_table_second_frame`).
- **Stream reception** — RDI loss counts gaps in the E2E Counter (one per
  send slot) instead of MessageCounter; RDI holds up to 8192 detections
  (was 4096); UDP receivers accept only datagrams from `sensor_ip`.
- **Discovery** — one collection window per call, deduplication by (source
  IP, serial number), and `AfiSensorInfo::source_ip`.
- **Interface headers** — vendored under `third_party/afi920_interface/src/c`
  and updated to interface v1.1.0 (CRC-32 helpers in `bts_iso23150_e2e.h`,
  `BTS_RDI_MAX_DETECTIONS` 8192); no extra checkout step is needed.
- **Examples** — Python `sample_config.py` writes only with `--write`;
  `sample_multi_sensor.py`, `set_yaw_90.py` and the C++
  `set_stream_destination` enter the Diagnostic mode, restart and
  reconnect; `restart_sensor` waits for the Config API to return.
  `sample_pointcloud.py` does the same when it switches the transport and
  counts loss on the E2E Counter. `tools/rdi_udp_sender.py` emits the
  current RDI layout.
- **Integration guide** — `AFI920_SDK_GUIDE.md` / `AFI920_SDK_GUIDE_ko.md`
  describe the v2.2.0 API, access modes and firmware update.

### Fixed

- A late reply could be read as the answer to the next Config API request.
- C++ Config API calls ignored `request_timeout_ms`.
- `Init()` failed after `Stop()`.
- Copying or moving an `AfiRdiFrame` left `message.detections` on the
  source frame's storage.
- A second sensor sending to the same UDP port mixed into the stream.
- Discovery could overrun its timeout and stopped at a Windows ICMP reset.
- `sample_discovery` failed to link on MinGW (`iphlpapi`).

---

## v2.1.0 — Protocol v3.0

Release tracking the AFI920 protocol v3.0. **Breaking** — the wire
format and the public API both change; this SDK targets sensors running
v3.0 firmware (Radar SW 3.0.0).

### Breaking Changes

- **Config API method IDs renumbered** — categories now occupy 10-unit
  blocks. Several legacy methods were removed or consolidated.
- **E2E protection (AUTOSAR Profile 7)** — every RDI/SHII/SPI payload now
  carries a 20-byte E2E header (CRC-64/XZ). The SDK verifies the CRC on
  receive (`validate_e2e`, default on; `e2e_strict` drops bad frames).
- **RDI layout** — detection header 61 B → 36 B (radial-velocity-only
  ambiguity domain), detection entry 94 B → 51 B (error/probability/
  classification/debug fields removed; `object_id_reference` added).
- **SHII layout** — variable-length payload; non-contiguous operation mode
  enum (adds ULR/USR/Degradation/Evaluation/Calibration/Initialising/Test);
  12 defect reasons; CSII-based input signal types; new sensor calibration
  section; time-sync offset value removed.
- **SPI layout** — pose 48 B → 32 B (origin point errors and roll error
  removed); blockage status adds `UNKNOWN`.
- **Operation mode → Range mode** — `Get/SetRangeMode` with
  DR/LR/MR/SR + new ULR/USR.
- **Unified network config** — `Get/SetNetworkConfig` covers the radar's
  own network and all stream destinations (partial update; per-stream
  helper `SetStreamDestination`).
- **Device info** — `GetDeviceInfo` replaces `GetInventoryInfo`; unified
  `software_version` plus bootloader/driver/fw component hashes.
- **Diagnostics** — flat fault-array schema (combined state, fault levels,
  per-fault source/level/active).
- **PTP** — live sync status merged into `GetPtpConfig`; `SetPtpConfig`
  takes only the profile.
- **Discovery** — response fields follow the new version/range-mode scheme;
  sensors reply with unicast + limited broadcast and the SDK deduplicates.

### Added

- **CSII vehicle input** — `AfiSdk::SendVehicleInput()` /
  `send_vehicle_input()` publishes vehicle dynamics to the sensor over
  TCP 30501 (Event `0x8001`, E2E-protected 115 B frame).
- **Detection filters** — nine toggles including the new APS noise filter.
- **Detection density** — range/angle peak-density toggles.
- **Python `afi_sdk.streams`** — E2E parse/build (KAT-verified CRC-64/XZ),
  RDI/SHII/SPI parsers, CSII frame builder.

---

## v2.0.1 — Initial Release

Initial public release on GitHub.

### Features

- **Discovery** — Automatically detect AFI920 sensors on the network via UDP broadcast
- **Data Streaming** — Receive RDI (Point Cloud), SHII (Health), and SPI (Performance) data in real time
- **Config API** — Read and write sensor configuration over TCP (SOME/IP + JSON); 50+ methods across 14 categories
- **Multi-Sensor** — Operate up to 4 sensors simultaneously with independent instances
- **ISO 23150** — Compliant with the ISO 23150 automotive sensor data interface standard
- **TCP/UDP Transport** — Supports TCP (default) and UDP transport modes
- **Cross-Platform** — Linux, macOS, and Windows support
- **C++ SDK** — PIMPL-based, C++17, no external dependencies (`nlohmann/json` 3.12.0 bundled)
- **Python SDK** — Pure Python, no external dependencies (Python 3.8+)
- **SOME/IP-TP** — Segment reassembly for large RDI frames
- **Security Hardening** — Input validation (SOME/IP-TP reassembly size limit, parser field validation, response payload size cap)

### Examples

- `sample_discovery` — Discover sensors on the network
- `sample_pointcloud` — Receive RDI point cloud data
- `sample_data_stream` — Receive RDI/SHII/SPI data simultaneously
- `sample_config` — Read and write sensor configuration
- `sample_multi_sensor` — Receive data from multiple sensors simultaneously

### Documentation

- API Reference, README (English / Korean)
