/**
 * @file test_e2e.cpp
 * @brief Unit test: E2E Profile 7 header parse + CRC-64/XZ validation
 */
#include "protocol/e2e.h"
#include "bts_iso23150_e2e.h"
#include "test_common.h"
#include <cstring>

using namespace afi;

// AUTOSAR CRC-64/XZ known-answer test: crc("123456789") = 0x995DC9BBDF1939FA
TEST(crc64_known_answer) {
    const uint8_t kat[] = {'1','2','3','4','5','6','7','8','9'};
    uint64_t crc = bts_e2e_crc64_update(BTS_E2E_CRC64_INIT, kat, sizeof(kat));
    crc ^= BTS_E2E_CRC64_XOROUT;
    CHECK(crc == 0x995DC9BBDF1939FAull);
    PASS("crc64_known_answer");
}

TEST(build_and_validate_roundtrip) {
    uint8_t frame[20 + 16];
    const uint8_t payload[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

    bts_e2e_build_header(frame, BTS_E2E_DATA_ID_RDI, 77, sizeof(payload));
    std::memcpy(frame + 20, payload, sizeof(payload));
    bts_e2e_apply_crc_contig(frame, sizeof(frame));

    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kOk);
    CHECK(hdr.length == sizeof(payload));
    CHECK(hdr.counter == 77);
    CHECK(hdr.data_id == BTS_E2E_DATA_ID_RDI);
    PASS("build_and_validate_roundtrip");
}

TEST(crc_mismatch_detected) {
    uint8_t frame[20 + 8] = {};
    const uint8_t payload[8] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x11,0x22};

    bts_e2e_build_header(frame, BTS_E2E_DATA_ID_SHII, 1, sizeof(payload));
    std::memcpy(frame + 20, payload, sizeof(payload));
    bts_e2e_apply_crc_contig(frame, sizeof(frame));

    frame[25] ^= 0x01;  // corrupt one payload byte

    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kCrcMismatch);
    PASS("crc_mismatch_detected");
}

TEST(counter_corruption_detected) {
    uint8_t frame[20 + 4] = {};
    const uint8_t payload[4] = {1,2,3,4};

    bts_e2e_build_header(frame, BTS_E2E_DATA_ID_SPI, 10, sizeof(payload));
    std::memcpy(frame + 20, payload, sizeof(payload));
    bts_e2e_apply_crc_contig(frame, sizeof(frame));

    frame[15] ^= 0x01;  // corrupt counter (protected region byte)

    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kCrcMismatch);
    PASS("counter_corruption_detected");
}

TEST(too_short_rejected) {
    uint8_t frame[19] = {};
    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kTooShort);
    CHECK(e2e_parse(frame, sizeof(frame), hdr) < 0);
    CHECK(e2e_parse(nullptr, 100, hdr) < 0);
    PASS("too_short_rejected");
}

TEST(split_crc_equals_contig) {
    // RDI scatter-gather path must yield the same CRC as contiguous
    uint8_t payload[32];
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(i * 7);

    uint8_t contig[20 + 32];
    bts_e2e_build_header(contig, BTS_E2E_DATA_ID_RDI, 5, sizeof(payload));
    std::memcpy(contig + 20, payload, sizeof(payload));
    bts_e2e_apply_crc_contig(contig, sizeof(contig));

    uint8_t split_hdr[20];
    bts_e2e_build_header(split_hdr, BTS_E2E_DATA_ID_RDI, 5, sizeof(payload));
    bts_e2e_apply_crc_split(split_hdr, payload, sizeof(payload));

    CHECK(std::memcmp(contig, split_hdr, 20) == 0);
    PASS("split_crc_equals_contig");
}

TEST(e2e_data_ids_match_registry) {
    CHECK(BTS_E2E_DATA_ID_CSII == 0x60008001u);
    CHECK(BTS_E2E_DATA_ID_RDI  == 0x60008002u);
    CHECK(BTS_E2E_DATA_ID_SHII == 0x60008003u);
    CHECK(BTS_E2E_DATA_ID_SPI  == 0x60008004u);
    PASS("e2e_data_ids_match_registry");
}

TEST_MAIN("E2E Profile 7 Tests")
