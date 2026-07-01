// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/shi_parser.h"
#include "util/endian_util.h"
#include "util/logger.h"
#include <cstring>

namespace afi {

// Variable-length payload: 24B common header + count-driven sections.
// Minimum size with all counts zero: 24 +1 +2 +2 +1 +1 +1 = 32 bytes.
static constexpr size_t kShiMinSize = 32;

int ShiParser::Parse(const uint8_t* payload, size_t len, AfiShiMessage& msg) {
    if (!payload || len < kShiMinSize) {
        AFI_LOG_ERROR("SHII payload too short: %zu < %zu", len, kShiMinSize);
        return -1;
    }

    const uint8_t* p = payload;

    // Common Interface Header (24 bytes)
    msg.interface_version.major = p[0];
    msg.interface_version.minor = p[1];
    msg.interface_version.patch = p[2];
    msg.interface_id = p[3];
    msg.num_serving_sensors = p[4];
    msg.sensor_id = p[5];
    msg.timestamp = detail::unpack_le64(p + 6);
    msg.message_counter = detail::unpack_le32(p + 14);
    msg.interface_cycle_time = detail::unpack_le32(p + 18);
    msg.interface_cycle_time_variation = p[22];
    msg.data_qualifier = static_cast<bts_data_qualifier_t>(p[23]);

    size_t off = 24;

    // Operation Modes (1 + N)
    uint8_t n_op = p[off++];
    if (n_op > BTS_SHI_MAX_OPERATION_MODES) {
        AFI_LOG_WARN("SHII operation mode count %u exceeds max %u, clamping",
                     n_op, BTS_SHI_MAX_OPERATION_MODES);
        n_op = BTS_SHI_MAX_OPERATION_MODES;
    }
    if (off + n_op + 4 > len) {
        AFI_LOG_ERROR("SHII payload truncated in operation modes");
        return -1;
    }
    msg.num_valid_operation_modes = n_op;
    std::memcpy(msg.sensor_operation_modes, p + off, n_op);
    off += n_op;

    // Defect Info (2)
    msg.sensor_defect_recognised = static_cast<bts_shi_sensor_defect_recognised_t>(p[off++]);
    msg.sensor_defect_reason = static_cast<bts_shi_sensor_defect_reason_t>(p[off++]);

    // Voltage and Temperature (2)
    msg.supply_voltage_status = static_cast<bts_shi_supply_voltage_status_t>(p[off++]);
    msg.sensor_temperature_status = static_cast<bts_shi_sensor_temperature_status_t>(p[off++]);

    // Input Signal Status (1 + 2N: types array, then statuses array)
    if (off >= len) return -1;
    uint8_t n_in = p[off++];
    if (n_in > BTS_SHI_MAX_INPUT_SIGNALS) {
        AFI_LOG_WARN("SHII input signal count %u exceeds max %u, clamping",
                     n_in, BTS_SHI_MAX_INPUT_SIGNALS);
        n_in = BTS_SHI_MAX_INPUT_SIGNALS;
    }
    if (off + 2u * n_in + 1 > len) {
        AFI_LOG_ERROR("SHII payload truncated in input signals");
        return -1;
    }
    msg.num_valid_input_signal_statuses = n_in;
    std::memcpy(msg.sensor_input_signal_types, p + off, n_in);
    off += n_in;
    std::memcpy(msg.sensor_input_signal_statuses, p + off, n_in);
    off += n_in;

    // Time Sync (1; offset value field removed in v3.0)
    msg.sensor_time_sync = static_cast<bts_shi_sensor_time_sync_t>(p[off++]);

    // Sensor Calibration (1 + 3N: component, status, state parallel arrays)
    if (off >= len) return -1;
    uint8_t n_cal = p[off++];
    if (n_cal > BTS_SHI_MAX_CALIBRATION_COMPONENTS) {
        AFI_LOG_WARN("SHII calibration count %u exceeds max %u, clamping",
                     n_cal, BTS_SHI_MAX_CALIBRATION_COMPONENTS);
        n_cal = BTS_SHI_MAX_CALIBRATION_COMPONENTS;
    }
    if (off + 3u * n_cal > len) {
        AFI_LOG_ERROR("SHII payload truncated in calibration section");
        return -1;
    }
    msg.num_valid_calibration_components = n_cal;
    std::memcpy(msg.sensor_calibration_components, p + off, n_cal);
    off += n_cal;
    std::memcpy(msg.sensor_calibration_statuses, p + off, n_cal);
    off += n_cal;
    std::memcpy(msg.sensor_calibration_states, p + off, n_cal);

    return 0;
}

} // namespace afi
