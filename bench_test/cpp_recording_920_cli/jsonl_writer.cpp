// jsonl_writer.cpp — implementation of the CLI recorder's JSONL writer.
//
// Copyright (c) 2026 bitsensing Inc.
#include "jsonl_writer.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace afirec {

namespace {
using nlohmann::json;
constexpr char kFormat[] = "afi-radar-jsonl";
constexpr int kVersion = 1;
}  // namespace

JsonlWriter::JsonlWriter(const std::string& path, const Header& header)
    : path_(path) {
    f_.open(path_, std::ios::binary | std::ios::trunc);
    if (!f_) throw std::runtime_error("cannot open recording file: " + path_);
    try {
        json h;
        h["type"] = "header";
        h["format"] = kFormat;
        h["version"] = kVersion;
        h["kind"] = header.kind;
        h["transport"] = header.transport;
        h["started_unix"] = header.started_unix;
        h["sensors"] = json::array();
        for (const auto& s : header.sensors) {
            h["sensors"].push_back({
                {"index", s.index},
                {"ip", s.ip},
                {"port", s.port},
                {"model", s.model},
                {"serial", s.serial},
                {"pose", {{"offset_x_m", s.ox}, {"offset_y_m", s.oy},
                          {"offset_z_m", s.oz}, {"yaw_deg", s.yaw},
                          {"pitch_deg", s.pitch}, {"roll_deg", s.roll}}},
            });
        }
        f_ << h.dump() << "\n";
        f_.flush();
        if (!f_) throw std::runtime_error("write failed: " + path_);
    } catch (...) {
        closed_ = true;
        f_.close();
        throw;
    }
}

JsonlWriter::~JsonlWriter() { close(); }

void JsonlWriter::writeRdi(double ts, int idx, uint32_t counter, uint64_t ts_ns,
                           const std::vector<float>& r,
                           const std::vector<float>& az,
                           const std::vector<float>& el,
                           const std::vector<float>& v,
                           const std::vector<float>& rcs,
                           const std::vector<float>& snr) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_) return;
    json j;
    j["t"] = "rdi";
    j["ts"] = ts;
    j["s"] = idx & 0xFF;
    j["counter"] = counter;
    j["ts_ns"] = ts_ns;
    j["n"] = static_cast<uint32_t>(r.size());
    j["claimed"] = static_cast<uint32_t>(r.size());  // SDK pre-parses; claimed == available
    j["r"] = r;
    j["az"] = az;
    j["el"] = el;
    j["v"] = v;
    j["rcs"] = rcs;
    j["snr"] = snr;
    f_ << j.dump() << "\n";
    ++count_;
}

void JsonlWriter::writeShii(double ts, int idx, uint32_t counter, int defect,
                            int reason, int supply, int temp, int time_sync,
                            const std::vector<uint8_t>& modes,
                            const std::vector<uint8_t>& cal) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_) return;
    json j;
    j["t"] = "shii";
    j["ts"] = ts;
    j["s"] = idx & 0xFF;
    j["counter"] = counter;
    j["defect"] = defect;
    j["reason"] = reason;
    j["supply"] = supply;
    j["temp"] = temp;
    j["time_sync"] = time_sync;
    j["modes"] = modes;
    j["cal"] = cal;
    f_ << j.dump() << "\n";
    ++count_;
}

void JsonlWriter::writeSpi(double ts, int idx, uint32_t counter,
                           const float pose[8], const std::vector<FovSeg>& fov) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_) return;
    json j;
    j["t"] = "spi";
    j["ts"] = ts;
    j["s"] = idx & 0xFF;
    j["counter"] = counter;
    j["pose"] = {pose[0], pose[1], pose[2], pose[3],
                 pose[4], pose[5], pose[6], pose[7]};
    j["fov"] = json::array();
    for (const auto& s : fov) {
        j["fov"].push_back({{"az0", s.az0}, {"az1", s.az1}, {"el0", s.el0},
                            {"el1", s.el1}, {"blockage", s.blockage}});
    }
    f_ << j.dump() << "\n";
    ++count_;
}

void JsonlWriter::close() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_) return;
    closed_ = true;
    nlohmann::json foot{{"type", "footer"}, {"record_count", count_}};
    f_ << foot.dump() << "\n";
    f_.flush();
    f_.close();
}

}  // namespace afirec
