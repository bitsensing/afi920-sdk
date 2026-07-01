// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/csii_builder.h"
#include "protocol/someip.h"
#include "bts_iso23150_e2e.h"
#include <cstring>

namespace afi {

static constexpr size_t kE2eOffset = kSomeipHeaderSize;            // 16
static constexpr size_t kIsoOffset = kE2eOffset + BTS_E2E_HEADER_SIZE;  // 36

static inline void pack_le_f64(uint8_t* p, double d) {
    uint64_t u;
    std::memcpy(&u, &d, sizeof(u));
    bts_pack_le64(p, u);
}

static inline void pack_le_f32(uint8_t* p, float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    bts_pack_le32(p, u);
}

int csii_build_frame(const AfiVehicleInput& input,
                     uint32_t counter,
                     uint16_t session_id,
                     uint64_t timestamp_ns,
                     uint8_t* out) {
    if (!out) return -1;

    // ─── ISO 23150 CSII payload (79B, little-endian) ───
    uint8_t* p = out + kIsoOffset;

    // Common Interface Header (24B)
    p[0] = 1;  // interface_version major
    p[1] = 0;  //                   minor
    p[2] = 0;  //                   patch
    p[3] = BTS_IID_COMMON_SENSOR_INPUT;
    p[4] = BTS_CSII_NUM_VALID_SERVING_SENSORS;
    p[5] = input.sensor_id;
    bts_pack_le64(p + 6, timestamp_ns);
    bts_pack_le32(p + 14, counter);
    bts_pack_le32(p + 18, 100000000u);  // interface_cycle_time: 100 ms in ns
    p[22] = 0;  // interface_cycle_time_variation
    p[23] = 0;  // data_qualifier: DQ_Normal

    // Sensor Operation Command (1B)
    p[24] = static_cast<uint8_t>(input.somc);

    // Environmental Info: Time (8B)
    pack_le_f64(p + 25, input.global_timestamp_utc);

    // Vehicle State (46B)
    p[33] = static_cast<uint8_t>(input.coord_system);
    pack_le_f32(p + 34, input.velocity_x_mps);
    pack_le_f32(p + 38, input.velocity_y_mps);
    pack_le_f32(p + 42, input.velocity_z_mps);
    pack_le_f32(p + 46, input.rotation_rate_yaw_rps);
    pack_le_f32(p + 50, input.rotation_rate_pitch_rps);
    pack_le_f32(p + 54, input.rotation_rate_roll_rps);
    pack_le_f32(p + 58, input.wheel_flp_rps);
    pack_le_f32(p + 62, input.wheel_frp_rps);
    pack_le_f32(p + 66, input.wheel_rlp_rps);
    pack_le_f32(p + 70, input.wheel_rrp_rps);
    pack_le_f32(p + 74, input.steering_angle_rad);
    p[78] = static_cast<uint8_t>(input.gear_position);

    // ─── E2E Profile 7 header (20B, big-endian) + CRC-64/XZ ───
    bts_e2e_build_header(out + kE2eOffset, BTS_E2E_DATA_ID_CSII, counter,
                         BTS_CSII_SIZE);
    bts_e2e_apply_crc_contig(out + kE2eOffset,
                             BTS_E2E_HEADER_SIZE + BTS_CSII_SIZE);

    // ─── SOME/IP header (16B, big-endian) ───
    // Length covers request-id..payload: 8 + E2E(20) + ISO(79)
    uint32_t message_id = (static_cast<uint32_t>(kSomeipServiceId) << 16) | kEventCsii;
    uint32_t length = 8 + BTS_E2E_HEADER_SIZE + BTS_CSII_SIZE;
    bts_pack_be32(out + 0, message_id);
    bts_pack_be32(out + 4, length);
    bts_pack_be16(out + 8, BTS_SOMEIP_CLIENT_ID);
    bts_pack_be16(out + 10, session_id);
    out[12] = BTS_SOMEIP_PROTOCOL_VERSION;
    out[13] = BTS_SOMEIP_INTERFACE_VERSION;
    out[14] = BTS_SOMEIP_MSGTYPE_NOTIFICATION;
    out[15] = BTS_SOMEIP_RETURN_OK;

    return static_cast<int>(kCsiiFrameSize);
}

} // namespace afi
