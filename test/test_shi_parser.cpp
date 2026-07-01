/**
 * @file test_shi_parser.cpp
 * @brief Unit test: SHII v3.0 variable-length payload parsing
 */
#include "protocol/shi_parser.h"
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

// Build a synthetic SHII payload following the v3.0 serializer order:
// CIH(24) | n_op + modes | defect(2) | supply/temp(2) |
// n_in + types + statuses | time_sync | n_cal + comp + status + state
static std::vector<uint8_t> make_shi(uint8_t n_op, uint8_t n_in, uint8_t n_cal) {
    std::vector<uint8_t> buf;
    buf.resize(24, 0);
    buf[0] = 1;                       // version major
    buf[3] = 0x0D;                    // IID sensor health
    buf[4] = 1; buf[5] = 7;           // serving, sensor id
    pack_le64(&buf[6], 555ull);
    pack_le32(&buf[14], 99);          // message counter
    pack_le32(&buf[18], 100000000);
    buf[22] = 0; buf[23] = 0;

    buf.push_back(n_op);
    static const uint8_t kModes[] = {0, 1, 2, 3, 4, 5, 10, 50, 100, 200, 201};
    for (uint8_t i = 0; i < n_op; i++) buf.push_back(kModes[i]);

    buf.push_back(1);                 // defect recognised: NOT_FULLY_FUNCTIONAL
    buf.push_back(11);                // defect reason: EXTERNALLY_DISTURBED
    buf.push_back(2);                 // supply voltage: WITHIN_LIMITS
    buf.push_back(2);                 // temperature: IN_LIMITS

    buf.push_back(n_in);
    for (uint8_t i = 0; i < n_in; i++) buf.push_back(i);        // types
    for (uint8_t i = 0; i < n_in; i++) buf.push_back(0);        // statuses: VALID

    buf.push_back(3);                 // time sync: NOT_SYNCHRONIZED

    buf.push_back(n_cal);
    for (uint8_t i = 0; i < n_cal; i++) buf.push_back(i);       // components
    for (uint8_t i = 0; i < n_cal; i++) buf.push_back(1);       // statuses: NOT_CALIBRATED
    for (uint8_t i = 0; i < n_cal; i++) buf.push_back(2);       // states: INITIAL_CAL_FAILED

    return buf;
}

TEST(parse_full_message) {
    auto buf = make_shi(11, 4, 3);

    ShiParser parser;
    AfiShiMessage msg{};
    CHECK(parser.Parse(buf.data(), buf.size(), msg) == 0);

    CHECK(msg.sensor_id == 7);
    CHECK(msg.message_counter == 99);
    CHECK(msg.num_valid_operation_modes == 11);
    CHECK(msg.sensor_operation_modes[4] == 4);    // ULR
    CHECK(msg.sensor_operation_modes[5] == 5);    // USR
    CHECK(msg.sensor_operation_modes[7] == 50);   // Evaluation
    CHECK(msg.sensor_operation_modes[8] == 100);  // Calibration (EOL)
    CHECK(msg.sensor_operation_modes[10] == 201); // TestMode

    CHECK(msg.sensor_defect_recognised == BTS_SHI_SDR_NOT_FULLY_FUNCTIONAL);
    CHECK(msg.sensor_defect_reason == BTS_SHI_SDR_EXTERNALLY_DISTURBED);
    CHECK(msg.supply_voltage_status == BTS_SHI_SVS_WITHIN_LIMITS);

    CHECK(msg.num_valid_input_signal_statuses == 4);
    CHECK(msg.sensor_input_signal_types[3] == 3);    // CSII_SENSOR_POSE
    CHECK(msg.sensor_input_signal_statuses[0] == 0); // VALID

    CHECK(msg.sensor_time_sync == BTS_SHI_STS_NOT_SYNCHRONIZED);

    CHECK(msg.num_valid_calibration_components == 3);
    CHECK(msg.sensor_calibration_components[2] == 2);
    CHECK(msg.sensor_calibration_statuses[0] == 1);
    CHECK(msg.sensor_calibration_states[0] == 2);
    PASS("parse_full_message");
}

TEST(parse_minimal_message) {
    // All counts zero — minimum 32B
    auto buf = make_shi(0, 0, 0);
    CHECK(buf.size() == 32u);

    ShiParser parser;
    AfiShiMessage msg{};
    CHECK(parser.Parse(buf.data(), buf.size(), msg) == 0);
    CHECK(msg.num_valid_operation_modes == 0);
    CHECK(msg.num_valid_input_signal_statuses == 0);
    CHECK(msg.num_valid_calibration_components == 0);
    PASS("parse_minimal_message");
}

TEST(variable_length_sizes) {
    // Wire size depends on counts: 24 +1+N_op +2 +2 +1+2*N_in +1 +1+3*N_cal
    CHECK(make_shi(11, 4, 3).size() == 24u + 1 + 11 + 2 + 2 + 1 + 8 + 1 + 1 + 9);
    CHECK(make_shi(1, 1, 1).size() == 24u + 1 + 1 + 2 + 2 + 1 + 2 + 1 + 1 + 3);
    PASS("variable_length_sizes");
}

TEST(truncated_rejected) {
    auto buf = make_shi(11, 4, 3);
    buf.resize(buf.size() - 5);  // cut into calibration arrays

    ShiParser parser;
    AfiShiMessage msg{};
    CHECK(parser.Parse(buf.data(), buf.size(), msg) < 0);

    uint8_t tiny[31] = {};
    CHECK(parser.Parse(tiny, sizeof(tiny), msg) < 0);
    PASS("truncated_rejected");
}

TEST(count_overflow_clamped) {
    auto buf = make_shi(11, 4, 3);
    buf[24] = 200;  // n_op far above max 11

    ShiParser parser;
    AfiShiMessage msg{};
    // Clamped count no longer matches the actual wire layout, so the parse
    // may fail — it must not overflow the fixed-size struct arrays either way.
    parser.Parse(buf.data(), buf.size(), msg);
    CHECK(msg.num_valid_operation_modes <= BTS_SHI_MAX_OPERATION_MODES);
    PASS("count_overflow_clamped");
}

TEST_MAIN("SHII Parser Tests (v3.0 variable length)")
