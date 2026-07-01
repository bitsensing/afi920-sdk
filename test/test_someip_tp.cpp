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
    // Max v3.0 RDI frame: E2E 20B + 36B + 8192*51B = 417,848B in 1392B chunks
    SomeipTpReassembler tp;
    const size_t total = 20 + 36 + 8192 * 51;
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

TEST_MAIN("SOME/IP-TP Reassembly Tests")
