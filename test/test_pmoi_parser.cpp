/**
 * @file test_pmoi_parser.cpp
 * @brief Unit test: PMOI payload parsing (30B header + 116B objects)
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "protocol/pmoi_parser.h"
#include "test_common.h"

#include "afi/afi_config.h"
#include "afi/afi_sdk.h"

#include <cstring>
#include <vector>

using namespace afi;

static void pack_le16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

static void pack_le32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

static void pack_le64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

static void pack_f32(uint8_t* p, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    pack_le32(p, bits);
}

static std::vector<uint8_t> make_pmoi(uint16_t count) {
    std::vector<uint8_t> buf(BTS_PMOI_HEADER_SIZE +
                             static_cast<size_t>(count) * BTS_PMOI_OBJECT_SIZE, 0);
    buf[0] = 1;
    buf[3] = 0x02;
    buf[4] = 1;
    buf[5] = 4;
    pack_le64(buf.data() + 6, 9876543210ull);
    pack_le32(buf.data() + 14, 77);
    pack_le32(buf.data() + 18, 100000000);
    buf[22] = 3;
    buf[23] = BTS_DQ_NORMAL;
    buf[24] = BTS_PMOI_MT_RELATIVE;
    pack_le16(buf.data() + 25, BTS_PMOI_MAX_OBJECTS);
    buf[27] = 1;
    pack_le16(buf.data() + 28, count);

    for (uint16_t i = 0; i < count; ++i) {
        uint8_t* o = buf.data() + BTS_PMOI_HEADER_SIZE +
                     static_cast<size_t>(i) * BTS_PMOI_OBJECT_SIZE;
        pack_f32(o + 0, 95.0f);
        o[4] = static_cast<uint8_t>(10 + i);
        o[5] = 2;
        pack_f32(o + 6, 1.25f);
        o[10] = BTS_PMOI_MS_MEASURED;
        o[11] = 2;
        o[12] = BTS_PMOI_CT_CAR;
        o[13] = 80;
        o[14] = BTS_PMOI_CT_TRUCK;
        o[15] = 20;
        o[22] = 1;
        for (size_t f = 0; f < 22; ++f) {
            pack_f32(o + 23 + f * 4, static_cast<float>(f) + 0.5f);
        }
        o[111] = BTS_PMOI_MOV_MOVING;
        pack_f32(o + 112, 12.5f);
    }
    return buf;
}

TEST(layout_constants) {
    CHECK(BTS_PMOI_HEADER_SIZE == 30u);
    CHECK(BTS_PMOI_OBJECT_SIZE == 116u);
    CHECK(BTS_PMOI_MAX_OBJECTS == 255u);
    CHECK(BTS_PMOI_MAX_PAYLOAD_SIZE == 29610u);
    PASS("layout_constants");
}

TEST(public_api_source_compatibility) {
    // The initializer prefix is the complete pre-v1.0.2 member order. The new
    // values follow it, proving the additions remain appended rather than
    // being inserted between existing fields.
    AfiSdkConfig legacy_config{
        "127.0.0.1", 30000, 30001, 30002, 30003, 30004, "udp",
        false, true, true, false, false, true, 1234, 10, 20,
        30512, "", "", "", "", false,
    };
    CHECK(legacy_config.csii_port == 30004);
    CHECK(legacy_config.stream_transport == "udp");
    CHECK(legacy_config.request_timeout_ms == 20);
    CHECK(legacy_config.pmoi_port == 30512);
    CHECK(!legacy_config.enable_pmoi);

    AfiPtpConfig legacy_ptp{
        1, "1588v2-L2", 0, 128, 128, "SLAVE", 2, 10, 20, "clock",
        0, "", false,
    };
    CHECK(legacy_ptp.domain == 0);
    CHECK(legacy_ptp.master_clock_id == "clock");
    CHECK(legacy_ptp.configured_profile == 0);
    CHECK(!legacy_ptp.restart_required);

    using LegacyPtpSetter = int (AfiSdk::*)(uint8_t);
    LegacyPtpSetter setter =
        static_cast<LegacyPtpSetter>(&AfiSdk::SetPtpConfig);
    CHECK(setter != nullptr);
    PASS("public_api_source_compatibility");
}

TEST(parse_empty_frame) {
    auto buf = make_pmoi(0);
    PmoiParser parser;
    AfiPmoiFrame frame{};
    CHECK(parser.Parse(buf.data(), buf.size(), frame) == 0);
    CHECK(frame.message.interface_id == 0x02);
    CHECK(frame.message.message_counter == 77);
    CHECK(frame.message.num_pmo == 0);
    CHECK(frame.message.objects == nullptr);
    PASS("parse_empty_frame");
}

TEST(parse_objects) {
    auto buf = make_pmoi(2);
    PmoiParser parser;
    AfiPmoiFrame frame{};
    CHECK(parser.Parse(buf.data(), buf.size(), frame) == 0);
    CHECK(frame.message.num_pmo == 2);
    CHECK(frame.message.recognised_pmo_capability == 255);
    CHECK(frame.message.objects != nullptr);

    const auto& object = frame.message.objects[0];
    CHECK(object.existence_probability == 95.0f);
    CHECK(object.object_id == 10);
    CHECK(object.age == 1.25f);
    CHECK(object.num_classifications == 2);
    CHECK(object.classifications[0].type == BTS_PMOI_CT_CAR);
    CHECK(object.classifications[0].confidence == 80);
    CHECK(object.position_x == 0.5f);
    CHECK(object.orientation_yaw == 6.5f);
    CHECK(object.velocity_x == 10.5f);
    CHECK(object.acceleration_error_z == 21.5f);
    CHECK(object.entity_radar_cross_section == 12.5f);
    CHECK(frame.message.objects[1].object_id == 11);
    PASS("parse_objects");
}

TEST(interface_serializer_roundtrip) {
    bts_pmoi_message_t message;
    bts_pmoi_init(&message);
    bts_pmoi_set_version(&message, 1, 2, 3);
    bts_pmoi_set_sensor_info(&message, 9, 2);
    bts_pmoi_set_timestamp(&message, 123456789ull);
    bts_pmoi_set_message_counter(&message, 88);
    message.interface_cycle_time = 123000000u;
    message.interface_cycle_time_variation = 7;
    message.data_qualifier = BTS_DQ_TEST_MODE;
    message.motion_type = BTS_PMOI_MT_ABSOLUTE;
    bts_pmoi_set_capability(&message, 200);
    message.recognised_pmo_status = 9;

    bts_pmoi_object_t object;
    bts_pmoi_object_init(&object);
    object.existence_probability = 91.5f;
    object.object_id = 42;
    object.object_grouping_id = 7;
    object.age = 2.25f;
    object.measurement_status = BTS_PMOI_MS_PREDICTED_OCCLUDED;
    object.num_classifications = BTS_PMOI_MAX_CLASSIFICATIONS;
    object.classifications[0] = {BTS_PMOI_CT_CAR, 51};
    object.classifications[1] = {BTS_PMOI_CT_TRUCK, 52};
    object.classifications[2] = {BTS_PMOI_CT_BICYCLE, 53};
    object.classifications[3] = {BTS_PMOI_CT_PEDESTRIAN, 54};
    object.classifications[4] = {BTS_PMOI_CT_ANIMAL, 55};
    object.reference_point = 8;
    object.position_x = 12.25f;
    object.position_y = -13.5f;
    object.position_z = 0.75f;
    object.position_error_x = 0.25f;
    object.position_error_y = 0.5f;
    object.position_error_z = 0.75f;
    object.orientation_yaw = -1.25f;
    object.bounding_box_length = 4.5f;
    object.bounding_box_width = 2.0f;
    object.bounding_box_height = 1.5f;
    object.velocity_x = 3.25f;
    object.velocity_y = -2.5f;
    object.velocity_z = 0.125f;
    object.velocity_error_x = 0.25f;
    object.velocity_error_y = 0.5f;
    object.velocity_error_z = 0.75f;
    object.acceleration_x = 1.25f;
    object.acceleration_y = -1.5f;
    object.acceleration_z = 2.75f;
    object.acceleration_error_x = 0.125f;
    object.acceleration_error_y = 0.375f;
    object.acceleration_error_z = 0.625f;
    object.movement_status = BTS_PMOI_MOV_STATIONARY;
    object.entity_radar_cross_section = 7.75f;
    bts_pmoi_set_objects(&message, &object, 1);

    std::vector<uint8_t> wire(bts_pmoi_serialized_size(&message));
    CHECK(bts_pmoi_serialize(&message, wire.data(), wire.size()) ==
          static_cast<int>(wire.size()));

    PmoiParser parser;
    AfiPmoiFrame frame{};
    CHECK(parser.Parse(wire.data(), wire.size(), frame) == 0);
    CHECK(frame.message.interface_version.major == 1);
    CHECK(frame.message.interface_version.minor == 2);
    CHECK(frame.message.interface_version.patch == 3);
    CHECK(frame.message.sensor_id == 9);
    CHECK(frame.message.num_serving_sensors == 2);
    CHECK(frame.message.timestamp == 123456789ull);
    CHECK(frame.message.message_counter == 88);
    CHECK(frame.message.interface_cycle_time == 123000000u);
    CHECK(frame.message.interface_cycle_time_variation == 7);
    CHECK(frame.message.data_qualifier == BTS_DQ_TEST_MODE);
    CHECK(frame.message.motion_type == BTS_PMOI_MT_ABSOLUTE);
    CHECK(frame.message.recognised_pmo_capability == 200);
    CHECK(frame.message.recognised_pmo_status == 9);
    CHECK(frame.message.num_pmo == 1);
    CHECK(frame.message.objects[0].existence_probability == 91.5f);
    CHECK(frame.message.objects[0].object_id == 42);
    CHECK(frame.message.objects[0].object_grouping_id == 7);
    CHECK(frame.message.objects[0].age == 2.25f);
    CHECK(frame.message.objects[0].measurement_status ==
          BTS_PMOI_MS_PREDICTED_OCCLUDED);
    CHECK(frame.message.objects[0].num_classifications ==
          BTS_PMOI_MAX_CLASSIFICATIONS);
    CHECK(frame.message.objects[0].classifications[0].type == BTS_PMOI_CT_CAR);
    CHECK(frame.message.objects[0].classifications[0].confidence == 51);
    CHECK(frame.message.objects[0].classifications[1].type == BTS_PMOI_CT_TRUCK);
    CHECK(frame.message.objects[0].classifications[1].confidence == 52);
    CHECK(frame.message.objects[0].classifications[2].type == BTS_PMOI_CT_BICYCLE);
    CHECK(frame.message.objects[0].classifications[2].confidence == 53);
    CHECK(frame.message.objects[0].classifications[3].type ==
          BTS_PMOI_CT_PEDESTRIAN);
    CHECK(frame.message.objects[0].classifications[3].confidence == 54);
    CHECK(frame.message.objects[0].classifications[4].type == BTS_PMOI_CT_ANIMAL);
    CHECK(frame.message.objects[0].classifications[4].confidence == 55);

    const auto& parsed = frame.message.objects[0];
    CHECK(parsed.reference_point == 8);
    CHECK(parsed.position_x == 12.25f);
    CHECK(parsed.position_y == -13.5f);
    CHECK(parsed.position_z == 0.75f);
    CHECK(parsed.position_error_x == 0.25f);
    CHECK(parsed.position_error_y == 0.5f);
    CHECK(parsed.position_error_z == 0.75f);
    CHECK(parsed.orientation_yaw == -1.25f);
    CHECK(parsed.bounding_box_length == 4.5f);
    CHECK(parsed.bounding_box_width == 2.0f);
    CHECK(parsed.bounding_box_height == 1.5f);
    CHECK(parsed.velocity_x == 3.25f);
    CHECK(parsed.velocity_y == -2.5f);
    CHECK(parsed.velocity_z == 0.125f);
    CHECK(parsed.velocity_error_x == 0.25f);
    CHECK(parsed.velocity_error_y == 0.5f);
    CHECK(parsed.velocity_error_z == 0.75f);
    CHECK(parsed.acceleration_x == 1.25f);
    CHECK(parsed.acceleration_y == -1.5f);
    CHECK(parsed.acceleration_z == 2.75f);
    CHECK(parsed.acceleration_error_x == 0.125f);
    CHECK(parsed.acceleration_error_y == 0.375f);
    CHECK(parsed.acceleration_error_z == 0.625f);
    CHECK(parsed.movement_status == BTS_PMOI_MOV_STATIONARY);
    CHECK(parsed.entity_radar_cross_section == 7.75f);
    PASS("interface_serializer_roundtrip");
}

TEST(parse_maximum_object_count) {
    auto buf = make_pmoi(BTS_PMOI_MAX_OBJECTS);
    PmoiParser parser;
    AfiPmoiFrame frame{};
    CHECK(buf.size() == BTS_PMOI_MAX_PAYLOAD_SIZE);
    CHECK(parser.Parse(buf.data(), buf.size(), frame) == 0);
    CHECK(frame.message.num_pmo == BTS_PMOI_MAX_OBJECTS);
    CHECK(frame.message.objects[0].object_id == 10);
    CHECK(frame.message.objects[BTS_PMOI_MAX_OBJECTS - 1].object_id ==
          static_cast<uint8_t>(10 + BTS_PMOI_MAX_OBJECTS - 1));
    PASS("parse_maximum_object_count");
}

TEST(owning_frame_copy_and_move) {
    auto buf = make_pmoi(2);
    PmoiParser parser;
    AfiPmoiFrame original{};
    CHECK(parser.Parse(buf.data(), buf.size(), original) == 0);

    AfiPmoiFrame copied = original;
    CHECK(copied.message.objects == copied.objects_storage.data());
    CHECK(copied.message.objects != original.message.objects);
    CHECK(copied.message.objects[1].object_id == 11);

    AfiPmoiFrame moved = std::move(copied);
    CHECK(moved.message.objects == moved.objects_storage.data());
    CHECK(moved.message.objects[1].object_id == 11);
    CHECK(copied.message.objects == nullptr);
    PASS("owning_frame_copy_and_move");
}

TEST(malformed_payload_rejected) {
    PmoiParser parser;
    AfiPmoiFrame frame{};
    uint8_t short_header[BTS_PMOI_HEADER_SIZE - 1] = {};
    CHECK(parser.Parse(short_header, sizeof(short_header), frame) < 0);

    auto truncated = make_pmoi(1);
    truncated.pop_back();
    CHECK(parser.Parse(truncated.data(), truncated.size(), frame) < 0);

    auto too_many = make_pmoi(0);
    pack_le16(too_many.data() + 28, BTS_PMOI_MAX_OBJECTS + 1);
    CHECK(parser.Parse(too_many.data(), too_many.size(), frame) < 0);

    auto classifications = make_pmoi(1);
    classifications[BTS_PMOI_HEADER_SIZE + 11] = BTS_PMOI_MAX_CLASSIFICATIONS + 1;
    CHECK(parser.Parse(classifications.data(), classifications.size(), frame) < 0);
    PASS("malformed_payload_rejected");
}

TEST_MAIN("PMOI Parser Tests")
