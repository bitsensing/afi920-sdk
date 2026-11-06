// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/someip_tp.h"
#include "util/logger.h"
#include <cstring>
#include <algorithm>

namespace afi {

bool SomeipTpReassembler::Feed(const uint8_t* segment_data, size_t data_len,
                                uint32_t offset_bytes, bool more_segments,
                                uint16_t session_id) {
    if (!segment_data || data_len == 0) return false;

    if (data_len > max_segment_size_ ||
        (more_segments && (data_len % 16u) != 0u)) {
        AFI_LOG_WARN("Invalid TP segment size: len=%zu more=%d max=%zu",
                     data_len, more_segments ? 1 : 0, max_segment_size_);
        Reset();
        return false;
    }

    if (complete_) Reset();
    if (has_session_ && session_id_ != session_id) {
        AFI_LOG_WARN("TP session changed before completion: %u -> %u",
                     static_cast<unsigned>(session_id_),
                     static_cast<unsigned>(session_id));
        Reset();
    }
    if (!has_session_) {
        session_id_ = session_id;
        has_session_ = true;
    }

    const size_t begin = static_cast<size_t>(offset_bytes);
    if (begin > max_payload_size_ || data_len > max_payload_size_ - begin) {
        AFI_LOG_WARN("TP segment exceeds reassembly limit: offset=%zu len=%zu max=%zu",
                     begin, data_len, max_payload_size_);
        Reset();
        return false;
    }
    const size_t end = begin + data_len;
    if (expected_size_ != 0 && end > expected_size_) {
        AFI_LOG_WARN("TP segment exceeds declared final size: %zu > %zu",
                     end, expected_size_);
        Reset();
        return false;
    }

    in_progress_ = true;

    // Ensure buffer is large enough
    if (end > buffer_.size()) {
        buffer_.resize(end);
    }

    // Copy segment data at the correct offset
    std::memcpy(buffer_.data() + begin, segment_data, data_len);
    AddCoveredRange(begin, end);

    if (!more_segments) {
        if (expected_size_ != 0 && expected_size_ != end) {
            AFI_LOG_WARN("TP conflicting final sizes: %zu != %zu", end, expected_size_);
            Reset();
            return false;
        }
        expected_size_ = end;
        if (!covered_ranges_.empty() && covered_ranges_.back().second > expected_size_) {
            AFI_LOG_WARN("TP buffered data exceeds final size");
            Reset();
            return false;
        }
    }

    if (IsFullyCovered()) {
        total_size_ = expected_size_;
        complete_ = true;
        in_progress_ = false;
        AFI_LOG_DEBUG("TP reassembly complete: %zu bytes", total_size_);
        return true;
    }

    return false;
}

void SomeipTpReassembler::AddCoveredRange(size_t begin, size_t end) {
    covered_ranges_.emplace_back(begin, end);
    std::sort(covered_ranges_.begin(), covered_ranges_.end());

    std::vector<std::pair<size_t, size_t>> merged;
    merged.reserve(covered_ranges_.size());
    for (const auto& range : covered_ranges_) {
        if (merged.empty() || range.first > merged.back().second) {
            merged.push_back(range);
        } else {
            merged.back().second = (std::max)(merged.back().second, range.second);
        }
    }
    covered_ranges_.swap(merged);
}

bool SomeipTpReassembler::IsFullyCovered() const {
    return expected_size_ != 0 && covered_ranges_.size() == 1 &&
           covered_ranges_[0].first == 0 &&
           covered_ranges_[0].second >= expected_size_;
}

void SomeipTpReassembler::Reset() {
    // Don't deallocate — keep buffer for reuse
    covered_ranges_.clear();
    expected_size_ = 0;
    total_size_ = 0;
    session_id_ = 0;
    has_session_ = false;
    in_progress_ = false;
    complete_ = false;
}

} // namespace afi
