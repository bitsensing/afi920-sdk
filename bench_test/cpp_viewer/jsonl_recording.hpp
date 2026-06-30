// jsonl_recording.hpp — unified `afi-radar-jsonl` recorder + replay reader.
//
// This is the C++ viewer's implementation of the line-delimited JSON recording
// format shared with the Python viewer and both CLI recorders (see
// python_recording_920_cli/JSONL_FORMAT.md). It reuses the in-memory record
// structs from recording.hpp, so it is a drop-in replacement for the legacy
// .btsrec3 Recorder / ReplayReader.
//
// A recording is ONE .jsonl file (header line + one record per line); there is
// no separate sidecar. Detections are stored in the sensor frame (r/az/el),
// so replay re-applies the recorded mounting pose exactly like live capture and
// a file is interchangeable with the Python tools.
//
// Copyright (c) 2026 bitsensing Inc.
#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "recording.hpp"  // StreamKind, RdiRecord, ShiiRecord, SpiRecord, Record, RecMeta

namespace viewer {

// Thread-safe append-only JSONL recorder (same interface as the legacy Recorder).
class JsonlRecorder {
public:
    JsonlRecorder(const std::string& path, const RecMeta& meta);  // throws on open failure
    ~JsonlRecorder();

    void writeRdi(double host_ts, int idx, const RdiRecord& rec);
    void writeShii(double host_ts, int idx, const ShiiRecord& rec);
    void writeSpi(double host_ts, int idx, const SpiRecord& rec);
    void close();

    uint64_t recordCount() const { return count_; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::ofstream f_;
    std::mutex mtx_;
    uint64_t count_ = 0;
    bool closed_ = false;
};

// Reads an entire .jsonl recording into memory in capture order.
class JsonlReplayReader {
public:
    explicit JsonlReplayReader(const std::string& path);  // throws on bad header
    const RecMeta& meta() const { return meta_; }
    const std::vector<Record>& records() const { return records_; }
    std::vector<Record> takeRecords() { return std::move(records_); }

private:
    std::string path_;
    RecMeta meta_;
    std::vector<Record> records_;
};

}  // namespace viewer
