/**
 * @file test_csii_send.cpp
 * @brief Integration test: CSII vehicle input sending (requires a live sensor)
 *
 * Sends N CSII frames at 100 ms to port 30501 through the public
 * AfiSdk::SendVehicleInput API. The Config API probe picks the transport:
 * UDP on interface v1.1.0, TCP on v1.0.x. All sends succeeding (no ICMP
 * unreachable on UDP, no RST/close on TCP) verifies the outbound path.
 *
 * Usage: test_csii_send [sensor_ip] [frame_count] [udp|tcp]
 */
#include <afi/afi_sdk.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char* argv[]) {
    afi::AfiSdkConfig config;
    if (argc > 1) config.sensor_ip = argv[1];
    int frames = (argc > 2) ? std::atoi(argv[2]) : 20;
    if (argc > 3) config.csii_transport = argv[3];

    config.enable_rdi = false;  // CSII only; the Config API probe picks the transport

    afi::AfiSdk sdk;
    if (sdk.Init(config) != afi::kOk) {
        printf("Init failed\n");
        return 1;
    }
    const afi::AfiInterfaceGeneration gen = sdk.GetInterfaceGeneration();
    const char* transport = !config.csii_transport.empty() ? config.csii_transport.c_str()
                          : gen == afi::AfiInterfaceGeneration::kV1_0 ? "tcp" : "udp";
    printf("Interface generation %s, CSII over %s\n",
           gen == afi::AfiInterfaceGeneration::kV1_0   ? "v1.0.x"
           : gen == afi::AfiInterfaceGeneration::kV1_1 ? "v1.1.0" : "unknown",
           transport);

    afi::AfiVehicleInput input;
    input.somc = BTS_CSII_SOMC_NORMAL_MODE;
    input.velocity_x_mps = 5.0f;
    input.gear_position = BTS_CSII_GP_FORWARD;

    int sent = 0;
    for (int i = 0; i < frames; i++) {
        input.global_timestamp_utc = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()) / 1000.0;
        int rc = sdk.SendVehicleInput(input);
        if (rc != afi::kOk) {
            printf("FAIL: SendVehicleInput frame %d: %s (%d)\n",
                   i, afi::AfiErrorStr(rc), rc);
            sdk.Stop();
            return 1;
        }
        sent++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    sdk.Stop();
    printf("PASS: %d CSII frames sent to %s:%d over %s (115B each, E2E CRC)\n",
           sent, config.sensor_ip.c_str(), config.csii_port, transport);
    return 0;
}
