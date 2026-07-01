# Changelog

All notable changes to the AFI920 SDK will be documented in this file.

---

## Unreleased

### Added

- **Software update** — flash `bootloader`/`driver`/`firmware` from a software
  bundle over the Config API (methods `0x0080`–`0x0087`). New Python module
  `afi_sdk.software_update` and CLI `tools/software_update.py`: discovery-driven,
  updates up to 8 sensors in parallel with a live progress display, skips domains
  whose git version already matches the sensor, and reboots + verifies. Bundle
  decryption uses the optional `cryptography` extra (`pip install afi-sdk[update]`).
- **Examples** — `sample_shii_watchdog.py` (restart on repeated SHII warnings),
  `sample_stream_reconnect.py` (auto-reconnect a stalled RDI stream),
  `sample_config_backup.py` (save/restore configuration to JSON).
- `ConfigClient.request_binary()` for raw-binary Config API payloads (update chunks).

### Changed

- RDI now supports up to **8192 detections per frame** (was 4096), tracking the
  interface bump; the C++ receive buffer is sized from `BTS_RDI_MAX_PAYLOAD_SIZE`.
- MSVC builds compile with `/utf-8` to silence C4819 on non-UTF-8 code pages.

---

## v3.0.0 — Protocol v3.0

Major release tracking the AFI920 protocol v3.0. **Breaking** — the wire
format and the public API both change; sensors running v3.0 firmware
require this SDK, and earlier sensors require SDK v2.x.

### Breaking Changes

- **Config API method IDs renumbered** — categories now occupy 10-unit
  blocks (`0x0010`–`0x00A6`). Removed APIs: standby/update-cycle/EOL mode,
  per-stream port methods, host network config, monitoring (temperature,
  power, noise, interference, load, operation log), vehicle info source,
  PTP diagnostics/lock offset, firmware/hardware/driver version getters,
  field-of-view, ghost/multipath filter, blockage detection, legacy
  `UpdateFirmware`.
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
  DR/LR/MR/SR + new ULR/USR. EOL entry moved to `SetFactoryMode`.
- **Unified network config** — `Get/SetNetworkConfig` covers the radar's
  own network and all stream destinations (partial update; per-stream
  helper `SetStreamDestination`).
- **Device info** — `GetDeviceInfo` replaces `GetInventoryInfo`; unified
  `software_version` plus bootloader/driver/fw component hashes.
- **Diagnostics** — flat fault-array schema (combined state, fault levels,
  per-fault source/level/active).
- **PTP** — live sync status merged into `GetPtpConfig`; `SetPtpConfig`
  takes only the profile.
- **Firmware update** — OTA methods moved to `0x0080`–`0x0087`;
  `GetBootInfo` fields renamed to driver/firmware slots.
- **Discovery** — response fields follow the new version/range-mode scheme;
  sensors reply with unicast + limited broadcast and the SDK deduplicates.

### Added

- **CSII vehicle input** — `AfiSdk::SendVehicleInput()` /
  `send_vehicle_input()` publishes vehicle dynamics to the sensor over
  TCP 30501 (Event `0x8001`, E2E-protected 115 B frame).
- **Detection filters** — nine toggles including the new APS noise filter.
- **Detection density** — range/angle measurement augmentation toggles.
- **Factory domain** — factory data, factory mode (normal/evaluation/EOL)
  and calibration APIs under `0x00A0`–`0x00A6`.
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
