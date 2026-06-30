// frame_store.hpp — thread-safe latest-state per sensor for the C++ viewer.
//
// Receiver / replay / sim threads write here; the Qt GUI polls snapshot() on a
// timer. Snapshots are deep copies so the GUI never touches producer state.
//
// Copyright (c) 2026 bitsensing Inc.
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "geometry.hpp"

namespace viewer {

struct HealthInfo {
    bool valid = false;
    uint32_t counter = 0;
    int defect = 0, reason = 0, supply = 0, temp = 0, time_sync = 0;
    std::vector<uint8_t> modes;
    std::vector<uint8_t> cal;
    uint64_t frames = 0;
};

struct PerfFov {
    float az0 = 0, az1 = 0, el0 = 0, el1 = 0;
    int blockage = 0;
};

struct PerfInfo {
    bool valid = false;
    uint32_t counter = 0;
    float pose[8] = {};  // ox, oy, oz, yaw, pitch, roll, yaw_err, pitch_err (rad)
    std::vector<PerfFov> fov;
    uint64_t frames = 0;
};

// World-frame point cloud + attributes for one sensor.
struct CloudData {
    std::vector<Vec3> points;  // world frame
    std::vector<float> velocity, rcs, snr;
    uint32_t counter = 0;
    uint64_t ts = 0;
    int n = 0;
    int claimed = 0;
    uint64_t frames = 0;
};

struct SensorSnapshot {
    std::string ip, model, serial;
    SensorPose pose;
    CloudData cloud;
    HealthInfo health;
    PerfInfo perf;
    bool connected = false;
    double fps = 0.0;
};

class FrameStore {
public:
    explicit FrameStore(int n) : states_(n) {}

    int size() const { return static_cast<int>(states_.size()); }

    void setPose(int idx, const SensorPose& pose, const std::string& ip,
                 const std::string& model = "", const std::string& serial = "") {
        if (!inRange(idx)) return;
        std::lock_guard<std::mutex> lk(mtx_);
        auto& s = states_[idx].snap;
        s.pose = pose;
        if (!ip.empty()) s.ip = ip;
        if (!model.empty()) s.model = model;
        if (!serial.empty()) s.serial = serial;
    }

    void setConnected(int idx, bool v) {
        if (!inRange(idx)) return;
        std::lock_guard<std::mutex> lk(mtx_);
        states_[idx].snap.connected = v;
    }

    void setCloud(int idx, CloudData&& cloud) {
        if (!inRange(idx)) return;
        const double now = monoSeconds();
        std::lock_guard<std::mutex> lk(mtx_);
        auto& st = states_[idx];
        cloud.frames = st.snap.cloud.frames + 1;
        st.snap.cloud = std::move(cloud);
        st.fps_times.push_back(now);
        if (st.fps_times.size() > 20) st.fps_times.pop_front();
        if (st.fps_times.size() >= 2) {
            double span = st.fps_times.back() - st.fps_times.front();
            st.snap.fps = span > 0 ? (st.fps_times.size() - 1) / span : 0.0;
        }
    }

    void setHealth(int idx, HealthInfo h) {
        if (!inRange(idx)) return;
        std::lock_guard<std::mutex> lk(mtx_);
        h.valid = true;
        h.frames = states_[idx].snap.health.frames + 1;
        states_[idx].snap.health = std::move(h);
    }

    void setPerf(int idx, PerfInfo p) {
        if (!inRange(idx)) return;
        std::lock_guard<std::mutex> lk(mtx_);
        p.valid = true;
        p.frames = states_[idx].snap.perf.frames + 1;
        states_[idx].snap.perf = std::move(p);
    }

    std::vector<SensorSnapshot> snapshot() {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<SensorSnapshot> out;
        out.reserve(states_.size());
        for (const auto& s : states_) out.push_back(s.snap);
        return out;
    }

private:
    struct State {
        SensorSnapshot snap;
        std::deque<double> fps_times;
    };

    bool inRange(int i) const { return i >= 0 && i < static_cast<int>(states_.size()); }

    static double monoSeconds() {
        return std::chrono::duration<double>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::mutex mtx_;
    std::vector<State> states_;
};

}  // namespace viewer
