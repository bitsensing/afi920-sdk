/**
 * @file someip_tp.h
 * @brief SOME/IP-TP segment reassembly
 *
 * Handles reassembly of segmented SOME/IP messages.
 * RDI with >17 detections triggers TP segmentation.
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace afi {

class SomeipTpReassembler {
public:
    SomeipTpReassembler() = default;

    /**
     * @brief Feed a TP segment
     * @param segment_data Payload data of this segment (after TP header)
     * @param data_len Length of segment data
     * @param offset_bytes Byte offset in reassembled payload
     * @param more_segments True if more segments follow
     * @return true if message is now complete
     */
    bool Feed(const uint8_t* segment_data, size_t data_len,
              uint32_t offset_bytes, bool more_segments);

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
    std::vector<uint8_t> buffer_;
    size_t total_size_ = 0;
    bool in_progress_ = false;
    bool complete_ = false;
};

} // namespace afi
