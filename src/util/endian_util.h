/**
 * @file endian_util.h
 * @brief Endianness unpack helpers (complement to bts_iso23150.h pack helpers)
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstdint>
#include <cstring>

namespace afi {
namespace detail {

// ─── Big-endian unpack (for SOME/IP header) ───

inline uint16_t unpack_be16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t unpack_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

// ─── Little-endian unpack (for ISO 23150 payload) ───

inline uint16_t unpack_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t unpack_le32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]))       |
           (static_cast<uint32_t>(p[1]) << 8)  |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t unpack_le64(const uint8_t* p) {
    return (static_cast<uint64_t>(p[0]))       |
           (static_cast<uint64_t>(p[1]) << 8)  |
           (static_cast<uint64_t>(p[2]) << 16) |
           (static_cast<uint64_t>(p[3]) << 24) |
           (static_cast<uint64_t>(p[4]) << 32) |
           (static_cast<uint64_t>(p[5]) << 40) |
           (static_cast<uint64_t>(p[6]) << 48) |
           (static_cast<uint64_t>(p[7]) << 56);
}

inline float unpack_le_f32(const uint8_t* p) {
    uint32_t u = unpack_le32(p);
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

// ─── Big-endian pack (for SOME/IP header serialization) ───

inline void pack_be16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

inline void pack_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

} // namespace detail
} // namespace afi
