// jsonl_writer.hpp — self-contained `afi-radar-jsonl` writer for the C++ CLI
// recorder. Produces the same line-delimited JSON as every other tool in this
// repo (see ../python_recording_920_cli/JSONL_FORMAT.md), so its output replays
// in either viewer.
//
// nlohmann/json is used only in the .cpp, so this header stays light.
//
// Copyright (c) 2026 bitsensing Inc.
#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace afirec {

struct FovSeg {
    float az0 = 0, az1 = 0, el0 = 0, el1 = 0;
    int blockage = 0;
};

struct SensorMeta {
    int index = 0;
    std::string ip;
    int port = 30509;
    std::string model;
    std::string serial;
    // mounting pose: offset (m) + orientation (degrees)
    float ox = 0, oy = 0, oz = 0, yaw = 0, pitch = 0, roll = 0;
};

struct Header {
    std::string kind = "live";
    std::string transport = "tcp";
    double started_unix = 0.0;
    std::vector<SensorMeta> sensors;
};

// Thread-safe append-only JSONL writer (SDK receiver threads share one writer).
class JsonlWriter {
public:
    JsonlWriter(const std::string& path, const Header& header);  // throws on open failure
    ~JsonlWriter();

    void writeRdi(double ts, int idx, uint32_t counter, uint64_t ts_ns,
                  const std::vector<float>& r, const std::vector<float>& az,
                  const std::vector<float>& el, const std::vector<float>& v,
                  const std::vector<float>& rcs, const std::vector<float>& snr);
    void writeShii(double ts, int idx, uint32_t counter, int defect, int reason,
                   int supply, int temp, int time_sync,
                   const std::vector<uint8_t>& modes,
                   const std::vector<uint8_t>& cal);
    void writeSpi(double ts, int idx, uint32_t counter, const float pose[8],
                  const std::vector<FovSeg>& fov);
    void close();

    uint64_t count() const { return count_; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::ofstream f_;
    std::mutex mtx_;
    uint64_t count_ = 0;
    bool closed_ = false;
};

}  // namespace afirec
