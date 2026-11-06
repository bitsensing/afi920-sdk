/**
 * @file e2e.h
 * @brief E2E communication protection header (20B, CRC-32) — receive and send
 *
 * RDI/SHII/SPI/CSII and the Config API carry a 20-byte E2E header right
 * after the SOME/IP header. All E2E fields are Big-endian, unlike the LE
 * ISO 23150 payload.
 *
 * Layout: CRC high (4B, 0) | CRC-32 (4B) | Length (4B) | Counter (4B) | Data ID (4B)
 * The IEEE 802.3 CRC-32 covers the body (Length bytes) only; the header is
 * not part of the protected region (interface v1.1.0, System Spec §7).
 *
 * Interface v1.0.x (firmware 3.0.0 to 3.1.0) keeps the field layout but puts
 * a CRC-64/XZ in the first 8 bytes, computed over Length, Counter, Data ID
 * and the body. A non-zero high word selects that check; it is accepted on
 * receive only and is limited to this SDK release.
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include <cstdint>
#include <cstddef>

namespace afi {

/// Parsed E2E header fields
struct E2eHeader {
    uint32_t crc_high = 0;  ///< Upper CRC word: 0 on v1.1.0, CRC-64 high on v1.0.x
    uint32_t crc = 0;       ///< IEEE 802.3 CRC-32 over the body (v1.1.0)
    uint32_t length = 0;    ///< Body byte count
    uint32_t counter = 0;   ///< Sequence counter
    uint32_t data_id = 0;   ///< E2E Data ID (0x6000 << 16 | channel id)
    /// kV1_1 when crc_high is 0, kV1_0 otherwise
    AfiInterfaceGeneration generation = AfiInterfaceGeneration::kUnknown;
};

enum class E2eStatus {
    kOk = 0,          ///< Header parsed and all requested checks passed
    kCrcMismatch,     ///< CRC-32 (or v1.0.x CRC-64) does not match
    kTooShort,        ///< Buffer shorter than the 20B E2E header
    kLengthMismatch,  ///< Header Length does not match the body length
    kDataIdMismatch,  ///< Data ID does not match the expected channel
    kCrcZero,         ///< CRC field is 0, which never counts as a pass
};

/// Short name for log messages.
const char* e2e_status_name(E2eStatus status);

/**
 * @brief Parse the 20B E2E header without verification
 * @param buf  Frame starting at the E2E header
 * @param len  Frame length (header + body)
 * @param[out] out Parsed header fields
 * @return 0 on success, negative if buffer too short
 */
int e2e_parse(const uint8_t* buf, size_t len, E2eHeader& out);

/**
 * @brief Parse the E2E header and validate length, optional Data ID, and CRC
 * @param buf  Frame starting at the E2E header
 * @param len  Frame length (header + body)
 * @param[out] out Parsed header fields (filled unless the frame is too short)
 * @param expected_data_id Expected Data ID, or 0 to skip this check
 * @param check_crc Recompute the CRC-32 (false checks structure only)
 */
E2eStatus e2e_validate(const uint8_t* buf, size_t len, E2eHeader& out,
                       uint32_t expected_data_id = 0, bool check_crc = true);

/// CRC-64/XZ (reflected 0x42F0E1EBA9EA3693, init and xorout all ones), the
/// interface v1.0.x E2E CRC. KAT: "123456789" -> 0x995DC9BBDF1939FA.
uint64_t e2e_crc64_xz(const uint8_t* data, size_t len);

/**
 * @brief Write a complete 20B E2E header (CRC computed) for an outbound body
 * @param out      20-byte destination
 * @param data_id  E2E Data ID
 * @param counter  Sequence counter
 * @param body     Body bytes that follow the header on the wire
 * @param body_len Body length
 */
void e2e_write_header(uint8_t* out, uint32_t data_id, uint32_t counter,
                      const uint8_t* body, size_t body_len);

} // namespace afi
