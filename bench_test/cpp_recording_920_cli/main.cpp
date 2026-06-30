// main.cpp — AFI920 CLI Recorder (C++).
//
// Connect to up to 4 AFI920 radars and record their RDI/SHII/SPI streams to a
// single unified .jsonl file (see ../python_recording_920_cli/JSONL_FORMAT.md).
// Recording only — no GUI. Output replays in either the Python or C++ viewer.
//
// Usage:
//   afi_recorder record IP[:PORT] ...           [--streams rdi shii spi]
//                [--radar IP[:PORT]] [--config radars.json]
//                [--transport tcp|udp] [--host-ip IP]
//                [--duration N] [--outdir DIR] [--out NAME] [--no-configure]
//
// PORT is the radar's RDI port (default 30509); SHII/SPI use PORT+1/PORT+2.
//
// Copyright (c) 2026 bitsensing Inc.
#include <afi/afi_sdk.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "jsonl_writer.hpp"

using namespace afirec;

namespace {

constexpr int kMaxRadars = 4;
std::atomic<bool> g_running{true};
// Per (radar, stream) frame counters for the live stats line. Static => zeroed.
std::atomic<uint64_t> g_frames[kMaxRadars][3];

void onSignal(int) { g_running.store(false); }

double nowUnix() {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct Target {
    std::string ip;
    int port = 30509;
};

bool isAllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

Target parseTarget(const std::string& tok) {
    Target t;
    auto colon = tok.find_last_of(':');
    if (colon != std::string::npos && isAllDigits(tok.substr(colon + 1))) {
        t.ip = tok.substr(0, colon);
        t.port = std::atoi(tok.c_str() + colon + 1);
    } else {
        t.ip = tok;
    }
    return t;
}

bool hasStream(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& e : v)
        if (e == s) return true;
    return false;
}

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

void usage() {
    std::printf(
        "AFI920 CLI Recorder (C++)\n"
        "Usage: afi_recorder record IP[:PORT] ... [options]\n"
        "  --radar IP[:PORT]   add a radar target (repeatable, up to 4)\n"
        "  --config FILE       JSON config (used if no target on the CLI)\n"
        "  --streams rdi shii spi   streams to record (default: all)\n"
        "  --transport tcp|udp transport (default tcp)\n"
        "  --host-ip IP        host IP radars push UDP to (required for udp)\n"
        "  --duration N        record N seconds (0 = until Ctrl+C)\n"
        "  --outdir DIR        output directory (default ./recordings)\n"
        "  --out NAME          output filename (default rec_<ts>.jsonl)\n"
        "  --no-configure      do not use the Config API (passive receive)\n");
}

// Parse the radar config JSON. Returns false on read/parse error.
bool loadConfig(const std::string& path, std::vector<Target>& radars,
                std::string& transport, std::vector<std::string>& streams,
                std::string& host_ip) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot open config %s\n", path.c_str());
        return false;
    }
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: bad config JSON: %s\n", e.what());
        return false;
    }
    const char* key = j.contains("radars") ? "radars"
                      : (j.contains("sensors") ? "sensors" : nullptr);
    if (key) {
        for (const auto& e : j[key]) {
            if (e.is_string()) {
                radars.push_back(parseTarget(e.get<std::string>()));
            } else if (e.is_object()) {
                Target t;
                t.ip = e.value("ip", e.value("address", std::string()));
                t.port = e.value("port", 30509);
                if (!t.ip.empty()) radars.push_back(t);
            }
        }
    }
    if (j.contains("transport")) transport = j["transport"].get<std::string>();
    if (j.contains("host_ip") && j["host_ip"].is_string())
        host_ip = j["host_ip"].get<std::string>();
    if (j.contains("streams") && j["streams"].is_array()) {
        streams.clear();
        for (const auto& s : j["streams"]) streams.push_back(s.get<std::string>());
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::vector<Target> cliRadars;
    std::string configPath;
    std::vector<std::string> streams;
    std::string transport;
    std::string host_ip;
    std::string outdir = "recordings";
    std::string outName;
    double duration = 0.0;
    bool no_configure = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* def) -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string(def);
        };
        if (a == "record") {
            continue;  // optional leading subcommand
        } else if (a == "--radar") {
            cliRadars.push_back(parseTarget(next("")));
        } else if (a == "--config") {
            configPath = next("");
        } else if (a == "--streams") {
            while (i + 1 < argc && argv[i + 1][0] != '-')
                streams.push_back(argv[++i]);
        } else if (a == "--transport") {
            transport = next("tcp");
        } else if (a == "--host-ip") {
            host_ip = next("");
        } else if (a == "--duration") {
            duration = std::atof(next("0").c_str());
        } else if (a == "--outdir") {
            outdir = next("recordings");
        } else if (a == "--out") {
            outName = next("");
        } else if (a == "--no-configure") {
            no_configure = true;
        } else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else if (!a.empty() && a[0] != '-') {
            cliRadars.push_back(parseTarget(a));
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
            return 2;
        }
    }

    // Resolve targets: CLI first, else config file.
    std::vector<Target> radars = cliRadars;
    if (radars.empty()) {
        std::string cfg = configPath.empty() ? "radars.json" : configPath;
        std::string c_transport, c_host;
        std::vector<std::string> c_streams;
        if (!loadConfig(cfg, radars, c_transport, c_streams, c_host)) {
            std::fprintf(stderr,
                         "ERROR: no radar on the command line and no usable "
                         "config (%s). Pass IP or --config FILE.\n", cfg.c_str());
            return 2;
        }
        if (radars.empty()) {
            std::fprintf(stderr, "ERROR: config %s lists no radars.\n", cfg.c_str());
            return 2;
        }
        std::printf("Loaded %zu radar(s) from %s\n", radars.size(), cfg.c_str());
        if (transport.empty()) transport = c_transport;
        if (streams.empty()) streams = c_streams;
        if (host_ip.empty()) host_ip = c_host;
    }

    if (transport.empty()) transport = "tcp";
    if (streams.empty()) streams = {"rdi", "shii", "spi"};

    if (radars.size() > static_cast<std::size_t>(kMaxRadars)) {
        std::fprintf(stderr, "ERROR: at most %d radars at once (got %zu).\n",
                     kMaxRadars, radars.size());
        return 2;
    }
    if (transport == "udp" && host_ip.empty()) {
        std::fprintf(stderr, "ERROR: --transport udp requires --host-ip.\n");
        return 2;
    }

    const std::string fname =
        outName.empty() ? ("rec_" + timestamp() + ".jsonl") : outName;
    const std::string outPath = outdir + "/" + fname;

    std::printf("AFI920 CLI Recorder (C++)\n");
    std::printf("Radars:    ");
    for (const auto& r : radars) std::printf("%s:%d  ", r.ip.c_str(), r.port);
    std::printf("\nStreams:   ");
    for (const auto& s : streams) std::printf("%s ", s.c_str());
    std::printf("\nTransport: %s\nOutput:    %s\n", transport.c_str(),
                outPath.c_str());

    // 1) Init each radar + read mounting pose (best effort) -> header metadata.
    std::vector<std::unique_ptr<afi::AfiSdk>> sdks;
    std::vector<afi::AfiSdkConfig> cfgs;
    std::vector<bool> inited;
    Header header;
    header.kind = "live";
    header.transport = transport;
    header.started_unix = nowUnix();

    for (std::size_t i = 0; i < radars.size(); ++i) {
        const Target& t = radars[i];
        auto sdk = std::make_unique<afi::AfiSdk>();
        afi::AfiSdkConfig cfg;
        cfg.sensor_ip = t.ip;
        cfg.stream_transport = transport;
        cfg.enable_config_api = !no_configure;
        cfg.enable_rdi = hasStream(streams, "rdi");
        cfg.enable_shi = hasStream(streams, "shii");
        cfg.enable_spi = hasStream(streams, "spi");
        if (transport == "tcp") {
            cfg.rdi_port = static_cast<uint16_t>(t.port);
            cfg.shi_port = static_cast<uint16_t>(t.port + 1);
            cfg.spi_port = static_cast<uint16_t>(t.port + 2);
        } else {
            cfg.rdi_port = static_cast<uint16_t>(30509 + i * 10);
            cfg.shi_port = static_cast<uint16_t>(30510 + i * 10);
            cfg.spi_port = static_cast<uint16_t>(30511 + i * 10);
        }

        SensorMeta sm;
        sm.index = static_cast<int>(i);
        sm.ip = t.ip;
        sm.port = t.port;

        const bool ok = (sdk->Init(cfg) == afi::kOk);
        if (ok && cfg.enable_config_api) {
            afi::AfiMountingPosition mp;
            if (sdk->GetMountingPosition(mp) == afi::kOk) {
                sm.ox = mp.x; sm.oy = mp.y; sm.oz = mp.z;
            }
            afi::AfiRotation rot;
            if (sdk->GetRotation(rot) == afi::kOk) {
                sm.yaw = rot.yaw; sm.pitch = rot.pitch; sm.roll = rot.roll;
            }
            afi::AfiDeviceInfo di;
            if (sdk->GetDeviceInfo(di) == afi::kOk) {
                sm.model = di.model_name; sm.serial = di.serial_number;
            }
        }
        if (!ok)
            std::fprintf(stderr, "[%s] Init failed; will not receive.\n",
                         t.ip.c_str());

        header.sensors.push_back(sm);
        sdks.push_back(std::move(sdk));
        cfgs.push_back(cfg);
        inited.push_back(ok);
    }

    // 2) Open the recording (header now has every sensor's pose).
    std::error_code ec;
    std::filesystem::create_directories(outdir, ec);  // ofstream won't make dirs
    std::unique_ptr<JsonlWriter> writer;
    try {
        writer = std::make_unique<JsonlWriter>(outPath, header);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
    JsonlWriter* w = writer.get();

    // 3) Register callbacks + start reception.
    for (std::size_t i = 0; i < radars.size(); ++i) {
        if (!inited[i]) continue;
        afi::AfiSdk* sdk = sdks[i].get();
        const afi::AfiSdkConfig& cfg = cfgs[i];
        const int idx = static_cast<int>(i);

        if (transport == "udp" && !host_ip.empty()) {
            if (cfg.enable_rdi)
                sdk->SetStreamDestination(afi::AfiStreamKind::kDetection,
                                          {host_ip, cfg.rdi_port, "udp"});
            if (cfg.enable_shi)
                sdk->SetStreamDestination(afi::AfiStreamKind::kHealth,
                                          {host_ip, cfg.shi_port, "udp"});
            if (cfg.enable_spi)
                sdk->SetStreamDestination(afi::AfiStreamKind::kPerformance,
                                          {host_ip, cfg.spi_port, "udp"});
        }

        if (cfg.enable_rdi) {
            sdk->RegRecvCallback([w, idx](const afi::AfiRdiFrame& f) {
                const auto& m = f.message;
                int n = m.num_detections;
                if (n < 0) n = 0;
                std::vector<float> r, az, el, v, rcs, snr;
                r.reserve(n); az.reserve(n); el.reserve(n);
                v.reserve(n); rcs.reserve(n); snr.reserve(n);
                if (m.detections) {
                    for (int k = 0; k < n; ++k) {
                        const auto& d = m.detections[k];
                        r.push_back(d.position_radial_distance);
                        az.push_back(d.position_azimuth);
                        el.push_back(d.position_elevation);
                        v.push_back(d.relative_velocity_radial);
                        rcs.push_back(d.radar_cross_section);
                        snr.push_back(d.signal_to_noise_ratio);
                    }
                }
                w->writeRdi(nowUnix(), idx, m.message_counter, m.timestamp,
                            r, az, el, v, rcs, snr);
                g_frames[idx][0].fetch_add(1, std::memory_order_relaxed);
            });
        }
        if (cfg.enable_shi) {
            sdk->RegRecvCallback([w, idx](const afi::AfiShiMessage& m) {
                std::vector<uint8_t> modes, cal;
                for (int k = 0; k < m.num_valid_operation_modes &&
                                k < BTS_SHI_MAX_OPERATION_MODES; ++k)
                    modes.push_back(m.sensor_operation_modes[k]);
                for (int k = 0; k < m.num_valid_calibration_components &&
                                k < BTS_SHI_MAX_CALIBRATION_COMPONENTS; ++k)
                    cal.push_back(m.sensor_calibration_statuses[k]);
                w->writeShii(nowUnix(), idx, m.message_counter,
                             m.sensor_defect_recognised, m.sensor_defect_reason,
                             m.supply_voltage_status, m.sensor_temperature_status,
                             m.sensor_time_sync, modes, cal);
                g_frames[idx][1].fetch_add(1, std::memory_order_relaxed);
            });
        }
        if (cfg.enable_spi) {
            sdk->RegRecvCallback([w, idx](const afi::AfiSpiMessage& m) {
                float pose[8];
                const auto& ps = m.sensor_pose;
                pose[0] = ps.origin_point_x; pose[1] = ps.origin_point_y;
                pose[2] = ps.origin_point_z; pose[3] = ps.orientation_yaw;
                pose[4] = ps.orientation_pitch; pose[5] = ps.orientation_roll;
                pose[6] = ps.orientation_error_yaw;
                pose[7] = ps.orientation_error_pitch;
                std::vector<FovSeg> fov;
                for (int k = 0; k < m.num_fov_segments &&
                                k < BTS_SPI_MAX_FOV_SEGMENTS; ++k) {
                    const auto& seg = m.fov_segments[k];
                    fov.push_back({seg.azimuth_begin, seg.azimuth_end,
                                   seg.elevation_begin, seg.elevation_end,
                                   static_cast<int>(seg.blockage_status)});
                }
                w->writeSpi(nowUnix(), idx, m.message_counter, pose, fov);
                g_frames[idx][2].fetch_add(1, std::memory_order_relaxed);
            });
        }
        sdk->Start();
    }

    std::signal(SIGINT, onSignal);
#ifdef SIGTERM
    std::signal(SIGTERM, onSignal);
#endif
    std::printf("Recording... (Ctrl+C to stop)\n\n");

    auto start = std::chrono::steady_clock::now();
    auto lastStats = start;
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        if (duration > 0 && elapsed >= duration) {
            std::printf("Duration reached (%.0fs), stopping.\n", duration);
            break;
        }
        if (std::chrono::duration<double>(now - lastStats).count() >= 5.0) {
            lastStats = now;
            std::printf("[t=%5.0fs] records=%llu", elapsed,
                        static_cast<unsigned long long>(w->count()));
            for (std::size_t i = 0; i < radars.size(); ++i) {
                std::printf("  | %s rdi=%llu shii=%llu spi=%llu", radars[i].ip.c_str(),
                            (unsigned long long)g_frames[i][0].load(),
                            (unsigned long long)g_frames[i][1].load(),
                            (unsigned long long)g_frames[i][2].load());
            }
            std::printf("\n");
            std::fflush(stdout);
        }
    }

    for (auto& s : sdks)
        if (s) s->Stop();
    writer->close();

    std::printf("\nDone. %llu records -> %s\n",
                static_cast<unsigned long long>(writer->count()), outPath.c_str());
    return 0;
}
