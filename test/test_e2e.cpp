/**
 * @file test_e2e.cpp
 * @brief Unit test: E2E header (20B, CRC-32) parse and validation, v1.1.0,
 *        and the interface v1.0.x CRC-64/XZ accepted on receive
 */
#include "protocol/e2e.h"
#include "bts_iso23150_e2e.h"
#include "legacy_vectors.h"
#include "test_common.h"
#include <cstring>

using namespace afi;

static void make_frame(uint8_t* frame, uint32_t data_id, uint32_t counter,
                       const uint8_t* body, size_t body_len) {
    std::memcpy(frame + 20, body, body_len);
    e2e_write_header(frame, data_id, counter, frame + 20, body_len);
}

// IEEE 802.3 CRC-32 known-answer test: crc("123456789") = 0xCBF43926
TEST(crc32_known_answer) {
    const uint8_t kat[] = {'1','2','3','4','5','6','7','8','9'};
    CHECK(bts_e2e_crc32(kat, sizeof(kat)) == 0xCBF43926u);
    CHECK(bts_e2e_crc32(kat, sizeof(kat)) == BTS_E2E_CRC32_KAT);
    // Chained updates equal one pass over the concatenation.
    uint32_t crc = bts_e2e_crc32_update(BTS_E2E_CRC32_INIT, kat, 4);
    crc = bts_e2e_crc32_update(crc, kat + 4, sizeof(kat) - 4);
    CHECK(crc == 0xCBF43926u);
    PASS("crc32_known_answer");
}

TEST(header_layout_big_endian) {
    const uint8_t body[9] = {'1','2','3','4','5','6','7','8','9'};
    uint8_t frame[20 + 9];
    make_frame(frame, BTS_E2E_DATA_ID_RDI, 0x01020304u, body, sizeof(body));

    // CRC high word 0, CRC-32 at offset 4, Length 8, Counter 12, Data ID 16.
    const uint8_t expected[20] = {
        0x00, 0x00, 0x00, 0x00,  0xCB, 0xF4, 0x39, 0x26,
        0x00, 0x00, 0x00, 0x09,  0x01, 0x02, 0x03, 0x04,
        0x60, 0x00, 0x80, 0x02,
    };
    CHECK(std::memcmp(frame, expected, sizeof(expected)) == 0);
    PASS("header_layout_big_endian");
}

// Golden headers pinned by the interface (tests/python/codec/test_e2e_vectors.py,
// tests/c/src/AFI920/test_e2e.c): body i -> (i*7+1)&0xFF, and the Config API
// response body below. These bytes come from the interface, not from this SDK.
TEST(interface_golden_headers) {
    uint8_t body51[51];
    for (int i = 0; i < 51; ++i) body51[i] = static_cast<uint8_t>((i * 7 + 1) & 0xFF);
    uint8_t rdi[20 + 51];
    make_frame(rdi, 0x60008002u, 1234, body51, sizeof(body51));
    const uint8_t golden_rdi[20] = {
        0x00, 0x00, 0x00, 0x00,  0xE4, 0xEA, 0x63, 0x3D,
        0x00, 0x00, 0x00, 0x33,  0x00, 0x00, 0x04, 0xD2,
        0x60, 0x00, 0x80, 0x02,
    };
    CHECK(std::memcmp(rdi, golden_rdi, sizeof(golden_rdi)) == 0);

    const char json[] = "{\"return_code\":0,\"crc32\":\"0x1a2b3c4d\"}";
    const size_t json_len = sizeof(json) - 1;
    uint8_t cfg[20 + sizeof(json)];
    make_frame(cfg, 0x600000A5u, 7, reinterpret_cast<const uint8_t*>(json), json_len);
    const uint8_t golden_cfg[20] = {
        0x00, 0x00, 0x00, 0x00,  0x59, 0x42, 0x2E, 0xD7,
        0x00, 0x00, 0x00, 0x26,  0x00, 0x00, 0x00, 0x07,
        0x60, 0x00, 0x00, 0xA5,
    };
    CHECK(std::memcmp(cfg, golden_cfg, sizeof(golden_cfg)) == 0);

    CHECK(bts_e2e_crc32(body51, 0) == 0u);  // empty body: init == final XOR
    PASS("interface_golden_headers");
}

TEST(build_and_validate_roundtrip) {
    uint8_t frame[20 + 16];
    const uint8_t payload[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    make_frame(frame, BTS_E2E_DATA_ID_RDI, 77, payload, sizeof(payload));

    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr,
                       BTS_E2E_DATA_ID_RDI) == E2eStatus::kOk);
    CHECK(hdr.crc_high == 0);
    CHECK(hdr.length == sizeof(payload));
    CHECK(hdr.counter == 77);
    CHECK(hdr.data_id == BTS_E2E_DATA_ID_RDI);
    PASS("build_and_validate_roundtrip");
}

TEST(crc_mismatch_detected) {
    uint8_t frame[20 + 8] = {};
    const uint8_t payload[8] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x11,0x22};
    make_frame(frame, BTS_E2E_DATA_ID_SHII, 1, payload, sizeof(payload));

    frame[25] ^= 0x01;  // corrupt one body byte

    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kCrcMismatch);
    PASS("crc_mismatch_detected");
}

TEST(header_fields_outside_crc) {
    // Counter is not part of the protected region (body only).
    uint8_t frame[20 + 4] = {};
    const uint8_t payload[4] = {1,2,3,4};
    make_frame(frame, BTS_E2E_DATA_ID_SPI, 10, payload, sizeof(payload));

    frame[15] ^= 0x01;

    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kOk);
    CHECK(hdr.counter == 11);
    PASS("header_fields_outside_crc");
}

TEST(zero_crc_is_not_a_pass) {
    uint8_t frame[20 + 4] = {};
    const uint8_t payload[4] = {1,2,3,4};
    make_frame(frame, BTS_E2E_DATA_ID_SPI, 10, payload, sizeof(payload));
    std::memset(frame, 0, 8);

    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kCrcZero);
    // Structure-only check (CRC verification disabled) still passes.
    CHECK(e2e_validate(frame, sizeof(frame), hdr, 0, false) == E2eStatus::kOk);
    PASS("zero_crc_is_not_a_pass");
}

TEST(crc_high_nonzero_selects_crc64) {
    // A CRC-32 frame with the high word set no longer matches: the high word
    // marks an interface v1.0.x CRC-64/XZ.
    uint8_t frame[20 + 4] = {};
    const uint8_t payload[4] = {1,2,3,4};
    make_frame(frame, BTS_E2E_DATA_ID_SPI, 10, payload, sizeof(payload));
    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kOk);
    CHECK(hdr.generation == AfiInterfaceGeneration::kV1_1);
    frame[0] = 0x01;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kCrcMismatch);
    CHECK(hdr.generation == AfiInterfaceGeneration::kV1_0);
    PASS("crc_high_nonzero_selects_crc64");
}

TEST(crc64_known_answer) {
    const uint8_t kat[] = {'1','2','3','4','5','6','7','8','9'};
    CHECK(e2e_crc64_xz(kat, sizeof(kat)) == 0x995DC9BBDF1939FAull);
    PASS("crc64_known_answer");
}

TEST(legacy_v1_0_frames_validate) {
    struct Stream { const char* file; uint32_t data_id; size_t body; };
    const Stream streams[] = {{"rdi_3frames.bin", BTS_E2E_DATA_ID_RDI, 36},
                              {"shii_3frames.bin", BTS_E2E_DATA_ID_SHII, 44},
                              {"spi_3frames.bin", BTS_E2E_DATA_ID_SPI, 77}};
    for (const Stream& s : streams) {
        auto frames = legacy_frames(s.file);
        CHECK(frames.size() == 3);
        for (size_t i = 0; i < frames.size(); ++i) {
            std::vector<uint8_t>& f = frames[i];
            E2eHeader hdr;
            CHECK(e2e_validate(f.data() + 16, f.size() - 16, hdr, s.data_id) == E2eStatus::kOk);
            CHECK(hdr.generation == AfiInterfaceGeneration::kV1_0);
            CHECK(hdr.counter == i + 1 && hdr.length == s.body);
            // The v1.0.x CRC-64 also covers Counter, unlike the v1.1.0 CRC-32.
            f[16 + 15] ^= 0x01;
            CHECK(e2e_validate(f.data() + 16, f.size() - 16, hdr, s.data_id) ==
                  E2eStatus::kCrcMismatch);
            f[16 + 15] ^= 0x01;
            f.back() ^= 0x80;
            CHECK(e2e_validate(f.data() + 16, f.size() - 16, hdr, s.data_id) ==
                  E2eStatus::kCrcMismatch);
            // Structure-only validation still passes and still names the generation.
            CHECK(e2e_validate(f.data() + 16, f.size() - 16, hdr, s.data_id, false) ==
                  E2eStatus::kOk);
            CHECK(hdr.generation == AfiInterfaceGeneration::kV1_0);
        }
    }
    PASS("legacy_v1_0_frames_validate");
}

TEST(too_short_rejected) {
    uint8_t frame[19] = {};
    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kTooShort);
    CHECK(e2e_parse(frame, sizeof(frame), hdr) < 0);
    CHECK(e2e_parse(nullptr, 100, hdr) < 0);
    PASS("too_short_rejected");
}

TEST(length_mismatch_rejected) {
    uint8_t frame[20 + 4] = {};
    const uint8_t payload[4] = {1, 2, 3, 4};
    make_frame(frame, BTS_E2E_DATA_ID_RDI, 1, payload, sizeof(payload));

    frame[BTS_E2E_OFF_LENGTH + 3] = 3;
    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr) == E2eStatus::kLengthMismatch);
    PASS("length_mismatch_rejected");
}

TEST(data_id_mismatch_rejected) {
    uint8_t frame[20 + 4] = {};
    const uint8_t payload[4] = {1, 2, 3, 4};
    make_frame(frame, BTS_E2E_DATA_ID_SPI, 1, payload, sizeof(payload));

    E2eHeader hdr;
    CHECK(e2e_validate(frame, sizeof(frame), hdr,
                       BTS_E2E_DATA_ID_RDI) == E2eStatus::kDataIdMismatch);
    CHECK(e2e_validate(frame, sizeof(frame), hdr,
                       BTS_E2E_DATA_ID_SPI) == E2eStatus::kOk);
    PASS("data_id_mismatch_rejected");
}

TEST(e2e_data_ids_match_registry) {
    CHECK(BTS_E2E_DATA_ID_CSII == 0x60008001u);
    CHECK(BTS_E2E_DATA_ID_RDI  == 0x60008002u);
    CHECK(BTS_E2E_DATA_ID_SHII == 0x60008003u);
    CHECK(BTS_E2E_DATA_ID_SPI  == 0x60008004u);
    CHECK(BTS_E2E_DATA_ID_PMOI == 0x60008005u);
    // The Config API uses the Method ID.
    CHECK(BTS_E2E_DATA_ID(0x0020) == 0x60000020u);
    CHECK(BTS_E2E_DATA_ID(0x00E1) == 0x600000E1u);
    PASS("e2e_data_ids_match_registry");
}

TEST_MAIN("E2E (CRC-32 v1.1.0, CRC-64 v1.0.x) Tests")
