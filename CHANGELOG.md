# Changelog

All notable changes to the AFI920 SDK will be documented in this file.

---

## v2.2.0 — Interface v1.2.0 and v1.1.0

Release tracking the AFI920 interface v1.2.0 and v1.1.0 (sensor firmware
v3.1.1).
**Breaking** — the E2E CRC, the Config API framing and parts of the
public API change. Sensors on interface v1.0.x (firmware 3.0.0 to 3.1.0)
remain supported for data reception in this release only.

### Breaking Changes

- **E2E CRC-32** — the 20-byte E2E header keeps its layout but carries an
  IEEE 802.3 CRC-32 over the body only (CRC high word 0, KAT `0xCBF43926`),
  and Config API requests and responses carry it too (Data ID
  `0x6000 << 16 | Method ID`). Python `parse_e2e()` returns `crc_high` and
  a 32-bit `crc`; `build_e2e_header()` writes CRC-32. In the C header
  `bts_iso23150_e2e.h`, `BTS_E2E_OFF_CRC` is 4 (was 0) with the new
  `BTS_E2E_OFF_CRC_HIGH` 0; `bts_e2e_crc64_update`,
  `bts_e2e_apply_crc_contig`, `bts_e2e_apply_crc_split`,
  `bts_e2e_pack_be64`, `BTS_E2E_CRC64_INIT` and `BTS_E2E_CRC64_XOROUT` are
  removed; `bts_e2e_apply_crc(hdr, body, body_len)` writes the CRC-32.
- **Interface headers on the include path** — only `bts_iso23150.h`,
  `bts_iso23150_e2e.h` and `AFI920/bts_iso23150_{rdi,shi,spi,csii}.h` are
  shipped and installed. `bts_iso23150_someip.h`, `generated/someip.h`,
  `AFI920/generated/*.h` and the interface `.c` / `.cpp` sources are removed,
  so the `bts_rdi_*`, `bts_shi_*`, `bts_spi_*` and `bts_csii_*` functions the
  headers declare have no definition in the SDK; use the SDK API instead.
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
- **Detection threshold** — `AfiDetectionThreshold::snr_dB` and the Python
  `snr_db` argument are removed; the threshold methods carry the CFAR tables
  only.
- **Detection filters** — five toggles, matching the interface:
  `filter_ground_sidelobe`, `filter_multibounce_2x`,
  `filter_structure_multipath` and `filter_specular_mirror` (default on) and
  `filter_ground_reflection` (new, default off). `filter_fov`,
  `filter_aps_noise`, `filter_ego_specular`, `filter_upper_structure` and
  `filter_wheel_microdoppler` are removed.
- **Diagnostic info** — `m7_snapshot_stale` / `m7_stale_cycles` are
  `status_snapshot_stale` / `status_stale_cycles` (C++ and Python).
- **CSII over UDP** — on interface v1.1.0 and later, `SendVehicleInput()` /
  `send_vehicle_input()` sends one UDP datagram per message to port 30501;
  `sensor_id` defaults to 1.
- **Error codes** — sensor answers formerly reported as `kSensorError` map
  to `kInvalidParam`, `kNotSupported`, `kBusy` (-11), `kSessionRequired`
  (-12), `kAccessDenied` (-13) or `kInvalidState` (-14). Python codes take
  the C++ values (`DISCONNECTED` -1 → -5, `TIMEOUT` -2 → -4,
  `PROTOCOL_ERROR` -3 → -7, `NOT_SUPPORTED` -9 → -10); Python argument
  checks return `INVALID_PARAM` and a failed `init()` `CONNECT_FAILED`.
- **SHII and SPI enumerators** — the interface headers drop or rename
  `BTS_SHI_SOM_EVALUATION_MODE` (50), `BTS_SHI_SOM_CALIBRATION_MODE` (100),
  `BTS_SHI_SOM_TEST_MODE` (201), `BTS_SHI_SDR_OUT_OF_ORDER` (2),
  `BTS_SHI_SDR_INTERNAL_MEMORY_ERROR` (1), `BTS_SHI_SDR_COMMUNICATION_ERROR`
  (4), `BTS_SHI_SDR_CONFIGURATION_ERROR` (6), `BTS_SHI_SDR_SOFTWARE_DEFECT`
  (8), `BTS_SHI_SDR_COMPUTING_POWER` (9), `BTS_SHI_CPS_*` (now
  `BTS_SHI_SCST_COMPLETED` 0, `_NOT_PERFORMED` 1, `_IN_PROGRESS` 2,
  `_FAILED` 3) and `BTS_SPI_BS_UNKNOWN` (3). Value 100 is now
  `BTS_SHI_SOM_FAULT_MODE`, defect reason 1 `BTS_SHI_SDR_SUPPLY_VOLTAGE_FAULT`
  and calibration state 2 `BTS_SHI_SCST_IN_PROGRESS`. `afi/afi_compat.h`,
  included by `afi_types.h`, keeps the old names with their v2.1.0 values as
  deprecated constants, so existing code compiles with a warning that states
  the current meaning; define `AFI_NO_COMPAT_NAMES` to omit them.
- **Defaults** — `AfiPtpConfig::priority1` / `priority2` 255 (was 128);
  `AfiDetectionFilter` turns `filter_ground_sidelobe` and
  `filter_structure_multipath` on (were off); `AfiRangeMode` gains value
  6, so exhaustive `switch` statements need a case.

### Added

- **Interface v1.2.0 RDI** — one scan arrives as header A, chunk messages
  and header B (SOME/IP Interface Version 0x02, Events `0x8002` and `0x8100`
  to `0x814B`). The SDK assembles them by E2E Counter into one `AfiRdiFrame`
  per scan; `chunk_count`, `chunks_received`, `announced_detections` and
  `IsComplete()` describe a partial scan, which keeps the detections that
  arrived. Python: `parse_rdi_header_msg`, `parse_rdi_chunk`,
  `RdiScanAssembler` and `RdiStreamDecoder`.
- **Interface generation v1.2** — `AfiInterfaceGeneration::kV1_2` /
  `INTERFACE_GEN_V1_2` from the Discovery and GetDeviceInfo `if_version` key
  (`AfiSensorInfo::if_version`, `AfiDeviceInfo::if_version`) and from the
  RDI framing.
- **Fault log** — `GetFaultLog` (`0x0017`) and Python `get_fault_log()` read
  up to 12 raw 128 B slots of the fault ring or the system information sample
  ring in every access mode.
- **VLAN ID of this boot** — `AfiDeviceInfo::vlan_id` and
  `AfiSensorInfo::vlan_id`; `AfiNetworkConfig::vlan_id` stays the stored
  value for the next boot.
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
- **Detection threshold** — the CFAR tables hold levels 1..9 per range
  segment; `cfar_table_second_frame` exists only in DR and UDR
  (`has_cfar_table_second_frame`).
- **Stream reception** — RDI loss counts gaps in the E2E Counter (one per
  send cycle) instead of MessageCounter; single-message RDI (interface
  v1.1.0 and v1.0.x) holds up to 8192 detections (was 4096); UDP receivers
  accept only datagrams from `sensor_ip`.
- **Discovery** — one collection window per call, deduplication by (source
  IP, serial number), and `AfiSensorInfo::source_ip`. From interface v1.2.0
  `range_mode` is the SHII SensorOperationMode with the access mode applied
  (0 to 6, 10 Degradation, 20 Diagnostic, 21 Programming, 100 Fault, 200
  Initialising), not the stored `GetRangeMode` value.
- **Sensor answers** — `kBusy` also covers a `GetFaultLog` flash job and any
  NVM-writing method within 3 s of boot. `AfiPtpConfig::state` is the PTP
  port state, not the lock verdict (SHII SensorTimeSync reports lock);
  `grid_offset_ns` is the radiate and send grid minus system time, and stream
  timestamps do not carry it.
- **RDI field values** — the docs follow interface v1.2.0: the ambiguity
  domain is computed per frame (`radial_velocity_begin` −17.21 to −12.05 m/s,
  `radial_velocity_end` 12.05 to 17.21 m/s) and `relative_velocity_radial`
  can lie outside it; `existence_probability` is 1 to 100 with the real,
  suspected ghost, likely ghost and ghost bands and the shape-estimation
  markers 98, 97, 95 and 93; `ambiguity_grouping_id` is always 0.
- **Firmware update** — Python `upload_component()` aborts a session that a
  refused `FirmwareUpdateStart` left in `error`; `FirmwareUpdateStatus` may
  report `"initializing"`.
- **Interface headers** — vendored under `third_party/afi920_interface/src/c`
  and updated to interface v1.2.0 (CRC-32 helpers in `bts_iso23150_e2e.h`,
  `BTS_RDI_MAX_DETECTIONS` 2048 per v1.2.0 scan; the single-message RDI of
  v1.1.0 and v1.0.x keeps `BTS_RDI_LEGACY_MAX_DETECTIONS` 8192); no extra
  checkout step is needed.
- **Examples** — Python `sample_config.py` writes only with `--write`;
  `sample_multi_sensor.py`, `set_yaw_90.py` and the C++
  `set_stream_destination` enter the Diagnostic mode, restart and
  reconnect; `restart_sensor` waits for the Config API to return.
  `sample_pointcloud.py` does the same when it switches the transport and
  counts loss on the E2E Counter. `tools/rdi_udp_sender.py` emits the
  interface v1.1.0 single-message RDI layout.
  `sample_data_stream.py` no longer writes the sensor's stream transport: it
  reads the configured transport and uses it, and decodes RDI through
  `RdiStreamDecoder`, reporting partial scans.
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
- `Stop()` hung while a TCP stream stayed connected but sent nothing.
- `Stop()` could crash a receive thread on Windows: it closed the socket
  under a blocked receive. It now wakes each thread, joins it and closes the
  socket afterwards.

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
