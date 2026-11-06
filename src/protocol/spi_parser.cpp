// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/spi_parser.h"
#include "util/endian_util.h"
#include "util/logger.h"
#include <cstring>

namespace afi {

// 24(header) + 1(vcs) + 32(pose) + three mandatory count fields.
static constexpr size_t kSpiMinSize = 60;

int SpiParser::Parse(const uint8_t* payload, size_t len, AfiSpiMessage& msg) {
    msg = {};
    if (!payload || len < kSpiMinSize) {
        AFI_LOG_ERROR("SPI payload too short: %zu < %zu", len, kSpiMinSize);
        return -1;
    }

    const uint8_t* p = payload;
    size_t offset = 0;

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
    offset = 24;

    // Vehicle Coordinate System (1 byte)
    msg.vehicle_coordinate_system = static_cast<bts_spi_vehicle_coordinate_system_t>(p[offset]);
    offset += 1;

    // Sensor Pose (32 bytes: origin x/y/z, yaw/pitch/roll, yaw and pitch errors)
    msg.sensor_pose.origin_point_x        = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.origin_point_y        = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.origin_point_z        = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.orientation_yaw       = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.orientation_pitch     = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.orientation_roll      = detail::unpack_le_f32(p + offset);      offset += 4;
    msg.sensor_pose.orientation_error_yaw   = detail::unpack_le_f32(p + offset);    offset += 4;
    msg.sensor_pose.orientation_error_pitch = detail::unpack_le_f32(p + offset);    offset += 4;

    // FOV Segments (variable, count field is mandatory)
    const uint8_t n_fov = p[offset++];
    if (n_fov > BTS_SPI_MAX_FOV_SEGMENTS) {
        AFI_LOG_ERROR("SPI FOV segment count %u exceeds max %u",
                      static_cast<unsigned>(n_fov),
                      static_cast<unsigned>(BTS_SPI_MAX_FOV_SEGMENTS));
        return -1;
    }
    if (static_cast<size_t>(n_fov) > (len - offset) / BTS_SPI_FOV_SEGMENT_SIZE) {
        AFI_LOG_ERROR("SPI payload truncated in FOV segments");
        return -1;
    }
    msg.num_fov_segments = n_fov;
    for (uint8_t i = 0; i < n_fov; i++) {
        auto& seg = msg.fov_segments[i];
        seg.azimuth_begin    = detail::unpack_le_f32(p + offset); offset += 4;
        seg.azimuth_end      = detail::unpack_le_f32(p + offset); offset += 4;
        seg.elevation_begin  = detail::unpack_le_f32(p + offset); offset += 4;
        seg.elevation_end    = detail::unpack_le_f32(p + offset); offset += 4;
        seg.blockage_status  = static_cast<bts_spi_blockage_status_t>(p[offset]); offset += 1;
    }

    // Recognisable Object Types (variable, count field is mandatory)
    if (offset >= len) {
        AFI_LOG_ERROR("SPI payload missing recognisable-object count");
        return -1;
    }
    const uint8_t n_obj = p[offset++];
    if (n_obj > BTS_SPI_MAX_OBJECT_TYPES) {
        AFI_LOG_ERROR("SPI object type count %u exceeds max %u",
                      static_cast<unsigned>(n_obj),
                      static_cast<unsigned>(BTS_SPI_MAX_OBJECT_TYPES));
        return -1;
    }
    if (static_cast<size_t>(n_obj) > (len - offset) / BTS_SPI_OBJECT_TYPE_SIZE) {
        AFI_LOG_ERROR("SPI payload truncated in recognisable object types");
        return -1;
    }
    msg.num_recognisable_object_types = n_obj;
    for (uint8_t i = 0; i < n_obj; i++) {
        auto& obj = msg.recognisable_object_types[i];
        obj.object_type         = static_cast<bts_spi_recognised_object_type_t>(p[offset]); offset += 1;
        obj.detection_range_begin = detail::unpack_le_f32(p + offset); offset += 4;
        obj.detection_range_end   = detail::unpack_le_f32(p + offset); offset += 4;
    }

    // Reference Target Types (variable, count field is mandatory)
    if (offset >= len) {
        AFI_LOG_ERROR("SPI payload missing reference-target count");
        return -1;
    }
    const uint8_t n_ref = p[offset++];
    if (n_ref > BTS_SPI_MAX_REF_TARGETS) {
        AFI_LOG_ERROR("SPI reference target count %u exceeds max %u",
                      static_cast<unsigned>(n_ref),
                      static_cast<unsigned>(BTS_SPI_MAX_REF_TARGETS));
        return -1;
    }
    if (static_cast<size_t>(n_ref) > (len - offset) / BTS_SPI_REF_TARGET_SIZE) {
        AFI_LOG_ERROR("SPI payload truncated in reference target types");
        return -1;
    }
    msg.num_reference_target_types = n_ref;
    for (uint8_t i = 0; i < n_ref; i++) {
        auto& ref = msg.reference_target_types[i];
        ref.radar_cross_section     = detail::unpack_le_f32(p + offset); offset += 4;
        ref.detection_range_begin   = detail::unpack_le_f32(p + offset); offset += 4;
        ref.detection_range_end     = detail::unpack_le_f32(p + offset); offset += 4;
        ref.signal_to_noise_ratio   = detail::unpack_le_f32(p + offset); offset += 4;
    }

    if (offset != len) {
        AFI_LOG_ERROR("SPI payload length mismatch: parsed=%zu actual=%zu", offset, len);
        return -1;
    }

    return 0;
}

} // namespace afi
