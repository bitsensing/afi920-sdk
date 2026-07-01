/**
 * @file sample_multi_sensor.cpp
 * @brief Sample: Receive data from multiple AFI920 sensors simultaneously
 *
 * Each AfiSdk instance is fully independent (own sockets, threads, callbacks).
 *
 * Usage: sample_multi_sensor <ip1> [ip2] [ip3] [ip4]
 */
#include <afi/afi_sdk.h>
#include <cstdio>
#include <csignal>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};

struct SensorStats {
    std::atomic<uint32_t> frames{0};
    std::atomic<uint32_t> detections{0};
    std::atomic<uint32_t> lost{0};
};

static void signal_handler(int) { g_running.store(false); }

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <ip1> [ip2] [ip3] [ip4]\n", argv[0]);
        return 1;
    }

    std::signal(SIGINT, signal_handler);

    int num_sensors = argc - 1;
    if (num_sensors > 4) num_sensors = 4;

    std::vector<std::unique_ptr<afi::AfiSdk>> sdks(num_sensors);
    std::vector<std::unique_ptr<SensorStats>> stats(num_sensors);

    // Init each sensor
    for (int i = 0; i < num_sensors; i++) {
        sdks[i] = std::make_unique<afi::AfiSdk>();
        stats[i] = std::make_unique<SensorStats>();

        afi::AfiSdkConfig config;
        config.sensor_ip = argv[i + 1];
        config.enable_rdi = true;
        config.enable_shi = false;
        config.enable_spi = false;
        config.enable_config_api = false;

        printf("Sensor %d: %s (RDI port %d)\n",
               i + 1, config.sensor_ip.c_str(), config.rdi_port);

        // Capture index for callbacks
        auto* st = stats[i].get();

        sdks[i]->RegRecvCallback([st, i](const afi::AfiRdiFrame& frame) {
            st->frames.fetch_add(1);
            st->detections.fetch_add(frame.message.num_detections);
        });

        sdks[i]->RegRecvCallback([st, i](uint32_t total, uint32_t lost) {
            st->lost.store(lost);
        });

        int rc = sdks[i]->Init(config);
        if (rc != afi::kOk) {
            printf("  Init failed: %s (%d)\n", afi::AfiErrorStr(rc), rc);
            return 1;
        }

        rc = sdks[i]->Start();
        if (rc != afi::kOk) {
            printf("  Start failed: %s (%d)\n", afi::AfiErrorStr(rc), rc);
            return 1;
        }
    }

    printf("\nReceiving data from %d sensor(s)... (Ctrl+C to stop)\n\n", num_sensors);

    // Periodic status print
    auto start = std::chrono::steady_clock::now();

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!g_running.load()) break;

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();

        printf("[%llds] ", static_cast<long long>(elapsed));
        for (int i = 0; i < num_sensors; i++) {
            printf("S%d: %u frames (%u det, %u lost)  ",
                   i + 1,
                   stats[i]->frames.load(),
                   stats[i]->detections.load(),
                   stats[i]->lost.load());
        }
        printf("\n");
    }

    // Stop all
    for (int i = 0; i < num_sensors; i++) {
        sdks[i]->Stop();
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();

    // Final summary
    printf("\n=== Final Summary (%lld seconds) ===\n", static_cast<long long>(elapsed));
    for (int i = 0; i < num_sensors; i++) {
        uint32_t f = stats[i]->frames.load();
        uint32_t d = stats[i]->detections.load();
        uint32_t l = stats[i]->lost.load();
        printf("Sensor %d (%s):\n", i + 1, argv[i + 1]);
        printf("  Frames:     %u\n", f);
        printf("  Detections: %u\n", d);
        printf("  Lost:       %u\n", l);
        if (elapsed > 0) {
            printf("  Avg FPS:    %.1f\n", static_cast<double>(f) / elapsed);
        }
    }

    return 0;
}
