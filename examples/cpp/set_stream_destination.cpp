/**
 * @file set_stream_destination.cpp
 * @brief Sample: Point the sensor's RDI/SHII/SPI streams at a host
 *
 * Enters the diagnostic access mode, writes the three stream destinations,
 * reads them back, and restarts the sensor: the sensor stores each SET in NVM
 * when it answers, and stored values take effect after a restart.
 *
 * dst.port is the host's receive port (defaults 30509/30510/30511). For
 * several sensors on one host, offset the ports per sensor (+100).
 *
 * Usage: set_stream_destination [sensor_ip] [dest_ip] [tcp|udp]
 */
#include <afi/afi_sdk.h>
#include <cstdio>
#include <string>

static void print_destinations(const char* label, const afi::AfiNetworkConfig& net) {
    printf("[%s] Detection: %s:%d (%s)\n", label,
           net.detection.ip.c_str(), net.detection.port, net.detection.protocol.c_str());
    printf("[%s] Health:    %s:%d (%s)\n", label,
           net.health.ip.c_str(), net.health.port, net.health.protocol.c_str());
    printf("[%s] Perf:      %s:%d (%s)\n", label,
           net.performance.ip.c_str(), net.performance.port, net.performance.protocol.c_str());
}

int main(int argc, char* argv[]) {
    afi::AfiSdkConfig config;
    if (argc > 1) config.sensor_ip = argv[1];
    const std::string dest_ip  = (argc > 2) ? argv[2] : "192.168.10.100";
    const std::string protocol = (argc > 3) ? argv[3] : "tcp";
    config.enable_rdi = false;  // Config API only
    config.enable_config_api = true;

    printf("Sensor: %s  ->  dest: %s (%s)\n",
           config.sensor_ip.c_str(), dest_ip.c_str(), protocol.c_str());

    afi::AfiSdk sdk;
    int rc = sdk.Init(config);
    if (rc != afi::kOk) {
        printf("Init failed: %s (%d)\n", afi::AfiErrorStr(rc), rc);
        return 1;
    }

    afi::AfiNetworkConfig net;
    if (sdk.GetNetworkConfig(net) == afi::kOk) print_destinations("Before", net);

    // Every config SET needs the diagnostic access mode.
    rc = sdk.EnterMode(afi::AfiAccessMode::kDiagnostic);
    if (rc != afi::kOk) {
        printf("EnterMode(Diagnostic) failed: %s (%d)\n", afi::AfiErrorStr(rc), rc);
        sdk.Stop();
        return 1;
    }

    auto set_stream = [&](afi::AfiStreamKind kind, const char* name, uint16_t port) {
        afi::AfiStreamDestination dst;
        dst.ip       = dest_ip;
        dst.port     = port;
        dst.protocol = protocol;
        int r = sdk.SetStreamDestination(kind, dst);
        printf("Set %-11s -> %s:%d: %s\n", name, dest_ip.c_str(), port, afi::AfiErrorStr(r));
        return r;
    };

    int failed = 0;
    failed += set_stream(afi::AfiStreamKind::kDetection,   "Detection",   30509) != afi::kOk;
    failed += set_stream(afi::AfiStreamKind::kHealth,      "Health",      30510) != afi::kOk;
    failed += set_stream(afi::AfiStreamKind::kPerformance, "Performance", 30511) != afi::kOk;

    // A GET returns the stored values, which run after the restart below.
    if (sdk.GetNetworkConfig(net) == afi::kOk) print_destinations("Stored", net);
    if (failed) {
        printf("%d destination(s) were not stored; not restarting.\n", failed);
        sdk.Stop();
        return 1;
    }

    printf("Restarting the sensor to apply ...\n");
    rc = sdk.Restart();
    if (rc == afi::kOk) rc = sdk.ReconnectConfigApi(60000);
    printf("Restart: %s (%d)\n", afi::AfiErrorStr(rc), rc);
    sdk.Stop();
    return rc == afi::kOk ? 0 : 1;
}
