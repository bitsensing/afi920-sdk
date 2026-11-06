/**
 * @file restart_sensor.cpp
 * @brief Sample: Restart the sensor and wait until the Config API answers again
 *
 * Restart is accepted in every access mode. The sensor answers before it
 * resets, so the Config API connection drops after the reply;
 * ReconnectConfigApi waits for the sensor, which comes back in Normal mode.
 *
 * Usage: restart_sensor [sensor_ip]
 */
#include <afi/afi_sdk.h>
#include <cstdio>

int main(int argc, char* argv[]) {
    afi::AfiSdkConfig config;
    if (argc > 1) config.sensor_ip = argv[1];
    config.enable_rdi = false;  // Config API only
    config.enable_config_api = true;

    afi::AfiSdk sdk;
    int rc = sdk.Init(config);
    if (rc != afi::kOk) {
        printf("Init failed: %s (%d)\n", afi::AfiErrorStr(rc), rc);
        return 1;
    }

    printf("Restarting sensor %s ...\n", config.sensor_ip.c_str());
    rc = sdk.Restart();
    printf("Restart: %s (%d)\n", afi::AfiErrorStr(rc), rc);
    if (rc != afi::kOk) {
        sdk.Stop();
        return 1;
    }

    rc = sdk.ReconnectConfigApi(60000);
    printf("Reconnect: %s (%d)\n", afi::AfiErrorStr(rc), rc);
    sdk.Stop();
    return rc == afi::kOk ? 0 : 1;
}
