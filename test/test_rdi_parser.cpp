/**
 * @file test_rdi_parser.cpp
 * @brief Unit test: RDI payload parsing (36B header + 51B detections, rdi.ksy)
 */
#include "protocol/rdi_parser.h"
#include "test_common.h"
#include <cstring>
#include <vector>

using namespace afi;

static void pack_le16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void pack_le32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static void pack_le64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (v >> (8 * i)) & 0xFF;
}
static void pack_f32(uint8_t* p, float f) {
    uint32_t u; std::memcpy(&u, &f, 4); pack_le32(p, u);
}

// Build a synthetic RDI payload with `n` detections
static std::vector<uint8_t> make_rdi(uint16_t n) {
    std::vector<uint8_t> buf(BTS_RDI_HEADER_SIZE + n * BTS_RDI_DETECTION_SIZE, 0);
    buf[0] = 1; buf[1] = 0; buf[2] = 0;          // version
    buf[3] = 0x08;                                // IID radar detection
    buf[4] = 1; buf[5] = 3;                       // serving sensors, sensor id
    pack_le64(&buf[6], 1234567890ull);            // timestamp
    pack_le32(&buf[14], 42);                      // message counter
    pack_le32(&buf[18], 100000000);               // cycle time
    buf[22] = 5;                                  // cycle variation
    buf[23] = 0;                                  // data qualifier
    pack_f32(&buf[24], -50.0f);                   // radial velocity begin
    pack_f32(&buf[28],  50.0f);                   // radial velocity end
    pack_le16(&buf[32], 8192);                    // capability
    pack_le16(&buf[34], n);                       // num detections

    for (uint16_t i = 0; i < n; i++) {
        uint8_t* d = buf.data() + BTS_RDI_HEADER_SIZE + i * BTS_RDI_DETECTION_SIZE;
        d[0] = 90;                                // existence probability
        pack_le16(d + 1, static_cast<uint16_t>(100 + i));  // detection id
        pack_le16(d + 3, 0xFFFF);                 // object id reference
        pack_f32(d + 5, 0.001f);                  // timestamp difference
        pack_f32(d + 9, 10.5f);                   // rcs
        pack_f32(d + 13, 20.0f);                  // snr
        pack_le16(d + 17, static_cast<uint16_t>(i)); // ambiguity grouping id
        pack_f32(d + 19, 12.5f + i);              // radial distance
        pack_f32(d + 23, 0.1f);                   // azimuth
        pack_f32(d + 27, -0.05f);                 // elevation
        pack_f32(d + 31, 0.01f);                  // distance error
        pack_f32(d + 35, 0.002f);                 // azimuth error
        pack_f32(d + 39, 0.003f);                 // elevation error
        pack_f32(d + 43, -3.5f);                  // radial velocity
        pack_f32(d + 47, 0.05f);                  // velocity error
    }
    return buf;
}

TEST(layout_constants) {
    CHECK(BTS_RDI_HEADER_SIZE == 36u);
    CHECK(BTS_RDI_DETECTION_SIZE == 51u);
    CHECK(BTS_RDI_MAX_DETECTIONS == 8192u);
    PASS("layout_constants");
}

TEST(parse_empty_frame) {
    auto buf = make_rdi(0);
    CHECK(buf.size() == 36u);

    RdiParser parser;
    AfiRdiFrame frame{};
    CHECK(parser.Parse(buf.data(), buf.size(), frame) == 0);
    CHECK(frame.message.num_detections == 0);
    CHECK(frame.message.message_counter == 42);
    CHECK(frame.message.sensor_id == 3);
    CHECK(frame.message.recognised_detections_capability == 8192);
    CHECK(frame.message.detections == nullptr);
    CHECK(frame.message.ambiguity_domain.radial_velocity_begin == -50.0f);
    CHECK(frame.message.ambiguity_domain.radial_velocity_end == 50.0f);
    PASS("parse_empty_frame");
}

TEST(parse_detections) {
    auto buf = make_rdi(3);
    CHECK(buf.size() == 36u + 3 * 51u);

    RdiParser parser;
    AfiRdiFrame frame{};
    CHECK(parser.Parse(buf.data(), buf.size(), frame) == 0);
    CHECK(frame.message.num_detections == 3);

    const auto& d0 = frame.message.detections[0];
    CHECK(d0.existence_probability == 90);
    CHECK(d0.detection_id == 100);
    CHECK(d0.object_id_reference == 0xFFFF);
    CHECK(d0.radar_cross_section == 10.5f);
    CHECK(d0.signal_to_noise_ratio == 20.0f);
    CHECK(d0.position_radial_distance == 12.5f);
    CHECK(d0.relative_velocity_radial == -3.5f);

    const auto& d2 = frame.message.detections[2];
    CHECK(d2.detection_id == 102);
    CHECK(d2.ambiguity_grouping_id == 2);
    CHECK(d2.position_radial_distance == 14.5f);
    PASS("parse_detections");
}

TEST(data_qualifier_1_and_6_are_delivered) {
    // System Spec 8.3: the radar sends DQ 0, 1, 6. The parser passes the
    // value through; the consumer decides how to use DQ 1 and DQ 6 frames.
    for (uint8_t dq : {uint8_t{1}, uint8_t{6}}) {
        auto buf = make_rdi(1);
        buf[23] = dq;
        RdiParser parser;
        AfiRdiFrame frame;
        CHECK(parser.Parse(buf.data(), buf.size(), frame) == 0);
        CHECK(static_cast<uint8_t>(frame.message.data_qualifier) == dq);
        CHECK(frame.message.num_detections == 1);
    }
    PASS("data_qualifier_1_and_6_are_delivered");
}

TEST(parse_maximum_detection_count) {
    auto buf = make_rdi(BTS_RDI_MAX_DETECTIONS);
    RdiParser parser;
    AfiRdiFrame frame{};
    CHECK(buf.size() == BTS_RDI_MAX_PAYLOAD_SIZE);
    CHECK(parser.Parse(buf.data(), buf.size(), frame) == 0);
    CHECK(frame.message.num_detections == BTS_RDI_MAX_DETECTIONS);
    CHECK(frame.message.detections[0].detection_id == 100);
    CHECK(frame.message.detections[BTS_RDI_MAX_DETECTIONS - 1].detection_id ==
          static_cast<uint16_t>(100 + BTS_RDI_MAX_DETECTIONS - 1));
    PASS("parse_maximum_detection_count");
}

TEST(owning_frame_copy_and_move) {
    auto buf = make_rdi(2);
    RdiParser parser;
    AfiRdiFrame original{};
    CHECK(parser.Parse(buf.data(), buf.size(), original) == 0);

    AfiRdiFrame copied = original;
    CHECK(copied.message.detections == copied.detections_storage.data());
    CHECK(copied.message.detections != original.message.detections);
    CHECK(copied.message.detections[1].detection_id == 101);

    AfiRdiFrame moved = std::move(copied);
    CHECK(moved.message.detections == moved.detections_storage.data());
    CHECK(moved.message.detections[1].detection_id == 101);
    CHECK(copied.message.detections == nullptr);
    PASS("owning_frame_copy_and_move");
}

TEST(legacy_member_order_construction) {
    bts_rdi_message_t message{};
    message.num_detections = 1;
    std::vector<bts_rdi_detection_t> detections(1);
    detections[0].detection_id = 321;

    AfiRdiFrame frame{message, 123, 7, detections};
    CHECK(frame.recv_timestamp_ns == 123);
    CHECK(frame.frame_seq == 7);
    CHECK(frame.message.detections == frame.detections_storage.data());
    CHECK(frame.message.detections[0].detection_id == 321);
    PASS("legacy_member_order_construction");
}

TEST(truncated_payload_rejected) {
    auto buf = make_rdi(4);
    // Truncate: only 2 full detections remain
    buf.resize(BTS_RDI_HEADER_SIZE + 2 * BTS_RDI_DETECTION_SIZE + 10);

    RdiParser parser;
    AfiRdiFrame frame{};
    CHECK(parser.Parse(buf.data(), buf.size(), frame) < 0);
    PASS("truncated_payload_rejected");
}

TEST(short_header_rejected) {
    uint8_t buf[35] = {};
    RdiParser parser;
    AfiRdiFrame frame{};
    CHECK(parser.Parse(buf, sizeof(buf), frame) < 0);
    CHECK(parser.Parse(nullptr, 100, frame) < 0);
    PASS("short_header_rejected");
}

TEST(count_overflow_rejected) {
    auto buf = make_rdi(1);
    pack_le16(&buf[34], 9000);  // num_detections > max 8192

    RdiParser parser;
    AfiRdiFrame frame{};
    CHECK(parser.Parse(buf.data(), buf.size(), frame) < 0);
    PASS("count_overflow_rejected");
}

TEST_MAIN("RDI Parser Tests")
