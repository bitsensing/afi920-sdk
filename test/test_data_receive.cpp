/**
 * @file test_data_receive.cpp
 * @brief Integration test: RDI/SHII/SPI stream reception (requires a live sensor)
 *
 * Receives streams for N seconds and verifies frames arrive with valid
 * E2E CRCs (strict mode drops bad frames, so received == valid).
 *
 * Usage: test_data_receive [sensor_ip] [duration_sec] [tcp|udp]
 */
#include <afi/afi_sdk.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

static std::atomic<uint32_t> g_rdi{0}, g_shi{0}, g_spi{0};

int main(int argc, char* argv[]) {
    afi::AfiSdkConfig config;
    if (argc > 1) config.sensor_ip = argv[1];
    int duration = (argc > 2) ? std::atoi(argv[2]) : 10;
    if (argc > 3) config.stream_transport = argv[3];

    config.enable_rdi = true;
    config.enable_shi = true;
    config.enable_spi = true;
    config.validate_e2e = true;
    config.e2e_strict = true;  // drop frames with bad CRC → received == valid

    afi::AfiSdk sdk;
    sdk.RegRecvCallback([](const afi::AfiRdiFrame& f) {
        g_rdi.fetch_add(1);
        if (g_rdi.load() == 1) {
            printf("[RDI] first frame: %d detections, counter=%u\n",
                   f.message.num_detections, f.message.message_counter);
        }
    });
    sdk.RegRecvCallback([](const afi::AfiShiMessage& m) {
        g_shi.fetch_add(1);
        if (g_shi.load() == 1) {
            printf("[SHII] first message: %u op modes, %u calib components\n",
                   m.num_valid_operation_modes, m.num_valid_calibration_components);
        }
    });
    sdk.RegRecvCallback([](const afi::AfiSpiMessage& m) {
        g_spi.fetch_add(1);
        if (g_spi.load() == 1) {
            printf("[SPI] first message: pose origin (%.2f, %.2f, %.2f)\n",
                   m.sensor_pose.origin_point_x, m.sensor_pose.origin_point_y,
                   m.sensor_pose.origin_point_z);
        }
    });
    sdk.RegRecvCallback([](uint32_t total, uint32_t lost) {
        printf("*** packet loss: %u / %u\n", lost, total);
    });

    if (sdk.Init(config) != afi::kOk) {
        printf("Init failed — sensor unreachable at %s\n", config.sensor_ip.c_str());
        return 1;
    }
    if (sdk.Start() != afi::kOk) {
        printf("Start failed\n");
        return 1;
    }

    printf("Receiving %s streams for %d s...\n",
           config.stream_transport.c_str(), duration);
    std::this_thread::sleep_for(std::chrono::seconds(duration));
    sdk.Stop();

    printf("\nFrames (E2E-valid): RDI=%u SHII=%u SPI=%u\n",
           g_rdi.load(), g_shi.load(), g_spi.load());

    // At 100 ms cycle expect ~10 Hz per stream; require at least half
    uint32_t min_expected = static_cast<uint32_t>(duration * 5);
    bool ok = g_rdi.load() >= min_expected &&
              g_shi.load() >= min_expected &&
              g_spi.load() >= min_expected;
    printf("%s\n", ok ? "PASS" : "FAIL (insufficient frames)");
    return ok ? 0 : 1;
}
