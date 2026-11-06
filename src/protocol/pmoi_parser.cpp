// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/pmoi_parser.h"
#include "util/endian_util.h"
#include "util/logger.h"

namespace afi {

static constexpr size_t kPmoiHeaderSize = BTS_PMOI_HEADER_SIZE; // 30
static constexpr size_t kPmoiObjectSize = BTS_PMOI_OBJECT_SIZE; // 116

int PmoiParser::Parse(const uint8_t* payload, size_t len, AfiPmoiFrame& frame) {
    frame.message = {};
    frame.objects_storage.clear();
    if (!payload || len < kPmoiHeaderSize) {
        AFI_LOG_ERROR("PMOI payload too short: %zu < %zu", len, kPmoiHeaderSize);
        return -1;
    }

    auto& msg = frame.message;
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

    // PMOI List Header (6 bytes)
    msg.motion_type = p[24];
    msg.recognised_pmo_capability = detail::unpack_le16(p + 25);
    msg.recognised_pmo_status = p[27];
    msg.num_pmo = detail::unpack_le16(p + 28);

    if (msg.num_pmo > BTS_PMOI_MAX_OBJECTS) {
        AFI_LOG_ERROR("PMOI num_pmo %u exceeds max %u",
                      static_cast<unsigned>(msg.num_pmo),
                      static_cast<unsigned>(BTS_PMOI_MAX_OBJECTS));
        return -1;
    }

    const size_t expected = kPmoiHeaderSize +
        static_cast<size_t>(msg.num_pmo) * kPmoiObjectSize;
    if (len != expected) {
        AFI_LOG_ERROR("PMOI payload size mismatch: %zu != %zu", len, expected);
        return -1;
    }

    frame.objects_storage.resize(msg.num_pmo);
    for (uint16_t i = 0; i < msg.num_pmo; ++i) {
        const uint8_t* object = payload + kPmoiHeaderSize +
            static_cast<size_t>(i) * kPmoiObjectSize;
        if (ParseObject(object, frame.objects_storage[i]) < 0) return -1;
    }
    msg.objects = frame.objects_storage.empty() ? nullptr : frame.objects_storage.data();
    return 0;
}

int PmoiParser::ParseObject(const uint8_t* d, bts_pmoi_object_t& object) {
    object.existence_probability = detail::unpack_le_f32(d + 0);
    object.object_id = d[4];
    object.object_grouping_id = d[5];
    object.age = detail::unpack_le_f32(d + 6);
    object.measurement_status = d[10];

    object.num_classifications = d[11];
    if (object.num_classifications > BTS_PMOI_MAX_CLASSIFICATIONS) {
        AFI_LOG_ERROR("PMOI classification count %u exceeds max %u",
                      static_cast<unsigned>(object.num_classifications),
                      static_cast<unsigned>(BTS_PMOI_MAX_CLASSIFICATIONS));
        return -1;
    }
    for (size_t i = 0; i < BTS_PMOI_MAX_CLASSIFICATIONS; ++i) {
        object.classifications[i].type = d[12 + i * 2];
        object.classifications[i].confidence = d[13 + i * 2];
    }

    object.reference_point = d[22];
    object.position_x = detail::unpack_le_f32(d + 23);
    object.position_y = detail::unpack_le_f32(d + 27);
    object.position_z = detail::unpack_le_f32(d + 31);
    object.position_error_x = detail::unpack_le_f32(d + 35);
    object.position_error_y = detail::unpack_le_f32(d + 39);
    object.position_error_z = detail::unpack_le_f32(d + 43);
    object.orientation_yaw = detail::unpack_le_f32(d + 47);
    object.bounding_box_length = detail::unpack_le_f32(d + 51);
    object.bounding_box_width = detail::unpack_le_f32(d + 55);
    object.bounding_box_height = detail::unpack_le_f32(d + 59);
    object.velocity_x = detail::unpack_le_f32(d + 63);
    object.velocity_y = detail::unpack_le_f32(d + 67);
    object.velocity_z = detail::unpack_le_f32(d + 71);
    object.velocity_error_x = detail::unpack_le_f32(d + 75);
    object.velocity_error_y = detail::unpack_le_f32(d + 79);
    object.velocity_error_z = detail::unpack_le_f32(d + 83);
    object.acceleration_x = detail::unpack_le_f32(d + 87);
    object.acceleration_y = detail::unpack_le_f32(d + 91);
    object.acceleration_z = detail::unpack_le_f32(d + 95);
    object.acceleration_error_x = detail::unpack_le_f32(d + 99);
    object.acceleration_error_y = detail::unpack_le_f32(d + 103);
    object.acceleration_error_z = detail::unpack_le_f32(d + 107);
    object.movement_status = d[111];
    object.entity_radar_cross_section = detail::unpack_le_f32(d + 112);
    return 0;
}

} // namespace afi
