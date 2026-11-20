/**
 * @file test_rdi_assembler.cpp
 * @brief Unit test: interface v1.2.0 RDI scan assembly (Interface Overview section 3.2.3)
 *
 * Header and chunk bodies are cut from a single-message payload as
 * 03_RDI_Radar_Detections section 1 lays them out.
 */
#include "protocol/rdi_assembler.h"
#include "util/endian_util.h"
#include "test_common.h"
#include <cstring>
#include <vector>

using namespace afi;

static constexpr uint64_t kMs = 1000000ull;

static void pack_le16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void pack_le32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF;
}
static void pack_f32(uint8_t* p, float f) {
    uint32_t u; std::memcpy(&u, &f, 4); pack_le32(p, u);
}

// Single-message RDI payload (Detection Header + n detections) to split.
static std::vector<uint8_t> make_payload(uint16_t n, uint32_t message_counter) {
    std::vector<uint8_t> buf(BTS_RDI_HEADER_SIZE + n * BTS_RDI_DETECTION_SIZE, 0);
    buf[0] = 1; buf[1] = 2; buf[2] = 0;
    buf[3] = 0x08;
    buf[4] = 1; buf[5] = 1;
    for (int i = 0; i < 8; i++) buf[6 + i] = static_cast<uint8_t>(0x10 + i);
    pack_le32(&buf[14], message_counter);
    pack_f32(&buf[24], -111.11f);
    pack_f32(&buf[28], 55.56f);
    pack_le16(&buf[32], 2048);
    pack_le16(&buf[34], n);
    for (uint16_t i = 0; i < n; i++) {
        uint8_t* d = buf.data() + BTS_RDI_HEADER_SIZE + i * BTS_RDI_DETECTION_SIZE;
        d[0] = 90;
        pack_le16(d + 1, i);                       // DetectionID = index
        pack_f32(d + 19, 1.0f + i);                // radial distance
    }
    return buf;
}

struct Scan {
    std::vector<uint8_t> header;
    std::vector<std::vector<uint8_t>> chunks;
};

// Header body: Detection Header + List Layout. Chunk k: Chunk Header
// (MessageCounter, FirstEntityIndex, EntityCount) + its detections.
static Scan split(const std::vector<uint8_t>& payload) {
    constexpr uint16_t kPerChunk = BTS_RDI_ENTITIES_PER_CHUNK;
    const uint16_t n = detail::unpack_le16(payload.data() + 34);
    const uint16_t k_count = static_cast<uint16_t>((n + kPerChunk - 1) / kPerChunk);
    Scan s;
    s.header.assign(payload.begin(), payload.begin() + BTS_RDI_HEADER_SIZE);
    s.header.resize(BTS_RDI_HEADER_MSG_SIZE);
    pack_le16(&s.header[BTS_RDI_HEADER_SIZE], kPerChunk);
    pack_le16(&s.header[BTS_RDI_HEADER_SIZE + 2], k_count);
    for (uint16_t k = 0; k < k_count; k++) {
        const uint16_t first = static_cast<uint16_t>(k * kPerChunk);
        const uint16_t count = static_cast<uint16_t>(
            n - first < kPerChunk ? n - first : kPerChunk);
        std::vector<uint8_t> c(BTS_RDI_CHUNK_HEADER_SIZE);
        std::memcpy(c.data(), payload.data() + 14, 4);  // MessageCounter
        pack_le16(&c[4], first);
        pack_le16(&c[6], count);
        const auto* d = payload.data() + BTS_RDI_HEADER_SIZE + first * BTS_RDI_DETECTION_SIZE;
        c.insert(c.end(), d, d + count * BTS_RDI_DETECTION_SIZE);
        s.chunks.push_back(c);
    }
    return s;
}

struct Sink {
    std::vector<AfiRdiFrame> frames;
    RdiScanAssembler::Emit emit() {
        return [this](AfiRdiFrame& f) { frames.push_back(f); };
    }
};

static void feed_header(RdiScanAssembler& a, Sink& sink, uint32_t counter,
                        const Scan& s, uint64_t t, bool valid = true) {
    a.OnHeader(counter, valid, s.header.data(), s.header.size(), t, sink.emit());
}
static void feed_chunk(RdiScanAssembler& a, Sink& sink, uint32_t counter,
                       const Scan& s, uint16_t k, uint64_t t, bool valid = true) {
    a.OnChunk(counter, valid, k, s.chunks[k].data(), s.chunks[k].size(), t, sink.emit());
}

static bool detections_in_order(const AfiRdiFrame& f, uint16_t first, uint16_t count) {
    if (f.message.num_detections != count || f.message.detections == nullptr) return false;
    for (uint16_t i = 0; i < count; i++) {
        if (f.message.detections[i].detection_id != first + i) return false;
        if (f.message.detections[i].position_radial_distance != 1.0f + first + i) return false;
    }
    return true;
}

TEST(full_scan_closes_on_the_last_chunk_and_header_b_is_late) {
    const auto payload = make_payload(60, 500);
    const Scan s = split(payload);
    CHECK(s.chunks.size() == 3);
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 7, s, 0);
    for (uint16_t k = 0; k < 3; k++) feed_chunk(a, sink, 7, s, k, 1 * kMs);
    CHECK(sink.frames.size() == 1);
    feed_header(a, sink, 7, s, 2 * kMs);  // header B after completion
    CHECK(sink.frames.size() == 1);
    CHECK(a.LateMessages() == 1);

    const AfiRdiFrame& f = sink.frames[0];
    CHECK(f.e2e_counter == 7);
    CHECK(f.e2e_valid);
    CHECK(f.generation == AfiInterfaceGeneration::kV1_2);
    CHECK(f.chunk_count == 3 && f.chunks_received == 3);
    CHECK(f.announced_detections == 60);
    CHECK(f.IsComplete());
    CHECK(f.message.message_counter == 500);
    CHECK(f.message.interface_version.minor == 2);
    CHECK(f.message.ambiguity_domain.radial_velocity_begin == -111.11f);
    CHECK(detections_in_order(f, 0, 60));
    PASS("full_scan_closes_on_the_last_chunk_and_header_b_is_late");
}

TEST(maximum_scan_uses_76_chunks) {
    const auto payload = make_payload(2048, 1);
    const Scan s = split(payload);
    CHECK(s.chunks.size() == 76);
    CHECK(s.chunks[75].size() == BTS_RDI_CHUNK_HEADER_SIZE + 23 * BTS_RDI_DETECTION_SIZE);
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 1, s, 0);
    for (uint16_t k = 0; k < 76; k++) feed_chunk(a, sink, 1, s, k, 0);
    CHECK(sink.frames.size() == 1);
    CHECK(sink.frames[0].chunk_count == 76 && sink.frames[0].IsComplete());
    CHECK(detections_in_order(sink.frames[0], 0, 2048));
    PASS("maximum_scan_uses_76_chunks");
}

TEST(zero_detections_close_on_header_a) {
    const Scan s = split(make_payload(0, 9));
    CHECK(s.chunks.empty());
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 3, s, 0);
    CHECK(sink.frames.size() == 1);
    CHECK(sink.frames[0].chunk_count == 0);
    CHECK(sink.frames[0].message.num_detections == 0);
    CHECK(sink.frames[0].message.detections == nullptr);
    CHECK(sink.frames[0].IsComplete());
    feed_header(a, sink, 3, s, 1 * kMs);
    CHECK(sink.frames.size() == 1);
    PASS("zero_detections_close_on_header_a");
}

TEST(lost_header_a_is_replaced_by_header_b) {
    const Scan s = split(make_payload(30, 4));
    RdiScanAssembler a;
    Sink sink;
    feed_chunk(a, sink, 11, s, 0, 0);
    feed_chunk(a, sink, 11, s, 1, 0);
    CHECK(sink.frames.empty());
    feed_header(a, sink, 11, s, 1 * kMs);  // the only header: completes the scan
    CHECK(sink.frames.size() == 1);
    CHECK(sink.frames[0].IsComplete());
    CHECK(detections_in_order(sink.frames[0], 0, 30));
    PASS("lost_header_a_is_replaced_by_header_b");
}

TEST(missing_chunk_is_delivered_on_header_b) {
    const Scan s = split(make_payload(60, 4));
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 20, s, 0);
    feed_chunk(a, sink, 20, s, 0, 0);
    feed_chunk(a, sink, 20, s, 2, 0);
    CHECK(sink.frames.empty());
    feed_header(a, sink, 20, s, 1 * kMs);  // header B
    CHECK(sink.frames.size() == 1);
    const AfiRdiFrame& f = sink.frames[0];
    CHECK(!f.IsComplete());
    CHECK(f.chunk_count == 3 && f.chunks_received == 2);
    CHECK(f.announced_detections == 60);
    CHECK(f.message.num_detections == 27 + 6);
    CHECK(f.message.detections[26].detection_id == 26);
    CHECK(f.message.detections[27].detection_id == 54);
    PASS("missing_chunk_is_delivered_on_header_b");
}

TEST(scan_without_header_is_dropped) {
    const Scan s = split(make_payload(30, 4));
    const Scan next = split(make_payload(0, 5));
    RdiScanAssembler a;
    Sink sink;
    feed_chunk(a, sink, 30, s, 0, 0);
    feed_chunk(a, sink, 30, s, 1, 0);
    feed_header(a, sink, 31, next, 1 * kMs);  // another counter closes scan 30
    CHECK(a.ScansWithoutHeader() == 1);
    CHECK(sink.frames.size() == 1);
    CHECK(sink.frames[0].e2e_counter == 31);
    PASS("scan_without_header_is_dropped");
}

TEST(timeout_closes_a_scan_without_header_b) {
    const Scan s = split(make_payload(60, 4));
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 40, s, 0);
    feed_chunk(a, sink, 40, s, 0, 0);
    a.Poll(99 * kMs, sink.emit());
    CHECK(sink.frames.empty());
    a.Poll(100 * kMs, sink.emit());
    CHECK(sink.frames.size() == 1);
    CHECK(sink.frames[0].chunks_received == 1);
    feed_chunk(a, sink, 40, s, 1, 101 * kMs);  // after closing: late
    CHECK(sink.frames.size() == 1);
    CHECK(a.LateMessages() == 1);
    PASS("timeout_closes_a_scan_without_header_b");
}

TEST(new_counter_closes_the_open_scan) {
    const Scan s1 = split(make_payload(60, 1));
    const Scan s2 = split(make_payload(10, 2));
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 50, s1, 0);
    feed_chunk(a, sink, 50, s1, 0, 0);
    feed_header(a, sink, 51, s2, 1 * kMs);
    CHECK(sink.frames.size() == 1);
    CHECK(sink.frames[0].e2e_counter == 50 && sink.frames[0].chunks_received == 1);
    feed_chunk(a, sink, 51, s2, 0, 1 * kMs);
    CHECK(sink.frames.size() == 2);
    CHECK(sink.frames[1].e2e_counter == 51 && sink.frames[1].IsComplete());
    PASS("new_counter_closes_the_open_scan");
}

TEST(late_messages_of_the_last_two_scans_are_dropped) {
    const Scan s = split(make_payload(0, 1));
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 60, s, 0);
    feed_header(a, sink, 61, s, 0);
    feed_header(a, sink, 62, s, 0);
    CHECK(sink.frames.size() == 3);
    feed_header(a, sink, 61, s, 0);  // late
    feed_header(a, sink, 62, s, 0);  // late
    CHECK(sink.frames.size() == 3);
    CHECK(a.LateMessages() == 2);
    feed_header(a, sink, 60, s, 0);  // older than two scans: a new scan (restart)
    CHECK(sink.frames.size() == 4);
    PASS("late_messages_of_the_last_two_scans_are_dropped");
}

TEST(chunk_with_another_message_counter_is_skipped) {
    const Scan s = split(make_payload(30, 100));
    Scan other = split(make_payload(30, 101));
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 70, s, 0);
    feed_chunk(a, sink, 70, s, 0, 0);
    feed_chunk(a, sink, 70, other, 1, 0);  // MessageCounter 101 != 100
    CHECK(sink.frames.size() == 1);
    CHECK(sink.frames[0].chunks_received == 1);
    CHECK(sink.frames[0].message.num_detections == 27);
    PASS("chunk_with_another_message_counter_is_skipped");
}

TEST(failed_e2e_check_marks_the_scan) {
    const Scan s = split(make_payload(30, 4));
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 80, s, 0);
    feed_chunk(a, sink, 80, s, 0, 0, false);
    feed_chunk(a, sink, 80, s, 1, 0);
    CHECK(sink.frames.size() == 1);
    CHECK(!sink.frames[0].e2e_valid);
    CHECK(sink.frames[0].IsComplete());
    PASS("failed_e2e_check_marks_the_scan");
}

TEST(reset_forgets_open_and_closed_scans) {
    const Scan s = split(make_payload(30, 4));
    RdiScanAssembler a;
    Sink sink;
    feed_header(a, sink, 90, s, 0);
    feed_chunk(a, sink, 90, s, 0, 0);
    a.Reset();
    a.Poll(1000 * kMs, sink.emit());
    CHECK(sink.frames.empty());
    feed_header(a, sink, 90, s, 0);
    feed_chunk(a, sink, 90, s, 0, 0);
    feed_chunk(a, sink, 90, s, 1, 0);
    CHECK(sink.frames.size() == 1);
    PASS("reset_forgets_open_and_closed_scans");
}


TEST_MAIN("RDI Scan Assembler Tests")
