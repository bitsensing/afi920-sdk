/**
 * @file rdi_assembler.h
 * @brief Interface v1.2.0 RDI scan assembly from header and chunk messages
 *
 * One scan is header A (Event 0x8002), chunk k (Event 0x8100 + k) for
 * k = 0..K-1 and header B (Event 0x8002 again), all under one E2E Counter.
 * The assembler follows Interface Overview section 3.2.3:
 *   - A message whose E2E Counter equals one of the last two closed scans is
 *     late and is dropped without opening or closing a scan.
 *   - A message with another E2E Counter closes the open scan and opens one.
 *   - The first 0x8002 of a scan is header A, the second header B.
 *   - A scan closes when one header and ChunkCount chunks are in, on header B,
 *     on a message of another E2E Counter, or 100 ms after its first message.
 *   - A closed scan without any header is dropped; one with missing chunks is
 *     delivered with the detections that arrived.
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include "protocol/someip.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace afi {

class RdiScanAssembler {
public:
    /// Receives each closed scan. The assembler fills message, detections,
    /// chunk counts, e2e_counter, e2e_valid (every message used passed),
    /// recv_timestamp_ns (last message) and generation (kV1_2).
    using Emit = std::function<void(AfiRdiFrame&)>;

    static constexpr uint64_t kDefaultTimeoutNs = 100000000ull;  // 100 ms

    explicit RdiScanAssembler(uint64_t timeout_ns = kDefaultTimeoutNs)
        : timeout_ns_(timeout_ns) {}

    /// Header message body (after the E2E header) of Event 0x8002.
    void OnHeader(uint32_t e2e_counter, bool e2e_valid,
                  const uint8_t* body, size_t len, uint64_t now_ns,
                  const Emit& emit);

    /// Chunk message body (after the E2E header) of Event 0x8100 + chunk_index.
    void OnChunk(uint32_t e2e_counter, bool e2e_valid, uint16_t chunk_index,
                 const uint8_t* body, size_t len, uint64_t now_ns,
                 const Emit& emit);

    /// Close the open scan once its first message is older than the timeout.
    void Poll(uint64_t now_ns, const Emit& emit);

    /// Drop the open scan and the closed-scan history.
    void Reset();

    uint64_t ScansWithoutHeader() const { return scans_without_header_; }
    uint64_t LateMessages() const { return late_messages_; }

private:
    struct Chunk {
        bool present = false;
        bool e2e_valid = false;
        std::vector<uint8_t> body;
    };

    // section 3.2.3 rules (1) to (3); false when the message is late.
    bool Admit(uint32_t e2e_counter, uint64_t now_ns, const Emit& emit);
    bool IsComplete() const;
    void Close(const Emit& emit);

    uint64_t timeout_ns_;

    bool     open_ = false;
    uint32_t counter_ = 0;
    uint64_t first_ns_ = 0;
    uint64_t last_ns_ = 0;
    int      headers_seen_ = 0;
    bool     header_valid_ = false;
    std::vector<uint8_t> header_;        // body of the header used (empty: none)
    std::array<Chunk, kRdiMaxChunks> chunks_{};

    uint32_t closed_[2] = {0, 0};        // E2E Counters of the last two closed scans
    size_t   closed_count_ = 0;

    uint64_t scans_without_header_ = 0;
    uint64_t late_messages_ = 0;
};

} // namespace afi
