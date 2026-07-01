/**
 * @file e2e.h
 * @brief AUTOSAR E2E Profile 7 header parsing/validation (receive side)
 *
 * RDI/SHII/SPI/CSII payloads carry a 20-byte E2E header between the
 * SOME/IP header and the ISO 23150 payload. All E2E fields are
 * Big-endian (profile-mandated), unlike the LE ISO 23150 payload.
 *
 * Layout: CRC-64/XZ (8B) | Length (4B) | Counter (4B) | Data ID (4B)
 * The CRC protects bytes [8..end) of the E2E header + payload frame.
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstdint>
#include <cstddef>

namespace afi {

/// Parsed E2E Profile 7 header fields
struct E2eHeader {
    uint64_t crc = 0;       ///< CRC-64/XZ over header tail + payload
    uint32_t length = 0;    ///< Protected ISO 23150 payload length
    uint32_t counter = 0;   ///< Per-stream sequence counter
    uint32_t data_id = 0;   ///< E2E Data ID (= stream MessageID32)
};

enum class E2eStatus {
    kOk = 0,        ///< Header parsed, CRC valid
    kCrcMismatch,   ///< Header parsed, CRC does not match
    kTooShort,      ///< Buffer shorter than the 20B E2E header
};

/**
 * @brief Parse the 20B E2E header without CRC verification
 * @param buf  Frame starting at the E2E header
 * @param len  Frame length (header + payload)
 * @param[out] out Parsed header fields
 * @return 0 on success, negative if buffer too short
 */
int e2e_parse(const uint8_t* buf, size_t len, E2eHeader& out);

/**
 * @brief Parse the 20B E2E header and verify the CRC-64/XZ
 * @param buf  Frame starting at the E2E header
 * @param len  Frame length (header + payload)
 * @param[out] out Parsed header fields (filled on kOk and kCrcMismatch)
 */
E2eStatus e2e_validate(const uint8_t* buf, size_t len, E2eHeader& out);

} // namespace afi
