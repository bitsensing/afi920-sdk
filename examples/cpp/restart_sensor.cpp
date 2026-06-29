/**
 * restart_sensor.cpp
 * 센서를 재시작 (Restart API 호출)
 * Usage: restart_sensor [sensor_ip]
 */
#include <afi/afi_sdk.h>
#include <cstdio>

int main(int argc, char* argv[]) {
    const char* sensor_ip = (argc > 1) ? argv[1] : "192.168.10.150";

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

    printf("Restarting sensor %s ...\n", sensor_ip);
    rc = sdk.Restart();
    printf("Restart: %s (%d)\n", rc == afi::kOk ? "OK" : afi::AfiErrorStr(rc), rc);

    sdk.Stop();
    return 0;
}
