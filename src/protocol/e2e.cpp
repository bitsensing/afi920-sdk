// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/e2e.h"
#include "bts_iso23150_e2e.h"
#include <array>

namespace afi {

namespace {

const std::array<uint64_t, 256>& crc64_table() {
    static const std::array<uint64_t, 256> table = [] {
        constexpr uint64_t kReflectedPoly = 0xC96C5795D7870F42ull;
        std::array<uint64_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint64_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (c >> 1) ^ kReflectedPoly : (c >> 1);
            }
            t[n] = c;
        }
        return t;
    }();
    return table;
}

}  // namespace

uint64_t e2e_crc64_xz(const uint8_t* data, size_t len) {
    const auto& t = crc64_table();
    uint64_t crc = ~0ull;
    for (size_t i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ t[(crc ^ data[i]) & 0xFFu];
    }
    return ~crc;
}

const char* e2e_status_name(E2eStatus status) {
    switch (status) {
        case E2eStatus::kOk:             return "ok";
        case E2eStatus::kCrcMismatch:    return "crc mismatch";
        case E2eStatus::kTooShort:       return "too short";
        case E2eStatus::kLengthMismatch: return "length mismatch";
        case E2eStatus::kDataIdMismatch: return "data id mismatch";
        case E2eStatus::kCrcZero:        return "crc field zero";
    }
    return "unknown";
}

int e2e_parse(const uint8_t* buf, size_t len, E2eHeader& out) {
    if (!buf || len < BTS_E2E_HEADER_SIZE) return -1;
    out.crc_high = bts_unpack_be32(buf + BTS_E2E_OFF_CRC_HIGH);
    out.crc      = bts_unpack_be32(buf + BTS_E2E_OFF_CRC);
    out.length   = bts_unpack_be32(buf + BTS_E2E_OFF_LENGTH);
    out.counter  = bts_unpack_be32(buf + BTS_E2E_OFF_COUNTER);
    out.data_id  = bts_unpack_be32(buf + BTS_E2E_OFF_DATA_ID);
    out.generation = out.crc_high == 0 ? AfiInterfaceGeneration::kV1_1
                                       : AfiInterfaceGeneration::kV1_0;
    return 0;
}

E2eStatus e2e_validate(const uint8_t* buf, size_t len, E2eHeader& out,
                       uint32_t expected_data_id, bool check_crc) {
    if (e2e_parse(buf, len, out) < 0) return E2eStatus::kTooShort;
    const size_t body_len = len - BTS_E2E_HEADER_SIZE;
    if (out.length != body_len) {
        return E2eStatus::kLengthMismatch;
    }
    if (expected_data_id != 0 && out.data_id != expected_data_id) {
        return E2eStatus::kDataIdMismatch;
    }
    if (!check_crc) return E2eStatus::kOk;

    // System Spec §7.5: a zero CRC field is never a pass.
    if (out.crc_high == 0 && out.crc == 0) return E2eStatus::kCrcZero;
    if (out.crc_high != 0) {
        // Interface v1.0.x: CRC-64/XZ over Length, Counter, Data ID and body.
        const uint64_t want = (static_cast<uint64_t>(out.crc_high) << 32) | out.crc;
        return e2e_crc64_xz(buf + BTS_E2E_OFF_LENGTH, len - BTS_E2E_OFF_LENGTH) == want
                   ? E2eStatus::kOk : E2eStatus::kCrcMismatch;
    }

    const uint32_t crc = bts_e2e_crc32(buf + BTS_E2E_HEADER_SIZE, body_len);
    return (crc == out.crc) ? E2eStatus::kOk : E2eStatus::kCrcMismatch;
}

void e2e_write_header(uint8_t* out, uint32_t data_id, uint32_t counter,
                      const uint8_t* body, size_t body_len) {
    bts_e2e_build_header(out, data_id, counter, static_cast<uint32_t>(body_len));
    bts_e2e_apply_crc(out, body, body_len);
}

} // namespace afi
