// radar_source.hpp — data sources feeding the FrameStore.
//
//   RecordController  start/stop the active Recorder (shared by all sources)
//   LiveSource        one afi::AfiSdk per sensor; reads mounting pose via the
//                     Config API, receives RDI/SHII/SPI via SDK callbacks
//   ReplaySource      replays a .btsrec3 with play/pause/speed
//   SimSource         synthetic generator (no hardware)
//
// The afi SDK is only pulled in by radar_source.cpp; this header stays SDK-free.
//
// Copyright (c) 2026 bitsensing Inc.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "frame_store.hpp"
#include "jsonl_recording.hpp"  // brings in recording.hpp structs + JsonlRecorder/Reader

namespace afi { class AfiSdk; }

namespace viewer {

// Holds the currently-active recorder; every source consults it on its hot path.
class RecordController {
public:
    std::shared_ptr<JsonlRecorder> start(const std::string& path, const RecMeta& meta);
    void stop();
    std::shared_ptr<JsonlRecorder> recorder();  // copy keeps it alive across a write
    bool active();

private:
    std::mutex m_;
    std::shared_ptr<JsonlRecorder> rec_;
};

class Source {
public:
    virtual ~Source() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual std::string kind() const = 0;
    virtual bool canRecord() const { return false; }
    virtual RecMeta recordingMeta() const { return {}; }
    virtual bool isReplay() const { return false; }
};

class LiveSource : public Source {
public:
    LiveSource(std::vector<std::string> sensors, std::vector<std::string> streams,
               std::string transport, std::string host_ip, FrameStore& store,
               RecordController& rec, bool configure);
    ~LiveSource() override;

    void start() override;
    void stop() override;
    std::string kind() const override { return "LIVE"; }
    bool canRecord() const override { return true; }
    RecMeta recordingMeta() const override;

private:
    std::vector<std::string> sensors_;
    std::vector<std::string> streams_;
    std::string transport_;
    std::string host_ip_;
    FrameStore& store_;
    RecordController& rec_;
    bool configure_;
    std::vector<SensorPose> poses_;
    std::vector<std::string> models_;
    std::vector<std::string> serials_;
    std::vector<std::unique_ptr<afi::AfiSdk>> sdks_;
    bool started_ = false;
};

class ReplaySource : public Source {
public:
    ReplaySource(RecMeta meta, std::vector<Record> records, FrameStore& store,
                 double speed);
    ~ReplaySource() override;

    void start() override;
    void stop() override;
    std::string kind() const override { return "REPLAY"; }
    bool isReplay() const override { return true; }

    void pause() { paused_.store(true); }
    void resume() { paused_.store(false); }
    bool paused() const { return paused_.load(); }
    void setSpeed(double s);
    double progress() const { return progress_.load(); }
    bool finished() const { return finished_.load(); }

private:
    void run();

    std::vector<Record> records_;
    FrameStore& store_;
    std::vector<SensorPose> poses_;  // indexed by sensor index
    std::thread th_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> finished_{false};
    std::atomic<double> progress_{0.0};
    std::mutex speed_mtx_;
    double speed_ = 1.0;
};

class SimSource : public Source {
public:
    SimSource(std::vector<std::string> sensors, std::vector<std::string> streams,
              FrameStore& store, RecordController& rec, double fps = 15.0);
    ~SimSource() override;

    void start() override;
    void stop() override;
    std::string kind() const override { return "SIM"; }
    bool canRecord() const override { return true; }
    RecMeta recordingMeta() const override;

private:
    void run();

    std::vector<std::string> sensors_;
    std::vector<std::string> streams_;
    FrameStore& store_;
    RecordController& rec_;
    std::vector<SensorPose> poses_;
    double period_s_;
    std::thread th_;
    std::atomic<bool> stop_{false};
};

}  // namespace viewer
