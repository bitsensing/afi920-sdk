// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/rdi_parser.h"
#include "util/endian_util.h"
#include "util/logger.h"
#include <cstring>
#include <algorithm>

namespace afi {

static constexpr size_t kRdiHeaderSize    = BTS_RDI_HEADER_SIZE;     // 36
static constexpr size_t kRdiDetectionSize = BTS_RDI_DETECTION_SIZE;  // 51
// The single-message format of interface v1.0.x and v1.1.0 defines up to 8192
// detections; v1.2.0 sends at most 2048 in header/chunk messages.
static constexpr size_t kRdiMaxDetections = BTS_RDI_LEGACY_MAX_DETECTIONS;

void RdiParser::ParseDetectionHeader(const uint8_t* p, bts_rdi_message_t& msg) {
    // --- Common Interface Header (24 bytes) ---
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

    // --- Ambiguity Domain (8 bytes: RadialVelocity begin/end) ---
    msg.ambiguity_domain.radial_velocity_begin = detail::unpack_le_f32(p + 24);
    msg.ambiguity_domain.radial_velocity_end   = detail::unpack_le_f32(p + 28);

    // --- Detection List Info (4 bytes) ---
    msg.recognised_detections_capability = detail::unpack_le16(p + 32);
    msg.num_detections = detail::unpack_le16(p + 34);
}

int RdiParser::Parse(const uint8_t* payload, size_t len, AfiRdiFrame& frame) {
    frame.message = {};
    frame.detections_storage.clear();
    frame.chunk_count = 0;
    frame.chunks_received = 0;
    frame.announced_detections = 0;
    if (!payload || len < kRdiHeaderSize) {
        AFI_LOG_ERROR("RDI payload too short: %zu < %zu", len, kRdiHeaderSize);
        return -1;
    }

    auto& msg = frame.message;
    ParseDetectionHeader(payload, msg);

    // Validate detection count
    if (msg.num_detections > kRdiMaxDetections) {
        AFI_LOG_ERROR("RDI num_detections %d exceeds max %zu",
                     msg.num_detections, kRdiMaxDetections);
        return -1;
    }

    // The E2E length and list count must describe exactly one complete payload.
    size_t expected = kRdiHeaderSize + static_cast<size_t>(msg.num_detections) * kRdiDetectionSize;
    if (len != expected) {
        AFI_LOG_ERROR("RDI payload size mismatch: %zu != %zu (for %d detections)",
                      len, expected, msg.num_detections);
        return -1;
    }

    // --- Parse Detections ---
    frame.detections_storage.resize(msg.num_detections);
    for (uint16_t i = 0; i < msg.num_detections; i++) {
        const uint8_t* det_ptr = payload + kRdiHeaderSize + i * kRdiDetectionSize;
        ParseDetection(det_ptr, frame.detections_storage[i]);
    }
    frame.announced_detections = msg.num_detections;

    // Point message.detections to our storage
    msg.detections = frame.detections_storage.empty()
        ? nullptr : frame.detections_storage.data();

    return 0;
}

int RdiParser::ParseHeaderMessage(const uint8_t* body, size_t len,
                                  bts_rdi_message_t& msg, RdiListLayout& layout) {
    if (!body || len != BTS_RDI_HEADER_MSG_SIZE) {
        AFI_LOG_ERROR("RDI header message size %zu != %u", len,
                      static_cast<unsigned>(BTS_RDI_HEADER_MSG_SIZE));
        return -1;
    }
    msg = {};
    ParseDetectionHeader(body, msg);
    layout.entities_per_chunk = detail::unpack_le16(body + kRdiHeaderSize);
    layout.chunk_count = detail::unpack_le16(body + kRdiHeaderSize + 2);
    return 0;
}

void RdiParser::ParseDetection(const uint8_t* d, bts_rdi_detection_t& det) {
    // Status (9 bytes)
    det.existence_probability = d[0];
    det.detection_id          = detail::unpack_le16(d + 1);
    det.object_id_reference   = detail::unpack_le16(d + 3);
    det.timestamp_difference  = detail::unpack_le_f32(d + 5);

    // Information (10 bytes)
    det.radar_cross_section   = detail::unpack_le_f32(d + 9);
    det.signal_to_noise_ratio = detail::unpack_le_f32(d + 13);
    det.ambiguity_grouping_id = detail::unpack_le16(d + 17);

    // Position (24 bytes)
    det.position_radial_distance       = detail::unpack_le_f32(d + 19);
    det.position_azimuth               = detail::unpack_le_f32(d + 23);
    det.position_elevation             = detail::unpack_le_f32(d + 27);
    det.position_radial_distance_error = detail::unpack_le_f32(d + 31);
    det.position_azimuth_error         = detail::unpack_le_f32(d + 35);
    det.position_elevation_error       = detail::unpack_le_f32(d + 39);

    // Dynamics (8 bytes)
    det.relative_velocity_radial       = detail::unpack_le_f32(d + 43);
    det.relative_velocity_radial_error = detail::unpack_le_f32(d + 47);
}

} // namespace afi
