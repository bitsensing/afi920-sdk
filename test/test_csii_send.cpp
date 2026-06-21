/**
 * @file test_csii_send.cpp
 * @brief Integration test: CSII vehicle input sending (requires a live sensor)
 *
 * Sends N CSII frames at 100 ms over TCP 30501 through the public
 * AfiSdk::SendVehicleInput API. The sensor accepting the connection and
 * all sends succeeding (no RST/close) verifies the outbound path.
 *
 * Usage: test_csii_send [sensor_ip] [frame_count]
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

    config.enable_rdi = false;
    config.enable_config_api = false;  // CSII only

    afi::AfiSdk sdk;
    if (sdk.Init(config) != afi::kOk) {
        printf("Init failed\n");
        return 1;
    }

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
    printf("PASS: %d CSII frames sent to %s:%d (115B each, E2E CRC)\n",
           sent, config.sensor_ip.c_str(), config.csii_port);
    return 0;
}
