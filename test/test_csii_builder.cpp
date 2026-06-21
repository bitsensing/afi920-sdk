/**
 * @file test_csii_builder.cpp
 * @brief Unit test: CSII frame builder (SOME/IP + E2E + 79B ISO payload)
 */
#include "protocol/csii_builder.h"
#include "protocol/e2e.h"
#include "protocol/someip.h"
#include "test_common.h"
#include <cstring>

using namespace afi;

static uint32_t unpack_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}
static uint32_t unpack_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
static float unpack_le_f32(const uint8_t* p) {
    uint32_t u = unpack_le32(p); float f; std::memcpy(&f, &u, 4); return f;
}

TEST(frame_size_is_115) {
    CHECK(kCsiiFrameSize == 115u);
    CHECK(BTS_CSII_SIZE == 79u);
    PASS("frame_size_is_115");
}

TEST(someip_header_fields) {
    AfiVehicleInput input;
    uint8_t frame[kCsiiFrameSize];
    CHECK(csii_build_frame(input, 7, 3, 1000, frame) == (int)kCsiiFrameSize);

    CHECK(unpack_be32(frame) == 0x60008001u);             // MessageID
    CHECK(unpack_be32(frame + 4) == 8u + 20u + 79u);      // Length = 107
    CHECK(frame[12] == 0x01);                              // protocol version
    CHECK(frame[13] == 0x01);                              // interface version
    CHECK(frame[14] == 0x02);                              // NOTIFICATION
    CHECK(frame[15] == 0x00);                              // return OK
    PASS("someip_header_fields");
}

TEST(e2e_header_valid_crc) {
    AfiVehicleInput input;
    input.velocity_x_mps = 16.7f;
    uint8_t frame[kCsiiFrameSize];
    csii_build_frame(input, 99, 1, 0, frame);

    // E2E header at offset 16, protecting the 79B ISO payload
    E2eHeader e2e;
    CHECK(e2e_validate(frame + 16, 20 + 79, e2e) == E2eStatus::kOk);
    CHECK(e2e.counter == 99);
    CHECK(e2e.length == 79);
    CHECK(e2e.data_id == 0x60008001u);
    PASS("e2e_header_valid_crc");
}

TEST(iso_payload_fields) {
    AfiVehicleInput input;
    input.sensor_id = 4;
    input.somc = BTS_CSII_SOMC_START_CALIBRATION;
    input.global_timestamp_utc = 1718000000.5;
    input.coord_system = BTS_CSII_VCS_ROAD_LEVEL;
    input.velocity_x_mps = 16.7f;
    input.rotation_rate_yaw_rps = 0.05f;
    input.wheel_rrp_rps = 33.3f;
    input.steering_angle_rad = -0.2f;
    input.gear_position = BTS_CSII_GP_REVERSE;

    uint8_t frame[kCsiiFrameSize];
    csii_build_frame(input, 11, 2, 987654321ull, frame);

    const uint8_t* p = frame + 16 + 20;  // ISO payload
    CHECK(p[0] == 1 && p[1] == 0 && p[2] == 0);   // version 1.0.0
    CHECK(p[3] == 0x0E);                           // IID_COMMON_SENSOR_INPUT
    CHECK(p[4] == 1);                              // serving sensors
    CHECK(p[5] == 4);                              // sensor id
    uint64_t ts = 0;
    for (int i = 0; i < 8; i++) ts |= static_cast<uint64_t>(p[6 + i]) << (8 * i);
    CHECK(ts == 987654321ull);
    CHECK(unpack_le32(p + 14) == 11u);             // message counter
    CHECK(unpack_le32(p + 18) == 100000000u);      // cycle time 100ms
    CHECK(p[24] == BTS_CSII_SOMC_START_CALIBRATION);
    double utc; uint64_t utc_bits = 0;
    for (int i = 0; i < 8; i++) utc_bits |= static_cast<uint64_t>(p[25 + i]) << (8 * i);
    std::memcpy(&utc, &utc_bits, 8);
    CHECK(utc == 1718000000.5);
    CHECK(p[33] == BTS_CSII_VCS_ROAD_LEVEL);
    CHECK(unpack_le_f32(p + 34) == 16.7f);         // velocity x
    CHECK(unpack_le_f32(p + 46) == 0.05f);         // yaw rate
    CHECK(unpack_le_f32(p + 70) == 33.3f);         // wheel rr
    CHECK(unpack_le_f32(p + 74) == -0.2f);         // steering
    CHECK(p[78] == BTS_CSII_GP_REVERSE);
    PASS("iso_payload_fields");
}

TEST(null_output_rejected) {
    AfiVehicleInput input;
    CHECK(csii_build_frame(input, 0, 0, 0, nullptr) < 0);
    PASS("null_output_rejected");
}

TEST_MAIN("CSII Builder Tests")
