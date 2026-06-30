// radar_source.cpp — LiveSource (afi SDK), ReplaySource, SimSource.
//
// Copyright (c) 2026 bitsensing Inc.
#include "radar_source.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <afi/afi_sdk.h>

namespace viewer {

namespace {

constexpr float kPi = 3.14159265358979323846f;

double nowUnix() {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool hasStream(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// ── Shared ingest path (used by live / replay / sim) ──

void ingestRdi(FrameStore& store, RecordController* rc, const SensorPose& pose,
               int idx, const RdiRecord& r, double host_ts) {
    if (rc) {
        if (auto rec = rc->recorder()) rec->writeRdi(host_ts, idx, r);
    }
    CloudData c;
    c.counter = r.counter;
    c.ts = r.ts;
    const uint32_t n = r.size();
    c.n = static_cast<int>(n);
    c.claimed = static_cast<int>(n);
    c.points.reserve(n);
    c.velocity.reserve(n);
    c.rcs.reserve(n);
    c.snr.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        c.points.push_back(pose.transform(
            sphericalToCartesian(r.r[i], r.az[i], r.el[i])));
        c.velocity.push_back(r.v[i]);
        c.rcs.push_back(r.rcs[i]);
        c.snr.push_back(r.snr[i]);
    }
    store.setConnected(idx, true);
    store.setCloud(idx, std::move(c));
}

void ingestShii(FrameStore& store, RecordController* rc, int idx,
                const ShiiRecord& s, double host_ts) {
    if (rc) {
        if (auto rec = rc->recorder()) rec->writeShii(host_ts, idx, s);
    }
    HealthInfo h;
    h.counter = s.counter;
    h.defect = s.defect;
    h.reason = s.reason;
    h.supply = s.supply;
    h.temp = s.temp;
    h.time_sync = s.time_sync;
    h.modes = s.modes;
    h.cal = s.cal;
    store.setHealth(idx, std::move(h));
}

void ingestSpi(FrameStore& store, RecordController* rc, int idx,
               const SpiRecord& sp, double host_ts) {
    if (rc) {
        if (auto rec = rc->recorder()) rec->writeSpi(host_ts, idx, sp);
    }
    PerfInfo p;
    p.counter = sp.counter;
    for (int i = 0; i < 8; ++i) p.pose[i] = sp.pose[i];
    for (const auto& f : sp.fov) {
        PerfFov pf;
        pf.az0 = f.az0; pf.az1 = f.az1; pf.el0 = f.el0; pf.el1 = f.el1;
        pf.blockage = f.blockage;
        p.fov.push_back(pf);
    }
    store.setPerf(idx, std::move(p));
}

SensorPose readPose(afi::AfiSdk& sdk, std::string& model, std::string& serial) {
    Vec3 off;
    float yaw = 0, pitch = 0, roll = 0;
    afi::AfiMountingPosition mp;
    if (sdk.GetMountingPosition(mp) == afi::kOk) off = {mp.x, mp.y, mp.z};
    afi::AfiRotation rot;
    if (sdk.GetRotation(rot) == afi::kOk) {
        yaw = rot.yaw; pitch = rot.pitch; roll = rot.roll;
    }
    afi::AfiDeviceInfo info;
    if (sdk.GetDeviceInfo(info) == afi::kOk) {
        model = info.model_name;
        serial = info.serial_number;
    }
    return SensorPose(off, yaw, pitch, roll);
}

void genSimPoints(std::size_t sensor_i, uint32_t k, RdiRecord& r) {
    const float phase = 0.15f * static_cast<float>(k) + static_cast<float>(sensor_i);
    const int m = 120;  // moving target ring
    const float t_r = 12.0f + 4.0f * std::sin(phase * 0.4f);
    const float t_az = 0.3f * std::sin(phase * 0.2f);
    const float t_el = 0.1f * std::cos(phase * 0.3f);
    for (int j = 0; j < m; ++j) {
        const float a = 2.0f * kPi * j / m;
        r.r.push_back(t_r + 0.8f * std::cos(a));
        r.az.push_back(t_az + 0.05f * std::sin(a));
        r.el.push_back(t_el + 0.05f * std::cos(a));
        r.v.push_back(-3.0f * std::sin(phase * 0.4f));
        r.rcs.push_back(8.0f);
        r.snr.push_back(22.0f);
    }
    const int c = 200;  // static clutter spread across the FoV
    for (int j = 0; j < c; ++j) {
        const float f = static_cast<float>(j) / (c - 1);
        const float rr = 3.0f + 37.0f * f;
        r.r.push_back(rr);
        r.az.push_back(-0.7f + 1.4f * f + 0.02f * std::sin(rr));
        r.el.push_back(0.02f * std::sin(rr * 0.5f));
        r.v.push_back(0.0f);
        r.rcs.push_back(-5.0f);
        r.snr.push_back(9.0f);
    }
}

}  // namespace

// ── RecordController ──

std::shared_ptr<JsonlRecorder> RecordController::start(const std::string& path,
                                                       const RecMeta& meta) {
    auto nr = std::make_shared<JsonlRecorder>(path, meta);  // may throw before we touch rec_
    std::lock_guard<std::mutex> lk(m_);
    if (rec_) rec_->close();
    rec_ = nr;
    return nr;
}

void RecordController::stop() {
    std::shared_ptr<JsonlRecorder> r;
    {
        std::lock_guard<std::mutex> lk(m_);
        r = rec_;
        rec_.reset();
    }
    if (r) r->close();
}

std::shared_ptr<JsonlRecorder> RecordController::recorder() {
    std::lock_guard<std::mutex> lk(m_);
    return rec_;
}

bool RecordController::active() {
    std::lock_guard<std::mutex> lk(m_);
    return static_cast<bool>(rec_);
}

// ── LiveSource ──

LiveSource::LiveSource(std::vector<std::string> sensors,
                       std::vector<std::string> streams, std::string transport,
                       std::string host_ip, FrameStore& store,
                       RecordController& rec, bool configure)
    : sensors_(std::move(sensors)), streams_(std::move(streams)),
      transport_(std::move(transport)), host_ip_(std::move(host_ip)),
      store_(store), rec_(rec), configure_(configure) {
    poses_.resize(sensors_.size());
    models_.resize(sensors_.size());
    serials_.resize(sensors_.size());
}

LiveSource::~LiveSource() { stop(); }

RecMeta LiveSource::recordingMeta() const {
    RecMeta m;
    m.kind = "live";
    m.transport = transport_;
    m.started_unix = nowUnix();
    for (std::size_t i = 0; i < sensors_.size(); ++i) {
        RecSensorMeta s;
        s.index = static_cast<int>(i);
        s.ip = sensors_[i];
        s.port = (transport_ == "udp") ? static_cast<int>(30509 + i * 10) : 30509;
        s.model = models_[i];
        s.serial = serials_[i];
        s.offset = poses_[i].position();
        s.yaw = poses_[i].yawDeg();
        s.pitch = poses_[i].pitchDeg();
        s.roll = poses_[i].rollDeg();
        m.sensors.push_back(std::move(s));
    }
    return m;
}

void LiveSource::start() {
    if (started_) return;
    started_ = true;

    FrameStore* store = &store_;
    RecordController* rc = &rec_;

    for (std::size_t i = 0; i < sensors_.size(); ++i) {
        auto sdk = std::make_unique<afi::AfiSdk>();
        afi::AfiSdkConfig cfg;
        cfg.sensor_ip = sensors_[i];
        cfg.stream_transport = transport_;
        cfg.enable_config_api = true;
        cfg.enable_rdi = hasStream(streams_, "rdi");
        cfg.enable_shi = hasStream(streams_, "shii");
        cfg.enable_spi = hasStream(streams_, "spi");
        if (transport_ == "udp") {
            cfg.rdi_port = static_cast<uint16_t>(30509 + i * 10);
            cfg.shi_port = static_cast<uint16_t>(30510 + i * 10);
            cfg.spi_port = static_cast<uint16_t>(30511 + i * 10);
        }

        const int idx = static_cast<int>(i);
        if (sdk->Init(cfg) == afi::kOk) {
            std::string model, serial;
            poses_[i] = readPose(*sdk, model, serial);
            models_[i] = model;
            serials_[i] = serial;
            store_.setPose(idx, poses_[i], sensors_[i], model, serial);

            // UDP: point the sensor's stream destinations at this host.
            if (transport_ == "udp" && !host_ip_.empty()) {
                if (cfg.enable_rdi)
                    sdk->SetStreamDestination(afi::AfiStreamKind::kDetection,
                                              {host_ip_, cfg.rdi_port, "udp"});
                if (cfg.enable_shi)
                    sdk->SetStreamDestination(afi::AfiStreamKind::kHealth,
                                              {host_ip_, cfg.shi_port, "udp"});
                if (cfg.enable_spi)
                    sdk->SetStreamDestination(afi::AfiStreamKind::kPerformance,
                                              {host_ip_, cfg.spi_port, "udp"});
            }

            const SensorPose pose = poses_[i];
            if (cfg.enable_rdi) {
                sdk->RegRecvCallback([store, rc, pose, idx](const afi::AfiRdiFrame& f) {
                    const auto& m = f.message;
                    RdiRecord r;
                    r.counter = m.message_counter;
                    r.ts = m.timestamp;
                    int n = m.num_detections;
                    if (n < 0) n = 0;
                    if (m.detections) {
                        for (int k = 0; k < n; ++k) {
                            const auto& d = m.detections[k];
                            r.r.push_back(d.position_radial_distance);
                            r.az.push_back(d.position_azimuth);
                            r.el.push_back(d.position_elevation);
                            r.v.push_back(d.relative_velocity_radial);
                            r.rcs.push_back(d.radar_cross_section);
                            r.snr.push_back(d.signal_to_noise_ratio);
                        }
                    }
                    ingestRdi(*store, rc, pose, idx, r, nowUnix());
                });
            }
            if (cfg.enable_shi) {
                sdk->RegRecvCallback([store, rc, idx](const afi::AfiShiMessage& m) {
                    ShiiRecord s;
                    s.counter = m.message_counter;
                    s.defect = static_cast<uint8_t>(m.sensor_defect_recognised);
                    s.reason = static_cast<uint8_t>(m.sensor_defect_reason);
                    s.supply = static_cast<uint8_t>(m.supply_voltage_status);
                    s.temp = static_cast<uint8_t>(m.sensor_temperature_status);
                    s.time_sync = static_cast<uint8_t>(m.sensor_time_sync);
                    for (int k = 0; k < m.num_valid_operation_modes &&
                                    k < BTS_SHI_MAX_OPERATION_MODES; ++k)
                        s.modes.push_back(m.sensor_operation_modes[k]);
                    for (int k = 0; k < m.num_valid_calibration_components &&
                                    k < BTS_SHI_MAX_CALIBRATION_COMPONENTS; ++k)
                        s.cal.push_back(m.sensor_calibration_statuses[k]);
                    ingestShii(*store, rc, idx, s, nowUnix());
                    store->setConnected(idx, true);
                });
            }
            if (cfg.enable_spi) {
                sdk->RegRecvCallback([store, rc, idx](const afi::AfiSpiMessage& m) {
                    SpiRecord sp;
                    sp.counter = m.message_counter;
                    const auto& ps = m.sensor_pose;
                    sp.pose[0] = ps.origin_point_x;
                    sp.pose[1] = ps.origin_point_y;
                    sp.pose[2] = ps.origin_point_z;
                    sp.pose[3] = ps.orientation_yaw;
                    sp.pose[4] = ps.orientation_pitch;
                    sp.pose[5] = ps.orientation_roll;
                    sp.pose[6] = ps.orientation_error_yaw;
                    sp.pose[7] = ps.orientation_error_pitch;
                    for (int k = 0; k < m.num_fov_segments &&
                                    k < BTS_SPI_MAX_FOV_SEGMENTS; ++k) {
                        const auto& seg = m.fov_segments[k];
                        SpiFov fv;
                        fv.az0 = seg.azimuth_begin;
                        fv.az1 = seg.azimuth_end;
                        fv.el0 = seg.elevation_begin;
                        fv.el1 = seg.elevation_end;
                        fv.blockage = static_cast<uint8_t>(seg.blockage_status);
                        sp.fov.push_back(fv);
                    }
                    ingestSpi(*store, rc, idx, sp, nowUnix());
                    store->setConnected(idx, true);
                });
            }
            sdk->Start();
        } else {
            // Couldn't reach the sensor; show it at the origin, disconnected.
            store_.setPose(idx, poses_[i], sensors_[i]);
        }
        sdks_.push_back(std::move(sdk));
    }
}

void LiveSource::stop() {
    for (auto& s : sdks_) {
        if (s) s->Stop();
    }
    sdks_.clear();
    started_ = false;
}

// ── ReplaySource ──

ReplaySource::ReplaySource(RecMeta meta, std::vector<Record> records,
                           FrameStore& store, double speed)
    : records_(std::move(records)), store_(store), speed_(speed) {
    // Sensor index is a uint8 on the wire (0-255); clamp so a corrupt header
    // can't request a huge allocation.
    constexpr int kMaxSensorIndex = 255;
    int maxIdx = store_.size() - 1;
    for (const auto& s : meta.sensors)
        if (s.index >= 0 && s.index <= kMaxSensorIndex)
            maxIdx = std::max(maxIdx, s.index);
    poses_.assign(std::max(0, maxIdx + 1), SensorPose());
    for (const auto& s : meta.sensors) {
        if (s.index >= 0 && s.index < static_cast<int>(poses_.size())) {
            poses_[s.index] = SensorPose(s.offset, s.yaw, s.pitch, s.roll);
            store_.setPose(s.index, poses_[s.index], s.ip, s.model);
        }
    }
}

ReplaySource::~ReplaySource() { stop(); }

void ReplaySource::start() { th_ = std::thread(&ReplaySource::run, this); }

void ReplaySource::stop() {
    stop_.store(true);
    paused_.store(false);
    if (th_.joinable()) th_.join();
}

void ReplaySource::setSpeed(double s) {
    std::lock_guard<std::mutex> lk(speed_mtx_);
    speed_ = s < 0.05 ? 0.05 : s;
}

void ReplaySource::run() {
    const auto& recs = records_;
    if (recs.empty()) {
        finished_.store(true);
        return;
    }
    const double t0 = recs.front().host_ts;
    const std::size_t total = recs.size();
    double play_clock = 0.0;  // seconds of recording time elapsed
    auto last = std::chrono::steady_clock::now();
    std::size_t i = 0;
    while (i < total && !stop_.load()) {
        auto now = std::chrono::steady_clock::now();
        if (!paused_.load()) {
            double sp;
            { std::lock_guard<std::mutex> lk(speed_mtx_); sp = speed_; }
            play_clock += std::chrono::duration<double>(now - last).count() * sp;
        }
        last = now;

        const auto& rec = recs[i];
        if (rec.host_ts - t0 <= play_clock) {
            const int idx = rec.sensor_idx;
            const SensorPose pose =
                (idx >= 0 && idx < static_cast<int>(poses_.size()))
                    ? poses_[idx]
                    : SensorPose();
            if (rec.kind == StreamKind::Rdi)
                ingestRdi(store_, nullptr, pose, idx, rec.rdi, 0.0);
            else if (rec.kind == StreamKind::Shii)
                ingestShii(store_, nullptr, idx, rec.shii, 0.0);
            else if (rec.kind == StreamKind::Spi)
                ingestSpi(store_, nullptr, idx, rec.spi, 0.0);
            ++i;
            progress_.store(static_cast<double>(i) / total);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    finished_.store(true);
}

// ── SimSource ──

SimSource::SimSource(std::vector<std::string> sensors,
                     std::vector<std::string> streams, FrameStore& store,
                     RecordController& rec, double fps)
    : sensors_(std::move(sensors)), streams_(std::move(streams)), store_(store),
      rec_(rec), period_s_(1.0 / fps) {
    poses_.resize(sensors_.size());
    const float half = (sensors_.size() - 1) / 2.0f;
    for (std::size_t i = 0; i < sensors_.size(); ++i) {
        const float yaw = (static_cast<float>(i) - half) * 30.0f;
        poses_[i] = SensorPose({0.0f, (1.0f - static_cast<float>(i)) * 1.5f, 0.6f},
                               yaw, 0.0f, 0.0f);
    }
}

SimSource::~SimSource() { stop(); }

RecMeta SimSource::recordingMeta() const {
    RecMeta m;
    m.kind = "sim";
    m.transport = "sim";
    m.started_unix = nowUnix();
    for (std::size_t i = 0; i < sensors_.size(); ++i) {
        RecSensorMeta s;
        s.index = static_cast<int>(i);
        s.ip = sensors_[i];
        s.port = 30509;
        s.model = "AFI920(sim)";
        s.serial = "SIM";
        s.offset = poses_[i].position();
        s.yaw = poses_[i].yawDeg();
        s.pitch = poses_[i].pitchDeg();
        s.roll = poses_[i].rollDeg();
        m.sensors.push_back(std::move(s));
    }
    return m;
}

void SimSource::start() {
    for (std::size_t i = 0; i < sensors_.size(); ++i)
        store_.setPose(static_cast<int>(i), poses_[i], sensors_[i], "AFI920(sim)");
    th_ = std::thread(&SimSource::run, this);
}

void SimSource::stop() {
    stop_.store(true);
    if (th_.joinable()) th_.join();
}

void SimSource::run() {
    constexpr float kDeg2Rad = kPi / 180.0f;
    uint32_t k = 0;
    while (!stop_.load()) {
        const double host_ts = nowUnix();
        for (std::size_t i = 0; i < sensors_.size(); ++i) {
            const SensorPose& pose = poses_[i];
            if (hasStream(streams_, "rdi")) {
                RdiRecord r;
                r.counter = k;
                r.ts = static_cast<uint64_t>(host_ts * 1e9);
                genSimPoints(i, k, r);
                ingestRdi(store_, &rec_, pose, static_cast<int>(i), r, host_ts);
            }
            if (hasStream(streams_, "shii") && k % 5 == 0) {
                ShiiRecord s;
                s.counter = k;
                s.supply = 2;  // within limits
                s.temp = ((k / 50) % 8 == i) ? 3 : 2;  // occasional pre-over-temp
                s.modes = {0};  // 0 = NORMAL_DUAL_RANGE (SHII operation-mode enum)
                s.cal = {0, 0, 0};
                ingestShii(store_, &rec_, static_cast<int>(i), s, host_ts);
            }
            if (hasStream(streams_, "spi") && k % 5 == 0) {
                SpiRecord sp;
                sp.counter = k;
                const auto off = pose.position();
                sp.pose[0] = off.x; sp.pose[1] = off.y; sp.pose[2] = off.z;
                sp.pose[3] = pose.yawDeg() * kDeg2Rad;
                sp.pose[4] = pose.pitchDeg() * kDeg2Rad;
                sp.pose[5] = pose.rollDeg() * kDeg2Rad;
                sp.fov.push_back({-1.0f, 1.0f, -0.3f, 0.3f, 0});
                ingestSpi(store_, &rec_, static_cast<int>(i), sp, host_ts);
            }
            store_.setConnected(static_cast<int>(i), true);
        }
        ++k;
        std::this_thread::sleep_for(std::chrono::duration<double>(period_s_));
    }
}

}  // namespace viewer
