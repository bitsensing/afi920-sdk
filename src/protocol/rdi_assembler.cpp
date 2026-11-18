// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/rdi_assembler.h"
#include "protocol/rdi_parser.h"
#include "util/endian_util.h"
#include "util/logger.h"

namespace afi {

namespace {

constexpr size_t kChunkHeaderSize = BTS_RDI_CHUNK_HEADER_SIZE;  // 8
constexpr size_t kDetectionSize = BTS_RDI_DETECTION_SIZE;       // 51

}  // namespace

bool RdiScanAssembler::Admit(uint32_t e2e_counter, uint64_t now_ns,
                             const Emit& emit) {
    for (size_t i = 0; i < closed_count_; ++i) {
        if (closed_[i] == e2e_counter) {
            ++late_messages_;
            return false;
        }
    }
    if (open_ && e2e_counter != counter_) Close(emit);
    if (!open_) {
        open_ = true;
        counter_ = e2e_counter;
        first_ns_ = now_ns;
        headers_seen_ = 0;
        header_valid_ = false;
        header_.clear();
    }
    last_ns_ = now_ns;
    return true;
}

void RdiScanAssembler::OnHeader(uint32_t e2e_counter, bool e2e_valid,
                                const uint8_t* body, size_t len,
                                uint64_t now_ns, const Emit& emit) {
    if (!Admit(e2e_counter, now_ns, emit)) return;
    ++headers_seen_;
    // Header A and header B carry the same body; one is used, preferring a
    // copy that passed its E2E check.
    if (header_.empty() || (!header_valid_ && e2e_valid)) {
        header_.assign(body, body + len);
        header_valid_ = e2e_valid;
    }
    if (headers_seen_ >= 2 || IsComplete()) Close(emit);
}

void RdiScanAssembler::OnChunk(uint32_t e2e_counter, bool e2e_valid,
                               uint16_t chunk_index, const uint8_t* body,
                               size_t len, uint64_t now_ns, const Emit& emit) {
    if (chunk_index >= kRdiMaxChunks) return;
    if (!Admit(e2e_counter, now_ns, emit)) return;
    Chunk& c = chunks_[chunk_index];
    if (!c.present || (!c.e2e_valid && e2e_valid)) {
        c.present = true;
        c.e2e_valid = e2e_valid;
        c.body.assign(body, body + len);
    }
    if (IsComplete()) Close(emit);
}

void RdiScanAssembler::Poll(uint64_t now_ns, const Emit& emit) {
    if (open_ && now_ns - first_ns_ >= timeout_ns_) Close(emit);
}

void RdiScanAssembler::Reset() {
    open_ = false;
    header_.clear();
    for (Chunk& c : chunks_) c.present = false;
    closed_count_ = 0;
}

bool RdiScanAssembler::IsComplete() const {
    if (header_.size() != BTS_RDI_HEADER_MSG_SIZE) return false;
    const uint16_t chunk_count = detail::unpack_le16(header_.data() + BTS_RDI_HEADER_SIZE + 2);
    if (chunk_count > kRdiMaxChunks) return false;
    for (uint16_t k = 0; k < chunk_count; ++k) {
        if (!chunks_[k].present) return false;
    }
    return true;
}

void RdiScanAssembler::Close(const Emit& emit) {
    open_ = false;
    closed_[1] = closed_[0];
    closed_[0] = counter_;
    if (closed_count_ < 2) ++closed_count_;

    AfiRdiFrame frame;
    RdiListLayout layout;
    const bool have_header =
        !header_.empty() &&
        RdiParser::ParseHeaderMessage(header_.data(), header_.size(),
                                      frame.message, layout) == 0;
    if (!have_header || layout.entities_per_chunk == 0 ||
        layout.chunk_count > kRdiMaxChunks) {
        // Without a header the timestamp and Ambiguity Domain are unknown.
        ++scans_without_header_;
        if (scans_without_header_ <= 10 || scans_without_header_ % 1000 == 0) {
            AFI_LOG_WARN("RDI scan (E2E counter %u) dropped: no usable header "
                         "message (%llu so far)", counter_,
                         static_cast<unsigned long long>(scans_without_header_));
        }
        for (Chunk& c : chunks_) c.present = false;
        return;
    }

    bool all_valid = header_valid_;
    uint16_t used = 0;
    for (uint16_t k = 0; k < kRdiMaxChunks; ++k) {
        Chunk& c = chunks_[k];
        if (!c.present) continue;
        c.present = false;
        if (k >= layout.chunk_count) {
            AFI_LOG_WARN("RDI chunk %u beyond ChunkCount %u ignored",
                         k, layout.chunk_count);
            continue;
        }
        const uint8_t* b = c.body.data();
        const size_t len = c.body.size();
        if (len < kChunkHeaderSize) {
            AFI_LOG_WARN("RDI chunk %u shorter than its header: %zu", k, len);
            continue;
        }
        const uint32_t message_counter = detail::unpack_le32(b);
        const uint16_t entity_count = detail::unpack_le16(b + 6);
        if (entity_count > layout.entities_per_chunk ||
            len != kChunkHeaderSize + static_cast<size_t>(entity_count) * kDetectionSize) {
            AFI_LOG_WARN("RDI chunk %u size %zu does not hold EntityCount %u",
                         k, len, entity_count);
            continue;
        }
        if (message_counter != frame.message.message_counter) {
            AFI_LOG_WARN("RDI chunk %u MessageCounter %u differs from the "
                         "header's %u", k, message_counter,
                         frame.message.message_counter);
            continue;
        }
        const size_t base = frame.detections_storage.size();
        frame.detections_storage.resize(base + entity_count);
        for (uint16_t i = 0; i < entity_count; ++i) {
            RdiParser::ParseDetection(b + kChunkHeaderSize + i * kDetectionSize,
                                      frame.detections_storage[base + i]);
        }
        all_valid = all_valid && c.e2e_valid;
        ++used;
    }

    frame.announced_detections = frame.message.num_detections;
    frame.message.num_detections =
        static_cast<uint16_t>(frame.detections_storage.size());
    frame.chunk_count = layout.chunk_count;
    frame.chunks_received = used;
    frame.e2e_counter = counter_;
    frame.e2e_valid = all_valid;
    frame.recv_timestamp_ns = last_ns_;
    frame.generation = AfiInterfaceGeneration::kV1_2;
    frame.RebindStorage();
    emit(frame);
}

} // namespace afi
