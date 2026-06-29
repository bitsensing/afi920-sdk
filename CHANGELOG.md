# Changelog

All notable changes to the AFI920 SDK will be documented in this file.

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
