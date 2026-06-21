/**
 * @file sample_pointcloud.cpp
 * @brief Sample: Receive RDI (point cloud) data from AFI920 sensor
 *
 * Usage: sample_pointcloud [sensor_ip] [tcp|udp]
 */
#include <afi/afi_sdk.h>
#include <cstdio>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>

static std::atomic<bool> g_running{true};
static std::atomic<uint32_t> g_frame_count{0};
static std::atomic<uint32_t> g_total_detections{0};

static void signal_handler(int) { g_running.store(false); }

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);

    // Configure
    afi::AfiSdkConfig config;
    if (argc > 1) config.sensor_ip = argv[1];
    if (argc > 2) config.stream_transport = argv[2];  // "tcp" or "udp"
    config.enable_rdi = true;
    config.enable_shi = false;
    config.enable_spi = false;
    config.enable_config_api = false;  // Data-only mode

    printf("Connecting to sensor at %s (%s) ...\n",
           config.sensor_ip.c_str(), config.stream_transport.c_str());

    afi::AfiSdk sdk;

    // Register RDI callback
    sdk.RegRecvCallback([](const afi::AfiRdiFrame& frame) {
        uint32_t fc = g_frame_count.fetch_add(1) + 1;
        g_total_detections.fetch_add(frame.message.num_detections);

        // Print every 10th frame to avoid flooding
        if (fc % 10 == 0) {
            printf("[Frame %u] detections=%d  counter=%u  timestamp=%llu ns\n",
                   fc,
                   frame.message.num_detections,
                   frame.message.message_counter,
                   static_cast<unsigned long long>(frame.message.timestamp));

            // Print first detection if available
            if (frame.message.num_detections > 0 && frame.message.detections) {
                const auto& d = frame.message.detections[0];
                printf("  [0] r=%.2f m  az=%.3f rad  el=%.3f rad  v=%.2f m/s  rcs=%.1f dBm2\n",
                       d.position_radial_distance,
                       d.position_azimuth,
                       d.position_elevation,
                       d.relative_velocity_radial,
                       d.radar_cross_section);
            }
        }
    });

    // Register packet loss callback
    sdk.RegRecvCallback([](uint32_t total, uint32_t lost) {
        printf("*** Packet loss detected: %u lost / %u total (%.2f%%)\n",
               lost, total, 100.0 * lost / total);
    });

    // Init and start
    int rc = sdk.Init(config);
    if (rc != afi::kOk) {
        printf("Init failed: %s (%d)\n", afi::AfiErrorStr(rc), rc);
        return 1;
    }

    rc = sdk.Start();
    if (rc != afi::kOk) {
        printf("Start failed: %s (%d)\n", afi::AfiErrorStr(rc), rc);
        return 1;
    }

    printf("Receiving RDI data... (Ctrl+C to stop)\n\n");

    // Main loop — just wait
    auto start = std::chrono::steady_clock::now();
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();

    sdk.Stop();

    // Summary
    printf("\n--- Summary ---\n");
    printf("Duration:         %lld seconds\n", static_cast<long long>(elapsed));
    printf("Frames received:  %u\n", g_frame_count.load());
    printf("Total detections: %u\n", g_total_detections.load());
    if (elapsed > 0) {
        printf("Avg frame rate:   %.1f fps\n",
               static_cast<double>(g_frame_count.load()) / elapsed);
    }

    return 0;
}
