// recording.hpp — .btsrec3 recorder + replay reader for the C++ 3D viewer.
//
// A recording is two files sharing a stem:
//   <stem>.btsrec3   binary log of parsed frames (host byte order)
//   <stem>.json      sidecar metadata: per-sensor pose, ip, transport, count
//
// Detections are stored in the SENSOR frame (radial/azimuth/elevation + v/rcs/
// snr) so replay re-applies the recorded mounting pose exactly like live
// capture. (This is a C++-native format; it is NOT the Python viewer's
// .btsrec, which stores raw ISO payloads.)
//
// Depends only on the standard library + nlohmann/json (vendored in the repo).
//
// Copyright (c) 2026 bitsensing Inc.
#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "geometry.hpp"

namespace viewer {

enum class StreamKind : uint8_t { Rdi = 0, Shii = 1, Spi = 2 };

struct RdiRecord {
    uint32_t counter = 0;
    uint64_t ts = 0;
    // Parallel arrays, all length n (sensor-frame detection fields).
    std::vector<float> r, az, el, v, rcs, snr;
    uint32_t size() const { return static_cast<uint32_t>(r.size()); }
};

struct ShiiRecord {
    uint32_t counter = 0;
    uint8_t defect = 0, reason = 0, supply = 0, temp = 0, time_sync = 0;
    std::vector<uint8_t> modes;
    std::vector<uint8_t> cal;  // calibration statuses
};

struct SpiFov {
    float az0 = 0, az1 = 0, el0 = 0, el1 = 0;
    uint8_t blockage = 0;
};

struct SpiRecord {
    uint32_t counter = 0;
    float pose[8] = {};  // ox, oy, oz, yaw, pitch, roll, yaw_err, pitch_err (rad)
    std::vector<SpiFov> fov;
};

struct Record {
    double host_ts = 0.0;
    StreamKind kind = StreamKind::Rdi;
    int sensor_idx = 0;
    RdiRecord rdi;    // populated when kind == Rdi
    ShiiRecord shii;  // populated when kind == Shii
    SpiRecord spi;    // populated when kind == Spi
};

struct RecSensorMeta {
    int index = 0;
    std::string ip;
    int port = 30509;
    std::string model;
    std::string serial;
    Vec3 offset;
    float yaw = 0, pitch = 0, roll = 0;
};

struct RecMeta {
    std::string kind = "live";
    std::string transport = "tcp";
    double started_unix = 0.0;
    std::vector<RecSensorMeta> sensors;
    uint64_t record_count = 0;
};

// Thread-safe append-only recorder.
class Recorder {
public:
    Recorder(const std::string& path, const RecMeta& meta);  // throws on open failure
    ~Recorder();

    void writeRdi(double host_ts, int idx, const RdiRecord& rec);
    void writeShii(double host_ts, int idx, const ShiiRecord& rec);
    void writeSpi(double host_ts, int idx, const SpiRecord& rec);
    void close();

    uint64_t recordCount() const { return count_; }
    const std::string& path() const { return path_; }

private:
    void writeSidecar();

    std::string path_;
    std::string sidecar_;
    std::ofstream f_;
    std::mutex mtx_;
    RecMeta meta_;
    uint64_t count_ = 0;
    bool closed_ = false;
};

// Reads an entire recording into memory in capture order.
class ReplayReader {
public:
    explicit ReplayReader(const std::string& path);  // throws on error
    const RecMeta& meta() const { return meta_; }
    const std::vector<Record>& records() const { return records_; }

private:
    std::string path_;
    std::string sidecar_;
    RecMeta meta_;
    std::vector<Record> records_;
};

// Replace the file extension with `ext` (e.g. ".json"); appends if none.
std::string withExtension(const std::string& path, const std::string& ext);

}  // namespace viewer
