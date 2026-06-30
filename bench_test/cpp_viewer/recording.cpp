// recording.cpp — implementation of the .btsrec3 recorder / replay reader.
//
// Copyright (c) 2026 bitsensing Inc.
#include "recording.hpp"

#include <cstring>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace viewer {

namespace {

constexpr char kMagic[8] = {'B', 'T', 'S', 'R', 'E', 'C', '3', '\n'};
constexpr uint32_t kMaxDetections = 4096;
constexpr uint32_t kMaxFov = 64;

template <typename T>
void put(std::ostream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool get(std::istream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(is) && is.gcount() == static_cast<std::streamsize>(sizeof(T));
}

}  // namespace

std::string withExtension(const std::string& path, const std::string& ext) {
    auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return path + ext;  // no extension present
    }
    return path.substr(0, dot) + ext;
}

// ── Recorder ──

Recorder::Recorder(const std::string& path, const RecMeta& meta)
    : path_(path), sidecar_(withExtension(path, ".json")), meta_(meta) {
    f_.open(path_, std::ios::binary | std::ios::trunc);
    if (!f_) {
        throw std::runtime_error("cannot open recording file: " + path_);
    }
    try {
        f_.write(kMagic, sizeof(kMagic));
        if (!f_) throw std::runtime_error("write failed: " + path_);
        writeSidecar();
    } catch (...) {
        closed_ = true;
        f_.close();  // don't leak the handle if setup fails
        throw;
    }
}

Recorder::~Recorder() { close(); }

void Recorder::writeSidecar() {
    nlohmann::json j;
    j["kind"] = meta_.kind;
    j["transport"] = meta_.transport;
    j["started_unix"] = meta_.started_unix;
    j["record_count"] = count_;
    j["sensors"] = nlohmann::json::array();
    for (const auto& s : meta_.sensors) {
        j["sensors"].push_back({
            {"index", s.index},
            {"ip", s.ip},
            {"model", s.model},
            {"pose", {{"offset_x_m", s.offset.x},
                      {"offset_y_m", s.offset.y},
                      {"offset_z_m", s.offset.z},
                      {"yaw_deg", s.yaw},
                      {"pitch_deg", s.pitch},
                      {"roll_deg", s.roll}}},
        });
    }
    std::ofstream sf(sidecar_, std::ios::trunc);
    if (sf) sf << j.dump(2);
}

void Recorder::writeRdi(double host_ts, int idx, const RdiRecord& rec) {
    const uint32_t n = rec.size();
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_ || n > kMaxDetections) return;
    put(f_, host_ts);
    put<uint8_t>(f_, static_cast<uint8_t>(StreamKind::Rdi));
    put<uint8_t>(f_, static_cast<uint8_t>(idx & 0xFF));
    put<uint32_t>(f_, rec.counter);
    put<uint64_t>(f_, rec.ts);
    put<uint32_t>(f_, n);
    for (uint32_t i = 0; i < n; ++i) {
        put<float>(f_, rec.r[i]);
        put<float>(f_, rec.az[i]);
        put<float>(f_, rec.el[i]);
        put<float>(f_, rec.v[i]);
        put<float>(f_, rec.rcs[i]);
        put<float>(f_, rec.snr[i]);
    }
    ++count_;
}

void Recorder::writeShii(double host_ts, int idx, const ShiiRecord& rec) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_) return;
    put(f_, host_ts);
    put<uint8_t>(f_, static_cast<uint8_t>(StreamKind::Shii));
    put<uint8_t>(f_, static_cast<uint8_t>(idx & 0xFF));
    put<uint32_t>(f_, rec.counter);
    put<uint8_t>(f_, rec.defect);
    put<uint8_t>(f_, rec.reason);
    put<uint8_t>(f_, rec.supply);
    put<uint8_t>(f_, rec.temp);
    put<uint8_t>(f_, rec.time_sync);
    put<uint8_t>(f_, static_cast<uint8_t>(rec.modes.size()));
    for (uint8_t m : rec.modes) put<uint8_t>(f_, m);
    put<uint8_t>(f_, static_cast<uint8_t>(rec.cal.size()));
    for (uint8_t c : rec.cal) put<uint8_t>(f_, c);
    ++count_;
}

void Recorder::writeSpi(double host_ts, int idx, const SpiRecord& rec) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (closed_) return;
    put(f_, host_ts);
    put<uint8_t>(f_, static_cast<uint8_t>(StreamKind::Spi));
    put<uint8_t>(f_, static_cast<uint8_t>(idx & 0xFF));
    put<uint32_t>(f_, rec.counter);
    for (int i = 0; i < 8; ++i) put<float>(f_, rec.pose[i]);
    put<uint8_t>(f_, static_cast<uint8_t>(rec.fov.size()));
    for (const auto& s : rec.fov) {
        put<float>(f_, s.az0);
        put<float>(f_, s.az1);
        put<float>(f_, s.el0);
        put<float>(f_, s.el1);
        put<uint8_t>(f_, s.blockage);
    }
    ++count_;
}

void Recorder::close() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (closed_) return;
        closed_ = true;
        f_.flush();
        f_.close();
    }
    meta_.record_count = count_;
    writeSidecar();  // rewrite sidecar with the final record count
}

// ── ReplayReader ──

ReplayReader::ReplayReader(const std::string& path)
    : path_(path), sidecar_(withExtension(path, ".json")) {
    // Sidecar metadata
    std::ifstream sf(sidecar_);
    if (sf) {
        nlohmann::json j;
        sf >> j;
        meta_.kind = j.value("kind", std::string("replay"));
        meta_.transport = j.value("transport", std::string("?"));
        meta_.started_unix = j.value("started_unix", 0.0);
        meta_.record_count = j.value("record_count", 0ull);
        if (j.contains("sensors")) {
            for (const auto& s : j["sensors"]) {
                RecSensorMeta m;
                m.index = s.value("index", 0);
                m.ip = s.value("ip", std::string());
                m.model = s.value("model", std::string());
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
    }

    // Binary records
    std::ifstream f(path_, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open recording: " + path_);
    char magic[8];
    f.read(magic, sizeof(magic));
    if (!f || std::memcmp(magic, kMagic, sizeof(magic)) != 0) {
        throw std::runtime_error("not a BTSREC3 file: " + path_);
    }

    while (true) {
        Record rec;
        uint8_t kind = 0, idx = 0;
        if (!get(f, rec.host_ts)) break;       // clean EOF
        if (!get(f, kind)) break;
        if (!get(f, idx)) break;
        rec.kind = static_cast<StreamKind>(kind);
        rec.sensor_idx = idx;

        if (rec.kind == StreamKind::Rdi) {
            uint32_t n = 0;
            if (!get(f, rec.rdi.counter) || !get(f, rec.rdi.ts) || !get(f, n)) break;
            if (n > kMaxDetections) break;  // corrupt
            rec.rdi.r.resize(n); rec.rdi.az.resize(n); rec.rdi.el.resize(n);
            rec.rdi.v.resize(n); rec.rdi.rcs.resize(n); rec.rdi.snr.resize(n);
            bool ok = true;
            for (uint32_t i = 0; i < n && ok; ++i) {
                ok = get(f, rec.rdi.r[i]) && get(f, rec.rdi.az[i]) &&
                     get(f, rec.rdi.el[i]) && get(f, rec.rdi.v[i]) &&
                     get(f, rec.rdi.rcs[i]) && get(f, rec.rdi.snr[i]);
            }
            if (!ok) break;
        } else if (rec.kind == StreamKind::Shii) {
            uint8_t nmodes = 0, ncal = 0;
            if (!get(f, rec.shii.counter) || !get(f, rec.shii.defect) ||
                !get(f, rec.shii.reason) || !get(f, rec.shii.supply) ||
                !get(f, rec.shii.temp) || !get(f, rec.shii.time_sync) ||
                !get(f, nmodes)) break;
            rec.shii.modes.resize(nmodes);
            bool ok = true;
            for (uint8_t i = 0; i < nmodes && ok; ++i) ok = get(f, rec.shii.modes[i]);
            if (!ok || !get(f, ncal)) break;
            rec.shii.cal.resize(ncal);
            for (uint8_t i = 0; i < ncal && ok; ++i) ok = get(f, rec.shii.cal[i]);
            if (!ok) break;
        } else if (rec.kind == StreamKind::Spi) {
            uint8_t nfov = 0;
            if (!get(f, rec.spi.counter)) break;
            bool ok = true;
            for (int i = 0; i < 8 && ok; ++i) ok = get(f, rec.spi.pose[i]);
            if (!ok || !get(f, nfov) || nfov > kMaxFov) break;
            rec.spi.fov.resize(nfov);
            for (uint8_t i = 0; i < nfov && ok; ++i) {
                auto& s = rec.spi.fov[i];
                ok = get(f, s.az0) && get(f, s.az1) && get(f, s.el0) &&
                     get(f, s.el1) && get(f, s.blockage);
            }
            if (!ok) break;
        } else {
            break;  // unknown kind
        }
        records_.push_back(std::move(rec));
    }
}

}  // namespace viewer
