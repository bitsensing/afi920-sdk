/**
 * @file someip_tp.h
 * @brief SOME/IP-TP segment reassembly
 *
 * Handles reassembly of segmented SOME/IP messages. UDP payloads larger than
 * the 1392-byte transport chunk (notably RDI) use TP segmentation.
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>

namespace afi {

class SomeipTpReassembler {
public:
    explicit SomeipTpReassembler(size_t max_payload_size = 4 * 1024 * 1024,
                                 size_t max_segment_size = 1392)
        : max_payload_size_(max_payload_size),
          max_segment_size_(max_segment_size) {}

    /**
     * @brief Feed a TP segment
     * @param segment_data Payload data of this segment (after TP header)
     * @param data_len Length of segment data
     * @param offset_bytes Byte offset in reassembled payload
     * @param more_segments True if more segments follow
     * @param session_id SOME/IP session identifying the logical message
     * @return true if message is now complete
     */
    bool Feed(const uint8_t* segment_data, size_t data_len,
              uint32_t offset_bytes, bool more_segments,
              uint16_t session_id = 0);

    /**
     * @brief Get reassembled payload
     * @note Only valid after Feed() returns true
     */
    const uint8_t* GetPayload() const { return buffer_.data(); }
    size_t GetPayloadSize() const { return total_size_; }

    /**
     * @brief Reset for next message
     */
    void Reset();

    /**
     * @brief Check if assembly is in progress
     */
    bool InProgress() const { return in_progress_; }

private:
    void AddCoveredRange(size_t begin, size_t end);
    bool IsFullyCovered() const;

    std::vector<uint8_t> buffer_;
    std::vector<std::pair<size_t, size_t>> covered_ranges_;
    size_t max_payload_size_;
    size_t max_segment_size_;
    size_t expected_size_ = 0;
    size_t total_size_ = 0;
    uint16_t session_id_ = 0;
    bool has_session_ = false;
    bool in_progress_ = false;
    bool complete_ = false;
};

} // namespace afi
