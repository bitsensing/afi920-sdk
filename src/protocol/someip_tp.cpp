// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/someip_tp.h"
#include "util/logger.h"
#include <cstring>
#include <algorithm>

namespace afi {

bool SomeipTpReassembler::Feed(const uint8_t* segment_data, size_t data_len,
                                uint32_t offset_bytes, bool more_segments) {
    if (!segment_data || data_len == 0) return false;

    in_progress_ = true;

    // Ensure buffer is large enough
    size_t end = offset_bytes + data_len;
    if (end > buffer_.size()) {
        buffer_.resize(end);
    }

    // Copy segment data at the correct offset
    std::memcpy(buffer_.data() + offset_bytes, segment_data, data_len);

    if (!more_segments) {
        // Last segment received — message is complete
        total_size_ = end;
        complete_ = true;
        in_progress_ = false;
        AFI_LOG_DEBUG("TP reassembly complete: %zu bytes", total_size_);
        return true;
    }

    return false;
}

void SomeipTpReassembler::Reset() {
    // Don't deallocate — keep buffer for reuse
    total_size_ = 0;
    in_progress_ = false;
    complete_ = false;
}

} // namespace afi
