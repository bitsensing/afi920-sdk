/**
 * @file afi_types.h
 * @brief AFI920 SDK data types
 *
 * Protocol data structures are defined in protocol/ headers (C structs).
 * This file re-exports them and adds C++ convenience types.
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ISO 23150 protocol C structs
#include "bts_iso23150.h"
#include "AFI920/bts_iso23150_rdi.h"
#include "AFI920/bts_iso23150_shi.h"
#include "AFI920/bts_iso23150_spi.h"
#include "AFI920/bts_iso23150_csii.h"

namespace afi {

// ─── Data Stream Types ───

/**
 * @brief RDI frame with SDK metadata
 *
 * Contains the deserialized RDI message plus host-side metadata.
 * The `detections` pointer in `message` points to the internal
 * `detections_storage` buffer — valid until the next callback invocation.
 */
struct AfiRdiFrame {
    bts_rdi_message_t message;           ///< Deserialized RDI message
    uint64_t          recv_timestamp_ns; ///< Host receive timestamp (ns since epoch)
    uint32_t          frame_seq;         ///< SDK internal frame sequence number
    std::vector<bts_rdi_detection_t> detections_storage; ///< Detection storage
};

/// SHII message (direct C struct usage)
using AfiShiMessage = bts_shi_message_t;

/// SPI message (direct C struct usage)
using AfiSpiMessage = bts_spi_message_t;

// ─── Enums ───

/**
 * @brief Radar range mode (registry: 0=DR, 1=LR, 2=MR, 3=SR, 4=ULR, 5=USR)
 */
enum class AfiRangeMode : uint8_t {
    kDualRange       = 0,  ///< DR
    kLongRange       = 1,  ///< LR
    kMidRange        = 2,  ///< MR
    kShortRange      = 3,  ///< SR
    kUltraLongRange  = 4,  ///< ULR
    kUltraShortRange = 5,  ///< USR
};

/**
 * @brief Vehicle input (CSII) — host-provided vehicle dynamics
 *
 * Sent to the sensor over TCP 30501 (Event 0x8001), typically every 100 ms.
 * The SDK fills the interface header (timestamp, counter) automatically;
 * set timestamp_ns to override the measurement timestamp (0 = host clock).
 */
struct AfiVehicleInput {
    uint8_t  sensor_id = 0;
    uint64_t timestamp_ns = 0;                  ///< Measurement time (0 = host clock now)

    bts_csii_somc_t somc = BTS_CSII_SOMC_NORMAL_MODE;  ///< Sensor operation mode command
    double   global_timestamp_utc = 0.0;        ///< UTC seconds (environmental time)

    bts_csii_vehicle_coord_system_t coord_system = BTS_CSII_VCS_REAR_AXLE;
    float    velocity_x_mps = 0.0f;
    float    velocity_y_mps = 0.0f;
    float    velocity_z_mps = 0.0f;
    float    rotation_rate_yaw_rps = 0.0f;
    float    rotation_rate_pitch_rps = 0.0f;
    float    rotation_rate_roll_rps = 0.0f;
    float    wheel_flp_rps = 0.0f;              ///< Front-left wheel speed
    float    wheel_frp_rps = 0.0f;              ///< Front-right wheel speed
    float    wheel_rlp_rps = 0.0f;              ///< Rear-left wheel speed
    float    wheel_rrp_rps = 0.0f;              ///< Rear-right wheel speed
    float    steering_angle_rad = 0.0f;
    bts_csii_gear_position_t gear_position = BTS_CSII_GP_NEUTRAL;
};

// ─── Discovery ───

/**
 * @brief Sensor information from Discovery response
 */
struct AfiSensorInfo {
    // Device identity
    std::string serial_number;
    std::string mac_address;
    std::string model;

    // Version
    std::string hw_version;
    std::string software_version;   ///< Unified SW version (bootloader/driver/fw)
    std::string bootloader_hash;    ///< git short hash (8 hex)
    std::string driver_hash;
    std::string fw_hash;

    // Network
    std::string ip;
    std::string subnet_mask;
    std::string gateway;

    // Ports
    uint16_t    port = 30500;          ///< Config API TCP port
    uint16_t    detection_port = 30509;
    uint16_t    health_port = 30510;
    uint16_t    performance_port = 30511;

    // Data stream protocols
    std::string detection_protocol;    ///< "udp" or "tcp"
    std::string health_protocol;
    std::string performance_protocol;

    // Host destination IPs
    std::string host_ip;
    std::string detection_ip;
    std::string health_ip;
    std::string performance_ip;

    // Mounting / position
    uint8_t position_code = 0;
    float offset_x = 0.0f, offset_y = 0.0f, offset_z = 0.0f;
    float yaw_deg = 0.0f, pitch_deg = 0.0f, roll_deg = 0.0f;

    // Range mode
    AfiRangeMode range_mode = AfiRangeMode::kDualRange;
    std::string range_mode_name;

    // Operation status
    bool        radar_active = false;
    std::string radar_mode;            ///< "normal" or "dummy" (build config)

    // Scan status
    uint32_t    scan_index = 0;        ///< Current MessageCounter
    uint32_t    uptime_seconds = 0;

    // PTP status
    std::string ptp_state;             ///< "INIT" / "SLAVE" / "MASTER"
    int64_t     ptp_offset_ns = 0;
};

// ─── Config API Response Types ───

// Category 1: Device Info & Status (0x001x)

struct AfiDeviceInfo {
    std::string serial_number;
    std::string mac_address;
    std::string model_name;
    std::string hw_version;
    std::string software_version;   ///< Unified SW version
    std::string bootloader_hash;    ///< git short hash (8 hex)
    std::string driver_hash;
    std::string fw_hash;
};

struct AfiSensorStatus {
    std::string operation_state;
    uint8_t operation_state_code = 0;
    float temperature_mcu = 0.0f;
    float temperature_rf = 0.0f;
    float voltage = 0.0f;
    uint32_t uptime_sec = 0;
};

struct AfiFaultEntry {
    std::string id;           ///< hex string, e.g. "0x012"
    std::string name;
    std::string source;       ///< "A53" or "M7"
    std::string level;
    uint8_t level_code = 0;
    bool active = false;
};

struct AfiDiagnosticInfo {
    std::string combined_state;       ///< A53+M7 worst-case state
    uint8_t combined_state_code = 0;
    std::string max_fault_level;
    uint8_t max_fault_level_code = 0; ///< 0=NONE, 1=RECORD, 2=RESET
    std::string trigger_fault_id;     ///< hex string
    uint16_t active_fault_count = 0;
    bool m7_snapshot_stale = false;
    uint32_t m7_stale_cycles = 0;
    std::vector<AfiFaultEntry> faults;
};

struct AfiCommandHistory {
    uint32_t total_commands = 0;
    uint32_t success_count = 0;
    uint32_t fail_count = 0;
};

// Category 2: Network (0x002x)

struct AfiStreamDestination {
    std::string ip;
    uint16_t port = 0;
    std::string protocol;     ///< "tcp" or "udp"
};

/// Which data stream a destination refers to
enum class AfiStreamKind : uint8_t {
    kDetection = 0,    ///< RDI
    kHealth = 1,       ///< SHII
    kPerformance = 2,  ///< SPI
};

/**
 * @brief Unified network configuration (radar's own network + stream destinations)
 *
 * Radar network fields (ip_address/subnet_mask/gateway/api_port/vlan_id)
 * apply after a sensor restart; stream destinations apply immediately.
 */
struct AfiNetworkConfig {
    // Radar's own network (applies after restart)
    std::string ip_address;
    std::string subnet_mask;
    std::string gateway;
    uint16_t api_port = 30500;
    uint16_t vlan_id = 0;

    // Data stream destinations (apply immediately)
    AfiStreamDestination detection;     ///< RDI (default port 30509)
    AfiStreamDestination health;        ///< SHII (default port 30510)
    AfiStreamDestination performance;   ///< SPI (default port 30511)
};

// Category 4: Detection (0x004x)

struct AfiDetectionThreshold {
    static constexpr std::size_t kCfarTableLength = 19;
    static constexpr uint8_t kDefaultCfarLevel = 5;

    float snr_dB = 12.0f;
    std::vector<uint8_t> cfar_table_first_frame =
        std::vector<uint8_t>(kCfarTableLength, kDefaultCfarLevel);
    std::vector<uint8_t> cfar_table_second_frame =
        std::vector<uint8_t>(kCfarTableLength, kDefaultCfarLevel);
};

/// Nine detection filter toggles (SetDetectionFilters accepts any subset;
/// the SDK sends all fields)
struct AfiDetectionFilters {
    bool filter_fov = true;
    bool filter_aps_noise = false;          ///< APS sidelobe noise
    bool filter_ego_specular = false;
    bool filter_ground_sidelobe = false;
    bool filter_upper_structure = false;
    bool filter_multibounce_2x = true;
    bool filter_structure_multipath = false;
    bool filter_specular_mirror = true;
    bool filter_wheel_microdoppler = true;
};

/// Measurement density augmentation toggles (AddMsmt)
struct AfiDetectionDensity {
    bool range_density = true;
    bool angle_density = true;
};

// Category 5: Time Sync (0x005x)

/**
 * @brief PTP configuration + live sync status (GetPTPConfig)
 *
 * SetPTPConfig only accepts `profile` (currently 0 only); the remaining
 * fields are read-only status sourced from ptp4l.
 */
struct AfiPtpConfig {
    uint8_t profile = 0;              ///< PTP profile index (0 only)
    std::string profile_name;         ///< "default"
    uint8_t domain = 0;
    uint8_t priority1 = 128;
    uint8_t priority2 = 128;
    std::string state;                ///< "INIT" / "SLAVE" / "MASTER"
    uint8_t state_code = 0;
    int64_t offset_ns = 0;
    int64_t mean_path_delay_ns = 0;
    std::string master_clock_id;
};

// Category 7: Mounting (0x007x)

struct AfiMountingPosition {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct AfiRotation {
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
};

struct AfiSensorPosition {
    uint8_t position_code = 0;
};

// Category 9: Config Persistence (0x009x)

struct AfiConfigStatus {
    uint8_t load_status = 0;           ///< 0=LOAD_OK, 1=BACKUP, 2=DEFAULTS
    std::string load_status_name;
};

struct AfiResetConfigDefaultsResult {
    uint32_t reset_fields = 0;         ///< Number of fields reset to defaults
};

} // namespace afi
