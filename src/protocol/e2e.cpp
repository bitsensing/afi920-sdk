// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/e2e.h"
#include "bts_iso23150_e2e.h"

namespace afi {

static inline uint64_t unpack_be64(const uint8_t* p) {
    return (static_cast<uint64_t>(p[0]) << 56) | (static_cast<uint64_t>(p[1]) << 48) |
           (static_cast<uint64_t>(p[2]) << 40) | (static_cast<uint64_t>(p[3]) << 32) |
           (static_cast<uint64_t>(p[4]) << 24) | (static_cast<uint64_t>(p[5]) << 16) |
           (static_cast<uint64_t>(p[6]) << 8)  |  static_cast<uint64_t>(p[7]);
}

int e2e_parse(const uint8_t* buf, size_t len, E2eHeader& out) {
    if (!buf || len < BTS_E2E_HEADER_SIZE) return -1;
    out.crc     = unpack_be64(buf + BTS_E2E_OFF_CRC);
    out.length  = bts_unpack_be32(buf + BTS_E2E_OFF_LENGTH);
    out.counter = bts_unpack_be32(buf + BTS_E2E_OFF_COUNTER);
    out.data_id = bts_unpack_be32(buf + BTS_E2E_OFF_DATA_ID);
    return 0;
}

E2eStatus e2e_validate(const uint8_t* buf, size_t len, E2eHeader& out) {
    if (e2e_parse(buf, len, out) < 0) return E2eStatus::kTooShort;

    // CRC spans the E2E header tail (Length+Counter+DataID) plus the payload
    uint64_t crc = bts_e2e_crc64_update(BTS_E2E_CRC64_INIT,
                                        buf + BTS_E2E_OFF_LENGTH,
                                        len - BTS_E2E_OFF_LENGTH);
    crc ^= BTS_E2E_CRC64_XOROUT;

    return (crc == out.crc) ? E2eStatus::kOk : E2eStatus::kCrcMismatch;
}

} // namespace afi
