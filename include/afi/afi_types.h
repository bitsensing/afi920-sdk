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
#include <utility>
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
 * @brief Interface generation a sensor speaks (afi920_interface)
 *
 * The SDK receives RDI, SHII and SPI from both generations and sends CSII to
 * both. The Config API is interface v1.1.0 only: on a v1.0.x sensor every
 * Config API call returns kNotSupported without sending a request.
 * Compatibility with v1.0.x is limited to this SDK release.
 */
enum class AfiInterfaceGeneration : uint8_t {
    kUnknown = 0,  ///< Not determined yet
    kV1_0    = 1,  ///< Interface v1.0.x (firmware 3.0.0 to 3.1.0)
    kV1_1    = 2,  ///< Interface v1.1.0 (firmware 3.1.1)
};

/**
 * @brief RDI frame with SDK metadata
 *
 * Contains the deserialized RDI message plus host-side metadata.
 * The `detections` pointer in `message` points to the internal
 * `detections_storage` buffer. Copying or moving the frame safely rebinds the
 * pointer to the destination frame's storage.
 */
struct AfiRdiFrame {
    bts_rdi_message_t message{};          ///< Deserialized RDI message
    uint64_t          recv_timestamp_ns = 0; ///< Host receive timestamp (steady clock)
    uint32_t          frame_seq = 0;      ///< SDK internal frame sequence number
    std::vector<bts_rdi_detection_t> detections_storage; ///< Detection storage

    // E2E outcome (System Spec §7.5). A failed check is delivered unless
    // AfiSdkConfig::e2e_strict, so a consumer can tell the two apart here.
    uint32_t e2e_counter = 0;             ///< E2E Counter (send slot, §8.2)
    bool     e2e_checked = false;         ///< validate_e2e was on
    bool     e2e_valid = false;           ///< Length, Data ID and CRC checks passed
    /// Generation of the sender, from the E2E header: v1.1.0 puts CRC-32 in
    /// the low word with the high word 0, v1.0.x a CRC-64/XZ in both words.
    AfiInterfaceGeneration generation = AfiInterfaceGeneration::kUnknown;

    AfiRdiFrame() = default;

    /// Preserve the former aggregate member order for source-style
    /// construction while still binding the C pointer to owned storage.
    AfiRdiFrame(bts_rdi_message_t message_in,
                uint64_t recv_timestamp_ns_in = 0,
                uint32_t frame_seq_in = 0,
                std::vector<bts_rdi_detection_t> detections_storage_in = {})
        : message(message_in),
          recv_timestamp_ns(recv_timestamp_ns_in),
          frame_seq(frame_seq_in),
          detections_storage(std::move(detections_storage_in)) {
        RebindStorage();
    }

    AfiRdiFrame(const AfiRdiFrame& other)
        : message(other.message),
          recv_timestamp_ns(other.recv_timestamp_ns),
          frame_seq(other.frame_seq),
          detections_storage(other.detections_storage),
          e2e_counter(other.e2e_counter),
          e2e_checked(other.e2e_checked),
          e2e_valid(other.e2e_valid),
          generation(other.generation) {
        RebindStorage();
    }

    AfiRdiFrame& operator=(const AfiRdiFrame& other) {
        if (this != &other) {
            message = other.message;
            recv_timestamp_ns = other.recv_timestamp_ns;
            frame_seq = other.frame_seq;
            detections_storage = other.detections_storage;
            e2e_counter = other.e2e_counter;
            e2e_checked = other.e2e_checked;
            e2e_valid = other.e2e_valid;
            generation = other.generation;
            RebindStorage();
        }
        return *this;
    }

    AfiRdiFrame(AfiRdiFrame&& other) noexcept
        : message(other.message),
          recv_timestamp_ns(other.recv_timestamp_ns),
          frame_seq(other.frame_seq),
          detections_storage(std::move(other.detections_storage)),
          e2e_counter(other.e2e_counter),
          e2e_checked(other.e2e_checked),
          e2e_valid(other.e2e_valid),
          generation(other.generation) {
        RebindStorage();
        other.RebindStorage();
    }

    AfiRdiFrame& operator=(AfiRdiFrame&& other) noexcept {
        if (this != &other) {
            message = other.message;
            recv_timestamp_ns = other.recv_timestamp_ns;
            frame_seq = other.frame_seq;
            detections_storage = std::move(other.detections_storage);
            e2e_counter = other.e2e_counter;
            e2e_checked = other.e2e_checked;
            e2e_valid = other.e2e_valid;
            generation = other.generation;
            RebindStorage();
            other.RebindStorage();
        }
        return *this;
    }

    /// Rebind after directly modifying detections_storage.
    void RebindStorage() noexcept {
        message.detections = detections_storage.empty()
            ? nullptr : detections_storage.data();
    }
};

/// SHII message (direct C struct usage)
using AfiShiMessage = bts_shi_message_t;

/// SPI message (direct C struct usage)
using AfiSpiMessage = bts_spi_message_t;

/**
 * @brief Per-message metadata for the SHII/SPI callbacks that take it
 *
 * AfiShiMessage and AfiSpiMessage are the interface's C structs, so the E2E
 * outcome and receive time travel beside them.
 */
struct AfiStreamMeta {
    uint64_t recv_timestamp_ns = 0;   ///< Host receive time (steady clock)
    uint32_t e2e_counter = 0;         ///< E2E Counter (send slot, §8.2)
    bool e2e_checked = false;         ///< validate_e2e was on
    bool e2e_valid = false;           ///< Length, Data ID and CRC checks passed
    /// Generation of the sender, from the E2E header (see AfiRdiFrame).
    AfiInterfaceGeneration generation = AfiInterfaceGeneration::kUnknown;
};

// ─── Enums ───

/**
 * @brief Radar range mode (registry: 0=DR, 1=LR, 2=MR, 3=SR, 4=ULR, 5=USR, 6=UDR)
 */
enum class AfiRangeMode : uint8_t {
    kDualRange       = 0,  ///< DR
    kLongRange       = 1,  ///< LR
    kMidRange        = 2,  ///< MR
    kShortRange      = 3,  ///< SR
    kUltraLongRange  = 4,  ///< ULR
    kUltraShortRange = 5,  ///< USR
    kUltraDualRange  = 6,  ///< UDR (two subcycles, like DR)
};

/// Name the radar reports for a range mode ("DR".."UDR"; "UNKNOWN" outside 0..6).
inline const char* AfiRangeModeName(AfiRangeMode mode) {
    switch (mode) {
        case AfiRangeMode::kDualRange:       return "DR";
        case AfiRangeMode::kLongRange:       return "LR";
        case AfiRangeMode::kMidRange:        return "MR";
        case AfiRangeMode::kShortRange:      return "SR";
        case AfiRangeMode::kUltraLongRange:  return "ULR";
        case AfiRangeMode::kUltraShortRange: return "USR";
        case AfiRangeMode::kUltraDualRange:  return "UDR";
        default:                             return "UNKNOWN";
    }
}

/**
 * @brief Vehicle input (CSII) — host-provided vehicle dynamics
 *
 * Sent to the sensor's port 30501 (Event 0x8001), typically every 100 ms:
 * one UDP datagram on interface v1.1.0, a TCP stream on v1.0.x.
 * The SDK fills the interface header (timestamp, counter) automatically;
 * set timestamp_ns to override the measurement timestamp (0 = host clock).
 */
struct AfiVehicleInput {
    uint8_t  sensor_id = 1;           ///< Common Header SensorID (default 1)
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
    std::string software_version;   ///< Unified SW version (release bundle version)
    std::string bootloader_hash;    ///< git short hash (8 hex)
    std::string fw_hash;

    // Network
    std::string ip;                 ///< ip_address the radar reports
    std::string source_ip;          ///< Address the response came from
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

    /// Interface generation from the response keys: driver_hash exists only
    /// on v1.0.x, the m72_* keys only on v1.1.0.
    AfiInterfaceGeneration generation = AfiInterfaceGeneration::kUnknown;
};

// ─── Config API Response Types ───

/// Radar outcome of the last Config API call (System Spec §6).
struct AfiSensorResult {
    int return_code = -1;        ///< Radar return code (AfiReturnCode); -1 when no response arrived
    std::string return_name;     ///< Radar's name for return_code, e.g. "SESSION_REQUIRED"
    std::string error_message;   ///< data.error_message (firmware update methods)
    bool e2e_ok = false;         ///< Response E2E verdict (a failed check still delivers the answer)
};

// Category 1: Device Info & Status (0x001x)

struct AfiDeviceInfo {
    std::string serial_number;
    std::string mac_address;
    std::string model_name;
    std::string hw_version;
    std::string software_version;   ///< Unified SW version
    std::string bootloader_hash;    ///< git short hash (8 hex)
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
 * @brief Parser-independent, reassembled stream payload.
 *
 * For RDI/SHII/SPI, `payload` starts at the 20-byte E2E header and
 * contains the complete ISO 23150 payload that follows it.  The callback is
 * emitted after SOME/IP validation, TP reassembly, and E2E verification, but
 * before the version-specific domain parser.  A failed E2E check does not
 * suppress the callback; `e2e_valid` carries the result.  This lets
 * recorders retain a future interface revision even when the typed parser
 * cannot decode it yet.
 */
struct AfiWirePayloadFrame {
    AfiStreamKind stream = AfiStreamKind::kDetection;
    uint64_t recv_timestamp_ns = 0;   ///< Host receive time (steady clock)
    uint64_t sensor_timestamp_ns = 0; ///< ISO common-header timestamp
    uint32_t message_counter = 0;     ///< ISO common-header message counter
    uint32_t e2e_counter = 0;         ///< E2E Counter (send slot)
    bool e2e_checked = false;         ///< CRC verification was requested
    bool e2e_valid = false;           ///< Length, Data ID and CRC checks passed
    std::vector<uint8_t> payload;     ///< Exact reassembled SOME/IP payload
    /// Generation of the sender, from the E2E header (see AfiRdiFrame).
    AfiInterfaceGeneration generation = AfiInterfaceGeneration::kUnknown;
};

/**
 * @brief Unified network configuration (radar's own network + stream destinations)
 *
 * Every field is stored in NVM and applies after a sensor restart; the radar
 * reports restart_required=true (02_Network).
 */
struct AfiNetworkConfig {
    // Radar's own network (applies after restart)
    std::string ip_address;
    std::string subnet_mask;
    std::string gateway;
    uint16_t api_port = 30500;
    uint16_t vlan_id = 0;

    // Data stream destinations (NVM; apply after Restart)
    AfiStreamDestination detection;     ///< RDI (sensor registry port 30509)
    AfiStreamDestination health;        ///< SHII (sensor registry port 30510)
    AfiStreamDestination performance;   ///< SPI (sensor registry port 30511)
};

// Category 4: Detection (0x004x)

struct AfiDetectionThreshold {
    static constexpr std::size_t kCfarTableLength = 19;
    static constexpr uint8_t kDefaultCfarLevel = 5;

    /// Wire key is `snr_threshold_db`: a level in 1..9, not a physical dB
    /// value. Fractions are stored as sent; GET answers with 2 decimals.
    float snr_dB = 5.0f;
    std::vector<uint8_t> cfar_table_first_frame =
        std::vector<uint8_t>(kCfarTableLength, kDefaultCfarLevel);
    std::vector<uint8_t> cfar_table_second_frame =
        std::vector<uint8_t>(kCfarTableLength, kDefaultCfarLevel);
    /// The second-frame table exists only in the two-subcycle modes DR (0)
    /// and UDR (6); single modes omit it and ignore it on SET.
    bool has_cfar_table_second_frame = true;
};

/// Detection filter toggles (interface v1.1.0, 8 filters). Defaults follow
/// 04_Detection.md. ego_specular, upper_structure, and wheel_microdoppler are
/// stored by FW v3.1.1 but not built into its filter chain.
struct AfiDetectionFilters {
    bool filter_ego_specular = false;
    bool filter_ground_sidelobe = true;
    bool filter_upper_structure = false;
    bool filter_multibounce_2x = true;
    bool filter_structure_multipath = true;
    bool filter_specular_mirror = true;
    bool filter_wheel_microdoppler = false;
    bool filter_ground_reflection = false;  ///< Ground reflection (SR subcycle)
};

// Category 5: Time Sync (0x005x)

/**
 * @brief PTP configuration + live sync status (GetPTPConfig)
 *
 * `profile` is the active boot-time profile. `configured_profile` is the
 * pending NVM selection that takes effect after restart.
 */
struct AfiPtpConfig {
    uint8_t profile = 0;              ///< Active: 0=gPTP, 1=1588v2-L2, 2=1588v2-UDP
    std::string profile_name;
    uint8_t domain = 0;
    uint8_t priority1 = 255;          ///< Firmware constant (slave-only)
    uint8_t priority2 = 255;          ///< Firmware constant (slave-only)
    std::string state;                ///< "INIT" / "SLAVE" / "MASTER" / "NOT_SYNCED"
    uint8_t state_code = 0;
    int64_t offset_ns = 0;
    int64_t mean_path_delay_ns = 0;
    std::string master_clock_id;

    // v1.0.2 fields are appended to preserve the original member order.
    uint8_t configured_profile = 0;   ///< Pending NVM profile
    std::string configured_profile_name;
    bool restart_required = false;    ///< configured_profile != profile

    /// System time minus PTP time (PHC). Non-zero means stream timestamps and
    /// the transmit grid differ from PTP time by this much (Common Header).
    int64_t grid_offset_ns = 0;
    std::string grid_state;           ///< "FREE" / "HOLD" / "CONVERGING" / "ALIGNED"
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

// Category 8: Firmware Update (0x008x)

struct AfiOtaStartRequest {
    std::string update_type;     ///< "firmware" or "bootloader"
    std::string component;       ///< "m7_0"/"m7_1"/"m7_2"/"a53" or "bootloader"
    uint32_t total_size = 0;
    /// Image CRC-32 (IEEE) as "0x%08X". The radar reads a string without
    /// "0x" as decimal, so the SDK adds the prefix to 8 bare hex digits; an
    /// empty string omits the key and the radar skips the check (warned).
    std::string crc32;
};

struct AfiOtaStartResponse {
    uint32_t max_chunk_size = 4096;  ///< Chunk ceiling; larger chunks are refused
    std::string target_slot;     ///< "A" or "B" (firmware; the inactive face)
};

/// FirmwareDataChunk (0x0081) answer. A chunk may be accepted only in part
/// (256 B pages, behind the slot erase); received_bytes is the next offset
/// and the unaccepted tail is sent again (07_Firmware_Update §07.A).
struct AfiOtaChunkResult {
    uint32_t received_bytes = 0;
    uint32_t total_bytes = 0;
};

struct AfiOtaStatus {
    std::string state;            ///< "idle", "transferring", "writing", "verifying", "error"
    uint8_t progress_pct = 0;
    std::string current_component;
    std::string error_message;
};

struct AfiOtaSwitchSlotResult {
    std::string previous_active;  ///< "A" or "B"
    std::string new_active;
};

/// Boot-Info v2 (07_Firmware_Update): one firmware slot pair.
struct AfiBootInfo {
    std::string fw_update_active;      ///< fw active slot ("A"/"B")
    bool fw_update_valid_a = false;
    bool fw_update_valid_b = false;
    uint32_t fw_update_boot_count = 0;
    std::string boot_info_status;      ///< "ok" / "backup_used" / "default"
};

// Category 9: Config Persistence (0x009x)

struct AfiConfigStatus {
    uint8_t load_status = 0;           ///< 0=LOAD_OK, 1=BACKUP, 2=DEFAULTS
    std::string load_status_name;
};

struct AfiResetConfigDefaultsResult {
    uint32_t reset_fields = 0;         ///< Number of fields reset to defaults
};

// Category 11: Access Mode (0x00Ex)

/**
 * @brief Config API access mode (interface v1.1.0, 11_Access_Mode)
 *
 * Entry is one way, Normal -> Diagnostic -> Programming, and only a reboot
 * returns to Normal. Diagnostic opens every config SET; Programming opens the
 * firmware update chain and halts RDI/SHII/SPI.
 */
enum class AfiAccessMode : uint8_t {
    kNormal      = 0,
    kDiagnostic  = 1,
    kProgramming = 2,
};

struct AfiModeStatus {
    AfiAccessMode access_mode = AfiAccessMode::kNormal;
    std::string access_mode_name;   ///< "normal" / "diagnostic" / "programming"
    uint32_t elapsed_ms = 0;        ///< Time in the current mode (GetMode only)
    bool streams_halted = false;    ///< Data streams stopped (programming mode)
};

} // namespace afi
