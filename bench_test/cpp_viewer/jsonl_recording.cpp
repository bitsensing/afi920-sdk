// jsonl_recording.cpp — implementation of the JSONL recorder / replay reader.
//
// Copyright (c) 2026 bitsensing Inc.
#include "jsonl_recording.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace viewer {

namespace {

using nlohmann::json;

constexpr char kFormat[] = "afi-radar-jsonl";
constexpr int kVersion = 1;
constexpr uint32_t kMaxDetections = 4096;
constexpr uint32_t kMaxFov = 64;

json poseJson(const RecSensorMeta& s) {
    return json{{"offset_x_m", s.offset.x}, {"offset_y_m", s.offset.y},
                {"offset_z_m", s.offset.z}, {"yaw_deg", s.yaw},
                {"pitch_deg", s.pitch}, {"roll_deg", s.roll}};
}

// Read a float vector from a JSON value, tolerating missing / non-array.
std::vector<float> floatVec(const json& j, const char* key) {
    std::vector<float> out;
    auto it = j.find(key);
    if (it != j.end() && it->is_array()) {
        out.reserve(it->size());
        for (const auto& e : *it)
            if (e.is_number()) out.push_back(e.get<float>());
    }
    return out;
}

std::vector<uint8_t> u8Vec(const json& j, const char* key) {
    std::vector<uint8_t> out;
    auto it = j.find(key);
    if (it != j.end() && it->is_array()) {
        out.reserve(it->size());
        for (const auto& e : *it)
            if (e.is_number()) out.push_back(static_cast<uint8_t>(e.get<int>()));
    }
    return out;
}

}  // namespace

// ── JsonlRecorder ──

JsonlRecorder::JsonlRecorder(const std::string& path, const RecMeta& meta)
    : path_(path) {
    f_.open(path_, std::ios::binary | std::ios::trunc);
    if (!f_) throw std::runtime_error("cannot open recording file: " + path_);
    try {
        json h;
        h["type"] = "header";
        h["format"] = kFormat;
        h["version"] = kVersion;
        h["kind"] = meta.kind;
        h["transport"] = meta.transport;
        h["started_unix"] = meta.started_unix;
        h["sensors"] = json::array();
        for (const auto& s : meta.sensors) {
            h["sensors"].push_back({{"index", s.index},
                                    {"ip", s.ip},
                                    {"port", s.port},
                                    {"model", s.model},
                                    {"serial", s.serial},
                                    {"pose", poseJson(s)}});
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

JsonlRecorder::~JsonlRecorder() { close(); }

void JsonlRecorder::writeRdi(double host_ts, int idx, const RdiRecord& rec) {
    const uint32_t n = rec.size();
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_ || n > kMaxDetections) return;
    json j;
    j["t"] = "rdi";
    j["ts"] = host_ts;
    j["s"] = idx & 0xFF;
    j["counter"] = rec.counter;
    j["ts_ns"] = rec.ts;
    j["n"] = n;
    j["claimed"] = n;  // SDK pre-parses; claimed == available for C++ capture
    j["r"] = rec.r;
    j["az"] = rec.az;
    j["el"] = rec.el;
    j["v"] = rec.v;
    j["rcs"] = rec.rcs;
    j["snr"] = rec.snr;
    f_ << j.dump() << "\n";
    ++count_;
}

void JsonlRecorder::writeShii(double host_ts, int idx, const ShiiRecord& rec) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_) return;
    json j;
    j["t"] = "shii";
    j["ts"] = host_ts;
    j["s"] = idx & 0xFF;
    j["counter"] = rec.counter;
    j["defect"] = rec.defect;
    j["reason"] = rec.reason;
    j["supply"] = rec.supply;
    j["temp"] = rec.temp;
    j["time_sync"] = rec.time_sync;
    j["modes"] = rec.modes;
    j["cal"] = rec.cal;
    f_ << j.dump() << "\n";
    ++count_;
}

void JsonlRecorder::writeSpi(double host_ts, int idx, const SpiRecord& rec) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_) return;
    json j;
    j["t"] = "spi";
    j["ts"] = host_ts;
    j["s"] = idx & 0xFF;
    j["counter"] = rec.counter;
    j["pose"] = {rec.pose[0], rec.pose[1], rec.pose[2], rec.pose[3],
                 rec.pose[4], rec.pose[5], rec.pose[6], rec.pose[7]};
    j["fov"] = json::array();
    for (const auto& s : rec.fov) {
        j["fov"].push_back({{"az0", s.az0}, {"az1", s.az1}, {"el0", s.el0},
                            {"el1", s.el1}, {"blockage", s.blockage}});
    }
    f_ << j.dump() << "\n";
    ++count_;
}

void JsonlRecorder::close() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_) return;
    closed_ = true;
    json foot{{"type", "footer"}, {"record_count", count_}};
    f_ << foot.dump() << "\n";
    f_.flush();
    f_.close();
}

// ── JsonlReplayReader ──

JsonlReplayReader::JsonlReplayReader(const std::string& path) : path_(path) {
    std::ifstream f(path_, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open recording: " + path_);

    std::string line;
    if (!std::getline(f, line))
        throw std::runtime_error("empty recording: " + path_);

    json hdr;
    try {
        hdr = json::parse(line);
    } catch (const std::exception&) {
        throw std::runtime_error("not an afi-radar-jsonl recording: " + path_);
    }
    if (!hdr.is_object() || hdr.value("type", std::string()) != "header") {
        throw std::runtime_error(
            "not an afi-radar-jsonl recording (missing header line): " + path_);
    }
    meta_.kind = hdr.value("kind", std::string("replay"));
    meta_.transport = hdr.value("transport", std::string("?"));
    meta_.started_unix = hdr.value("started_unix", 0.0);
    if (hdr.contains("sensors") && hdr["sensors"].is_array()) {
        for (const auto& s : hdr["sensors"]) {
            RecSensorMeta m;
            m.index = s.value("index", 0);
            m.ip = s.value("ip", std::string());
            m.port = s.value("port", 30509);
            m.model = s.value("model", std::string());
            m.serial = s.value("serial", std::string());
            if (s.contains("pose")) {
                const auto& p = s["pose"];
                m.offset = {p.value("offset_x_m", 0.0f),
                            p.value("offset_y_m", 0.0f),
                            p.value("offset_z_m", 0.0f)};
                m.yaw = p.value("yaw_deg", 0.0f);
                m.pitch = p.value("pitch_deg", 0.0f);
                m.roll = p.value("roll_deg", 0.0f);
            }
            meta_.sensors.push_back(std::move(m));
        }
    }

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        json j;
        try {
            j = json::parse(line);
        } catch (const std::exception&) {
            continue;  // tolerate a truncated/garbled trailing line
        }
        if (!j.is_object() || !j.contains("t")) continue;  // footer / unknown
        const std::string t = j.value("t", std::string());

        Record rec;
        rec.host_ts = j.value("ts", 0.0);
        rec.sensor_idx = j.value("s", 0);

        if (t == "rdi") {
            rec.kind = StreamKind::Rdi;
            rec.rdi.counter = j.value("counter", 0u);
            rec.rdi.ts = j.value("ts_ns", uint64_t{0});
            rec.rdi.r = floatVec(j, "r");
            rec.rdi.az = floatVec(j, "az");
            rec.rdi.el = floatVec(j, "el");
            rec.rdi.v = floatVec(j, "v");
            rec.rdi.rcs = floatVec(j, "rcs");
            rec.rdi.snr = floatVec(j, "snr");
            if (rec.rdi.r.size() > kMaxDetections) continue;
        } else if (t == "shii") {
            rec.kind = StreamKind::Shii;
            rec.shii.counter = j.value("counter", 0u);
            rec.shii.defect = static_cast<uint8_t>(j.value("defect", 0));
            rec.shii.reason = static_cast<uint8_t>(j.value("reason", 0));
            rec.shii.supply = static_cast<uint8_t>(j.value("supply", 0));
            rec.shii.temp = static_cast<uint8_t>(j.value("temp", 0));
            rec.shii.time_sync = static_cast<uint8_t>(j.value("time_sync", 0));
            rec.shii.modes = u8Vec(j, "modes");
            rec.shii.cal = u8Vec(j, "cal");
        } else if (t == "spi") {
            rec.kind = StreamKind::Spi;
            rec.spi.counter = j.value("counter", 0u);
            auto pose = floatVec(j, "pose");
            for (int i = 0; i < 8 && i < static_cast<int>(pose.size()); ++i)
                rec.spi.pose[i] = pose[i];
            auto it = j.find("fov");
            if (it != j.end() && it->is_array()) {
                for (const auto& seg : *it) {
                    if (rec.spi.fov.size() >= kMaxFov) break;
                    SpiFov fv;
                    fv.az0 = seg.value("az0", 0.0f);
                    fv.az1 = seg.value("az1", 0.0f);
                    fv.el0 = seg.value("el0", 0.0f);
                    fv.el1 = seg.value("el1", 0.0f);
                    fv.blockage = static_cast<uint8_t>(seg.value("blockage", 0));
                    rec.spi.fov.push_back(fv);
                }
            }
        } else {
            continue;  // unknown record type
        }
        records_.push_back(std::move(rec));
    }
}

}  // namespace viewer
