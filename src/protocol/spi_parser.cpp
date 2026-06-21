// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/spi_parser.h"
#include "util/endian_util.h"
#include "util/logger.h"
#include <cstring>
#include <algorithm>

namespace afi {

static constexpr size_t kSpiMinSize = 57; // 24(header) + 1(vcs) + 32(pose)

int SpiParser::Parse(const uint8_t* payload, size_t len, AfiSpiMessage& msg) {
    if (!payload || len < kSpiMinSize) {
        AFI_LOG_ERROR("SPI payload too short: %zu < %zu", len, kSpiMinSize);
        return -1;
    }

    const uint8_t* p = payload;
    size_t offset = 0;

    // Common Interface Header (23 bytes)
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
    offset = 24;

    // Vehicle Coordinate System (1 byte)
    msg.vehicle_coordinate_system = static_cast<bts_spi_vehicle_coordinate_system_t>(p[offset]);
    offset += 1;

    // Sensor Pose (32 bytes; origin errors and roll error removed in v3.0)
    msg.sensor_pose.origin_point_x        = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.origin_point_y        = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.origin_point_z        = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.orientation_yaw       = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.orientation_pitch     = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.orientation_roll      = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.orientation_error_yaw   = detail::unpack_le_f32(p + offset);    offset += 4;
    msg.sensor_pose.orientation_error_pitch = detail::unpack_le_f32(p + offset);    offset += 4;

    // FOV Segments (variable)
    if (offset >= len) return 0;
    msg.num_fov_segments = p[offset]; offset += 1;
    uint8_t n_fov = std::min(msg.num_fov_segments, static_cast<uint8_t>(BTS_SPI_MAX_FOV_SEGMENTS));
    for (uint8_t i = 0; i < n_fov && (offset + 17) <= len; i++) {
        auto& seg = msg.fov_segments[i];
        seg.azimuth_begin    = detail::unpack_le_f32(p + offset); offset += 4;
        seg.azimuth_end      = detail::unpack_le_f32(p + offset); offset += 4;
        seg.elevation_begin  = detail::unpack_le_f32(p + offset); offset += 4;
        seg.elevation_end    = detail::unpack_le_f32(p + offset); offset += 4;
        seg.blockage_status  = static_cast<bts_spi_blockage_status_t>(p[offset]); offset += 1;
    }

    // Recognisable Object Types (variable)
    if (offset >= len) return 0;
    msg.num_recognisable_object_types = p[offset]; offset += 1;
    uint8_t n_obj = std::min(msg.num_recognisable_object_types,
                             static_cast<uint8_t>(BTS_SPI_MAX_OBJECT_TYPES));
    for (uint8_t i = 0; i < n_obj && (offset + 9) <= len; i++) {
        auto& obj = msg.recognisable_object_types[i];
        obj.object_type         = static_cast<bts_spi_recognised_object_type_t>(p[offset]); offset += 1;
        obj.detection_range_begin = detail::unpack_le_f32(p + offset); offset += 4;
        obj.detection_range_end   = detail::unpack_le_f32(p + offset); offset += 4;
    }

    // Reference Target Types (variable)
    if (offset >= len) return 0;
    msg.num_reference_target_types = p[offset]; offset += 1;
    uint8_t n_ref = std::min(msg.num_reference_target_types,
                             static_cast<uint8_t>(BTS_SPI_MAX_REF_TARGETS));
    for (uint8_t i = 0; i < n_ref && (offset + 16) <= len; i++) {
        auto& ref = msg.reference_target_types[i];
        ref.radar_cross_section     = detail::unpack_le_f32(p + offset); offset += 4;
        ref.detection_range_begin   = detail::unpack_le_f32(p + offset); offset += 4;
        ref.detection_range_end     = detail::unpack_le_f32(p + offset); offset += 4;
        ref.signal_to_noise_ratio   = detail::unpack_le_f32(p + offset); offset += 4;
    }

    return 0;
}

} // namespace afi
