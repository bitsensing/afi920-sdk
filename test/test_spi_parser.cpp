/**
 * @file test_spi_parser.cpp
 * @brief Unit test: SPI payload parsing (32B pose, spi.ksy)
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

// CIH(24) + vcs(1) + pose(32 = 8 floats) + three count-driven sections.
static std::vector<uint8_t> make_spi(uint8_t n_fov,
                                     uint8_t n_obj = 0,
                                     uint8_t n_ref = 0) {
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

    buf.push_back(n_obj);
    for (uint8_t i = 0; i < n_obj; i++) {
        uint8_t obj[9];
        obj[0] = i;
        pack_f32(obj + 1, 1.0f);
        pack_f32(obj + 5, 200.0f);
        buf.insert(buf.end(), obj, obj + sizeof(obj));
    }

    buf.push_back(n_ref);
    for (uint8_t i = 0; i < n_ref; i++) {
        uint8_t ref[16];
        pack_f32(ref + 0, 10.0f);
        pack_f32(ref + 4, 2.0f);
        pack_f32(ref + 8, 150.0f);
        pack_f32(ref + 12, 5.0f);
        buf.insert(buf.end(), ref, ref + sizeof(ref));
    }
    return buf;
}

TEST(pose_layout_is_32_bytes) {
    CHECK(BTS_SPI_SENSOR_POSE_SIZE == 32u);
    PASS("pose_layout_is_32_bytes");
}

TEST(parse_pose_only) {
    auto buf = make_spi(0);
    CHECK(buf.size() == 60u);

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

TEST(parse_object_and_reference_capabilities) {
    auto buf = make_spi(0, 1, 1);

    SpiParser parser;
    AfiSpiMessage msg{};
    CHECK(parser.Parse(buf.data(), buf.size(), msg) == 0);
    CHECK(msg.num_recognisable_object_types == 1);
    CHECK(msg.recognisable_object_types[0].object_type == BTS_SPI_ROT_CAR);
    CHECK(msg.recognisable_object_types[0].detection_range_end == 200.0f);
    CHECK(msg.num_reference_target_types == 1);
    CHECK(msg.reference_target_types[0].radar_cross_section == 10.0f);
    CHECK(msg.reference_target_types[0].signal_to_noise_ratio == 5.0f);
    PASS("parse_object_and_reference_capabilities");
}

TEST(short_payload_rejected) {
    uint8_t buf[59] = {};
    SpiParser parser;
    AfiSpiMessage msg{};
    CHECK(parser.Parse(buf, sizeof(buf), msg) < 0);
    CHECK(parser.Parse(nullptr, 100, msg) < 0);
    PASS("short_payload_rejected");
}

TEST(malformed_count_and_length_rejected) {
    SpiParser parser;
    AfiSpiMessage msg{};

    auto overflow = make_spi(0);
    overflow[57] = BTS_SPI_MAX_FOV_SEGMENTS + 1;
    CHECK(parser.Parse(overflow.data(), overflow.size(), msg) < 0);

    auto truncated = make_spi(2, 1, 1);
    truncated.pop_back();
    CHECK(parser.Parse(truncated.data(), truncated.size(), msg) < 0);

    auto trailing = make_spi(0);
    trailing.push_back(0xAA);
    CHECK(parser.Parse(trailing.data(), trailing.size(), msg) < 0);
    PASS("malformed_count_and_length_rejected");
}

TEST_MAIN("SPI Parser Tests")
