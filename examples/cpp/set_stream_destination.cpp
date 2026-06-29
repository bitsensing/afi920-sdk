/**
 * set_stream_destination.cpp
 * Redirect an AFI920 sensor's RDI/SHII/SPI stream destination to a given host IP.
 * Usage: set_stream_destination [sensor_ip] [dest_ip]
 */
#include <afi/afi_sdk.h>
#include <cstdio>

int main(int argc, char* argv[]) {
    const char* sensor_ip = (argc > 1) ? argv[1] : "192.168.10.150";
    const char* dest_ip   = (argc > 2) ? argv[2] : "192.168.10.100";

    printf("Sensor: %s  ->  dest: %s\n", sensor_ip, dest_ip);

    afi::AfiSdkConfig config;
    config.sensor_ip         = sensor_ip;
    config.enable_rdi        = false;
    config.enable_config_api = true;

    afi::AfiSdk sdk;
    int rc = sdk.Init(config);
    if (rc != afi::kOk) {
        printf("Init failed: %s (%d)\n", afi::AfiErrorStr(rc), rc);
        return 1;
    }

    // Before
    afi::AfiNetworkConfig net;
    rc = sdk.GetNetworkConfig(net);
    if (rc == afi::kOk) {
        printf("[Before] Detection: %s:%d (%s)\n",
               net.detection.ip.c_str(), net.detection.port, net.detection.protocol.c_str());
        printf("[Before] Health:    %s:%d (%s)\n",
               net.health.ip.c_str(), net.health.port, net.health.protocol.c_str());
        printf("[Before] Perf:      %s:%d (%s)\n",
               net.performance.ip.c_str(), net.performance.port, net.performance.protocol.c_str());
    }

    // Point all three streams at dest_ip.
    // dst.port is the HOST's receive port (here the defaults 30509/30510/30511);
    // for multiple sensors on one host, offset these per the multi-sensor convention.
    // protocol is forced to "tcp" (the default transport); pass "udp" to change it.
    auto set_stream = [&](afi::AfiStreamKind kind, const char* name, uint16_t port) {
        afi::AfiStreamDestination dst;
        dst.ip       = dest_ip;
        dst.port     = port;
        dst.protocol = "tcp";
        int r = sdk.SetStreamDestination(kind, dst);
        printf("Set %-12s -> %s:%d: %s\n", name, dest_ip, port,
               r == afi::kOk ? "OK" : afi::AfiErrorStr(r));
        return r;
    };

    set_stream(afi::AfiStreamKind::kDetection,    "Detection",   30509);
    set_stream(afi::AfiStreamKind::kHealth,       "Health",      30510);
    set_stream(afi::AfiStreamKind::kPerformance,  "Performance", 30511);

    // After
    rc = sdk.GetNetworkConfig(net);
    if (rc == afi::kOk) {
        printf("[After]  Detection: %s:%d (%s)\n",
               net.detection.ip.c_str(), net.detection.port, net.detection.protocol.c_str());
        printf("[After]  Health:    %s:%d (%s)\n",
               net.health.ip.c_str(), net.health.port, net.health.protocol.c_str());
        printf("[After]  Perf:      %s:%d (%s)\n",
               net.performance.ip.c_str(), net.performance.port, net.performance.protocol.c_str());
    }

    sdk.Stop();
    printf("Done.\n");
    return 0;
}
