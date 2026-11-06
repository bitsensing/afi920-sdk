/**
 * @file test_someip_tp.cpp
 * @brief Unit test: SOME/IP-TP segment reassembly
 */
#include "protocol/someip_tp.h"
#include "test_common.h"
#include <cstring>
#include <vector>

using namespace afi;

TEST(single_segment_complete) {
    SomeipTpReassembler tp;
    const uint8_t data[64] = {1, 2, 3};
    CHECK(tp.Feed(data, sizeof(data), 0, false));
    CHECK(tp.GetPayloadSize() == 64);
    CHECK(tp.GetPayload()[0] == 1);
    tp.Reset();
    PASS("single_segment_complete");
}

TEST(two_segments_in_order) {
    SomeipTpReassembler tp;
    std::vector<uint8_t> seg1(1392, 0xAA);
    std::vector<uint8_t> seg2(100, 0xBB);

    CHECK(!tp.Feed(seg1.data(), seg1.size(), 0, true));
    CHECK(tp.InProgress());
    CHECK(tp.Feed(seg2.data(), seg2.size(), 1392, false));
    CHECK(tp.GetPayloadSize() == 1492);
    CHECK(tp.GetPayload()[0] == 0xAA);
    CHECK(tp.GetPayload()[1391] == 0xAA);
    CHECK(tp.GetPayload()[1392] == 0xBB);
    tp.Reset();
    PASS("two_segments_in_order");
}

TEST(reset_clears_state) {
    SomeipTpReassembler tp;
    const uint8_t data[16] = {};
    tp.Feed(data, sizeof(data), 0, true);
    CHECK(tp.InProgress());
    tp.Reset();
    CHECK(!tp.InProgress());
    PASS("reset_clears_state");
}

TEST(large_rdi_frame_reassembly) {
    // Max RDI frame: E2E 20B + 36B + 8192*51B = 417,848B in 1392B chunks
    const size_t total = 20 + 36 + 8192 * 51;
    SomeipTpReassembler tp(total);
    const size_t chunk = 1392;

    std::vector<uint8_t> payload(total);
    for (size_t i = 0; i < total; i++) payload[i] = static_cast<uint8_t>(i & 0xFF);

    bool complete = false;
    for (size_t off = 0; off < total; off += chunk) {
        size_t n = (off + chunk <= total) ? chunk : (total - off);
        bool more = (off + n < total);
        complete = tp.Feed(payload.data() + off, n, static_cast<uint32_t>(off), more);
    }
    CHECK(complete);
    CHECK(tp.GetPayloadSize() == total);
    CHECK(std::memcmp(tp.GetPayload(), payload.data(), total) == 0);
    tp.Reset();
    PASS("large_rdi_frame_reassembly");
}

TEST(out_of_order_requires_full_coverage) {
    SomeipTpReassembler tp(64);
    const uint8_t first[16] = {0x11};
    const uint8_t last[16] = {0x22};

    CHECK(!tp.Feed(last, sizeof(last), 16, false, 7));
    CHECK(tp.InProgress());
    CHECK(tp.Feed(first, sizeof(first), 0, true, 7));
    CHECK(tp.GetPayloadSize() == 32);
    CHECK(tp.GetPayload()[0] == 0x11);
    CHECK(tp.GetPayload()[16] == 0x22);
    PASS("out_of_order_requires_full_coverage");
}

TEST(gap_is_not_complete) {
    SomeipTpReassembler tp(64);
    const uint8_t data[16] = {};
    CHECK(!tp.Feed(data, sizeof(data), 0, true, 1));
    CHECK(!tp.Feed(data, sizeof(data), 32, false, 1));
    CHECK(tp.InProgress());
    PASS("gap_is_not_complete");
}

TEST(oversize_segment_rejected) {
    SomeipTpReassembler tp(32);
    const uint8_t data[16] = {};
    CHECK(!tp.Feed(data, sizeof(data), 32, false, 1));
    CHECK(!tp.InProgress());
    PASS("oversize_segment_rejected");
}

TEST(invalid_segment_shape_rejected) {
    SomeipTpReassembler tp(4096);
    std::vector<uint8_t> too_large(1393);
    CHECK(!tp.Feed(too_large.data(), too_large.size(), 0, false, 1));
    CHECK(!tp.InProgress());

    const uint8_t unaligned[17] = {};
    CHECK(!tp.Feed(unaligned, sizeof(unaligned), 0, true, 1));
    CHECK(!tp.InProgress());
    PASS("invalid_segment_shape_rejected");
}

TEST(session_change_discards_old_message) {
    SomeipTpReassembler tp(64);
    const uint8_t first[16] = {0x11};
    const uint8_t last[16] = {0x22};
    CHECK(!tp.Feed(first, sizeof(first), 0, true, 1));
    CHECK(!tp.Feed(last, sizeof(last), 16, false, 2));
    CHECK(tp.Feed(first, sizeof(first), 0, true, 2));
    CHECK(tp.GetPayloadSize() == 32);
    PASS("session_change_discards_old_message");
}

TEST_MAIN("SOME/IP-TP Reassembly Tests")
