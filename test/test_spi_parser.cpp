/**
 * @file test_spi_parser.cpp
 * @brief Unit test: SPI v3.0 payload parsing (32B pose, error fields removed)
 */
#include "protocol/spi_parser.h"
#include "test_common.h"
#include <cstring>
#include <vector>

using namespace afi;

static void pack_le32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static void pack_le64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (v >> (8 * i)) & 0xFF;
}
static void pack_f32(uint8_t* p, float f) {
    uint32_t u; std::memcpy(&u, &f, 4); pack_le32(p, u);
}

// CIH(24) + vcs(1) + pose(32 = 8 floats) [+ fov section]
static std::vector<uint8_t> make_spi(uint8_t n_fov) {
    std::vector<uint8_t> buf(57, 0);
    buf[0] = 1;
    buf[3] = 0x0C;                    // IID sensor performance
    buf[4] = 1; buf[5] = 2;
    pack_le64(&buf[6], 777ull);
    pack_le32(&buf[14], 123);

    buf[24] = 1;                      // vehicle coord system: ROAD_LEVEL
    const float pose[8] = {3.8f, 0.0f, 0.5f,        // origin x/y/z
                           0.01f, -0.02f, 0.03f,    // yaw/pitch/roll
                           0.001f, 0.002f};         // yaw/pitch error
    for (int i = 0; i < 8; i++) pack_f32(&buf[25 + 4 * i], pose[i]);

    if (n_fov > 0) {
        buf.push_back(n_fov);
        for (uint8_t i = 0; i < n_fov; i++) {
            uint8_t seg[17];
            pack_f32(seg + 0, -1.0f);
            pack_f32(seg + 4, 1.0f);
            pack_f32(seg + 8, -0.3f);
            pack_f32(seg + 12, 0.3f);
            seg[16] = (i == 0) ? 0 : 3;  // NO_BLOCKAGE / UNKNOWN
            buf.insert(buf.end(), seg, seg + sizeof(seg));
        }
    }
    return buf;
}

TEST(pose_layout_is_32_bytes) {
    CHECK(BTS_SPI_SENSOR_POSE_SIZE == 32u);
    PASS("pose_layout_is_32_bytes");
}

TEST(parse_pose_only) {
    auto buf = make_spi(0);
    CHECK(buf.size() == 57u);

    SpiParser parser;
    AfiSpiMessage msg{};
    CHECK(parser.Parse(buf.data(), buf.size(), msg) == 0);
    CHECK(msg.sensor_id == 2);
    CHECK(msg.message_counter == 123);
    CHECK(msg.vehicle_coordinate_system == 1);
    CHECK(msg.sensor_pose.origin_point_x == 3.8f);
    CHECK(msg.sensor_pose.origin_point_z == 0.5f);
    CHECK(msg.sensor_pose.orientation_yaw == 0.01f);
    CHECK(msg.sensor_pose.orientation_roll == 0.03f);
    CHECK(msg.sensor_pose.orientation_error_yaw == 0.001f);
    CHECK(msg.sensor_pose.orientation_error_pitch == 0.002f);
    PASS("parse_pose_only");
}

TEST(parse_fov_segments) {
    auto buf = make_spi(2);

    SpiParser parser;
    AfiSpiMessage msg{};
    CHECK(parser.Parse(buf.data(), buf.size(), msg) == 0);
    CHECK(msg.num_fov_segments == 2);
    CHECK(msg.fov_segments[0].azimuth_begin == -1.0f);
    CHECK(msg.fov_segments[0].azimuth_end == 1.0f);
    CHECK(msg.fov_segments[0].blockage_status == BTS_SPI_BS_NONE);
    CHECK(msg.fov_segments[1].blockage_status == BTS_SPI_BS_UNKNOWN);
    PASS("parse_fov_segments");
}

TEST(short_payload_rejected) {
    uint8_t buf[56] = {};
    SpiParser parser;
    AfiSpiMessage msg{};
    CHECK(parser.Parse(buf, sizeof(buf), msg) < 0);
    CHECK(parser.Parse(nullptr, 100, msg) < 0);
    PASS("short_payload_rejected");
}

TEST_MAIN("SPI Parser Tests (v3.0)")
