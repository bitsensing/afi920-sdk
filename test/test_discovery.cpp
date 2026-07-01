/**
 * @file test_discovery.cpp
 * @brief Integration test: sensor discovery (requires a live sensor)
 *
 * Usage: test_discovery [timeout_ms] [bind_ip]
 */
#include <afi/afi_sdk.h>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
    int timeout_ms = (argc > 1) ? std::atoi(argv[1]) : 3000;
    std::string bind_ip = (argc > 2) ? argv[2] : "";

    printf("Discovering sensors (timeout %d ms)...\n", timeout_ms);
    auto sensors = afi::AfiSdk::Discover(timeout_ms, 30520, bind_ip);

    printf("Found %zu sensor(s)\n\n", sensors.size());
    for (size_t i = 0; i < sensors.size(); i++) {
        const auto& s = sensors[i];
        printf("[%zu] %s (%s) at %s:%d\n", i, s.model.c_str(),
               s.serial_number.c_str(), s.ip.c_str(), s.port);
        printf("    sw=%s (bl=%s drv=%s fw=%s)\n", s.software_version.c_str(),
               s.bootloader_hash.c_str(), s.driver_hash.c_str(), s.fw_hash.c_str());
        printf("    range_mode=%s(%d) ports: det=%d shi=%d spi=%d\n",
               s.range_mode_name.c_str(), static_cast<int>(s.range_mode),
               s.detection_port, s.health_port, s.performance_port);
        printf("    scan_index=%u uptime=%us ptp=%s\n",
               s.scan_index, s.uptime_seconds, s.ptp_state.c_str());
    }

    // Duplicate responses (unicast + broadcast) must have been deduped
    for (size_t i = 0; i < sensors.size(); i++) {
        for (size_t j = i + 1; j < sensors.size(); j++) {
            if (sensors[i].serial_number == sensors[j].serial_number &&
                sensors[i].ip == sensors[j].ip) {
                printf("FAIL: duplicate sensor entry (dedup broken)\n");
                return 1;
            }
        }
    }

    return sensors.empty() ? 1 : 0;
}
