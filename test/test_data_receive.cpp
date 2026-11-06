/**
 * @file test_data_receive.cpp
 * @brief Integration test: RDI/SHII/SPI reception (requires a live sensor)
 *
 * Receives streams for N seconds through the parser-independent payload
 * callback and the typed callbacks, and reports per-stream E2E results.
 * E2E is verification only: failed frames are delivered and counted.
 * PMOI is not sent on interface v1.1.0 and is enabled only by the pmoi option;
 * then it must arrive as well.
 *
 * Usage: test_data_receive [sensor_ip] [duration_sec] [tcp|udp] [options]
 *   csii              also send CSII (port 30501) at 100 ms during the capture
 *   ports=R,H,P       RDI/SHII/SPI ports (default 30509,30510,30511)
 *   pmoi[=PORT]       enable PMOI (Event 0x8005, default port 30512) on sensors
 *                     whose Discovery reports objects_port
 */
#include <afi/afi_sdk.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

struct WireStats {
    std::atomic<uint32_t> frames{0};
    std::atomic<uint32_t> e2e_valid{0};
    std::atomic<uint32_t> counter_bad{0};
    uint32_t last_counter = 0;
    bool have_counter = false;
};

WireStats g_wire[4];
std::atomic<uint32_t> g_rdi{0}, g_shi{0}, g_spi{0}, g_pmoi{0};
std::atomic<uint32_t> g_lost{0};
std::atomic<uint8_t> g_shi_input_status{0xFF};

// Indexed by AfiStreamKind.
const char* kNames[4] = {"RDI", "SHII", "SPI", "PMOI"};

} // namespace

int main(int argc, char* argv[]) {
    afi::AfiSdkConfig config;
    if (argc > 1) config.sensor_ip = argv[1];
    int duration = (argc > 2) ? std::atoi(argv[2]) : 10;
    if (argc > 3) config.stream_transport = argv[3];
    bool send_csii = false;
    bool enable_pmoi = false;
    for (int i = 4; i < argc; ++i) {
        const std::string opt = argv[i];
        if (opt == "csii") send_csii = true;
        else if (opt == "pmoi") enable_pmoi = true;
        else if (opt.rfind("pmoi=", 0) == 0) {
            enable_pmoi = true;
            config.pmoi_port = static_cast<uint16_t>(std::atoi(opt.c_str() + 5));
        }
        else if (opt.rfind("ports=", 0) == 0) {
            unsigned r = 0, h = 0, p = 0;
            if (std::sscanf(opt.c_str() + 6, "%u,%u,%u", &r, &h, &p) == 3) {
                config.rdi_port = static_cast<uint16_t>(r);
                config.shi_port = static_cast<uint16_t>(h);
                config.spi_port = static_cast<uint16_t>(p);
            }
        }
    }

    config.enable_config_api = false;
    config.enable_rdi = true;
    config.enable_shi = true;
    config.enable_spi = true;
    config.enable_pmoi = enable_pmoi;
    config.validate_e2e = true;

    afi::AfiSdk sdk;
    sdk.RegWirePayloadCallback([](const afi::AfiWirePayloadFrame& f) {
        WireStats& w = g_wire[static_cast<int>(f.stream)];
        w.frames.fetch_add(1);
        if (f.e2e_valid) w.e2e_valid.fetch_add(1);
        if (w.have_counter && f.e2e_counter - w.last_counter - 1 >= 0x7FFFFFFFu) {
            w.counter_bad.fetch_add(1);
        }
        w.last_counter = f.e2e_counter;
        w.have_counter = true;
    });
    sdk.RegRecvCallback([](const afi::AfiRdiFrame& f) {
        if (g_rdi.fetch_add(1) == 0) {
            printf("[RDI] first frame: %d detections, counter=%u dq=%d\n",
                   f.message.num_detections, f.message.message_counter,
                   static_cast<int>(f.message.data_qualifier));
        }
    });
    sdk.RegRecvCallback([](const afi::AfiShiMessage& m) {
        if (g_shi.fetch_add(1) == 0) {
            printf("[SHII] first message: %u op modes, %u calib components\n",
                   m.num_valid_operation_modes, m.num_valid_calibration_components);
        }
        if (m.num_valid_input_signal_statuses > 0) {
            g_shi_input_status.store(
                static_cast<uint8_t>(m.sensor_input_signal_statuses[0]));
        }
    });
    sdk.RegRecvCallback([](const afi::AfiSpiMessage& m) {
        if (g_spi.fetch_add(1) == 0) {
            printf("[SPI] first message: pose origin (%.2f, %.2f, %.2f)\n",
                   m.sensor_pose.origin_point_x, m.sensor_pose.origin_point_y,
                   m.sensor_pose.origin_point_z);
        }
    });
    sdk.RegRecvCallback([](const afi::AfiPmoiFrame& f) {
        if (g_pmoi.fetch_add(1) == 0) {
            printf("[PMOI] first frame: %u objects, counter=%u\n",
                   static_cast<unsigned>(f.message.num_pmo), f.message.message_counter);
        }
    });
    sdk.RegRecvCallback([](uint32_t total, uint32_t lost) {
        g_lost.store(lost);
        printf("*** RDI slot loss: %u / %u\n", lost, total);
    });

    if (sdk.Init(config) != afi::kOk) {
        printf("Init failed — sensor unreachable at %s\n", config.sensor_ip.c_str());
        return 1;
    }
    if (sdk.Start() != afi::kOk) {
        printf("Start failed\n");
        return 1;
    }

    printf("Receiving %s streams for %d s%s...\n", config.stream_transport.c_str(),
           duration, send_csii ? " with CSII" : "");
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(duration);
    uint8_t status_before = 0xFF;
    int csii_sent = 0;
    while (std::chrono::steady_clock::now() < end) {
        if (send_csii) {
            if (csii_sent == 0) status_before = g_shi_input_status.load();
            afi::AfiVehicleInput in{};
            in.somc = BTS_CSII_SOMC_NORMAL_MODE;
            in.velocity_x_mps = 5.0f;
            in.gear_position = BTS_CSII_GP_FORWARD;
            if (sdk.SendVehicleInput(in) == afi::kOk) ++csii_sent;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    sdk.Stop();

    printf("\nTyped frames: RDI=%u SHII=%u SPI=%u PMOI=%u\n",
           g_rdi.load(), g_shi.load(), g_spi.load(), g_pmoi.load());
    const int streams = enable_pmoi ? 4 : 3;
    for (int i = 0; i < streams; ++i) {
        const uint32_t n = g_wire[i].frames.load();
        printf("Wire %-7s frames=%u e2e_valid=%u (%.2f%%) counter_bad=%u\n",
               kNames[i], n, g_wire[i].e2e_valid.load(),
               n ? 100.0 * g_wire[i].e2e_valid.load() / n : 0.0,
               g_wire[i].counter_bad.load());
    }
    printf("RDI slot loss=%u\n", g_lost.load());
    if (send_csii) {
        printf("CSII sent=%d, SHII input status before=%u after=%u (0=Valid 2=Missing)\n",
               csii_sent, status_before, g_shi_input_status.load());
    }

    const uint32_t min_expected = static_cast<uint32_t>(duration * 8);
    bool ok = true;
    for (int i = 0; i < streams; ++i) {
        ok = ok && g_wire[i].frames.load() >= min_expected &&
             g_wire[i].e2e_valid.load() == g_wire[i].frames.load() &&
             g_wire[i].counter_bad.load() == 0;
    }
    ok = ok && g_rdi.load() >= min_expected && g_shi.load() >= min_expected &&
         g_spi.load() >= min_expected;
    if (enable_pmoi) ok = ok && g_pmoi.load() >= min_expected;
    if (send_csii) ok = ok && g_shi_input_status.load() == 0;
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
