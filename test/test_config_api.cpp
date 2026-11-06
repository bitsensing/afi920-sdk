/**
 * @file test_config_api.cpp
 * @brief Integration test: Config API round-trip (requires a live sensor)
 *
 * Read-only GETs for every v1.1.0 category. With --write, also enters the
 * diagnostic access mode and writes the same values back. Every SET writes
 * NVM, and the radar stays in diagnostic mode until it reboots.
 *
 * Usage: test_config_api [sensor_ip] [--write]
 */
#include <afi/afi_sdk.h>
#include <cstdio>
#include <string>

static int g_failed = 0;

static void report(const char* name, int rc) {
    if (rc == afi::kOk) {
        printf("  PASS: %s\n", name);
    } else {
        printf("  FAIL: %s (%s, %d)\n", name, afi::AfiErrorStr(rc), rc);
        g_failed++;
    }
}

int main(int argc, char* argv[]) {
    afi::AfiSdkConfig config;
    bool write = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--write") write = true;
        else config.sensor_ip = argv[i];
    }
    config.enable_rdi = false;

    afi::AfiSdk sdk;
    if (sdk.Init(config) != afi::kOk) {
        printf("Init failed — sensor unreachable at %s\n", config.sensor_ip.c_str());
        return 1;
    }

    // ── GET round-trip across all categories ──
    afi::AfiDeviceInfo dev;          report("GetDeviceInfo", sdk.GetDeviceInfo(dev));
    afi::AfiSensorStatus status;     report("GetSensorStatus", sdk.GetSensorStatus(status));
    afi::AfiDiagnosticInfo diag;     report("GetDiagnosticInfo", sdk.GetDiagnosticInfo(diag));
    afi::AfiNetworkConfig net;       report("GetNetworkConfig", sdk.GetNetworkConfig(net));
    afi::AfiRangeMode mode;          report("GetRangeMode", sdk.GetRangeMode(mode));
    afi::AfiDetectionThreshold th;   report("GetDetectionThreshold", sdk.GetDetectionThreshold(th));
    afi::AfiDetectionFilters filt;   report("GetDetectionFilters", sdk.GetDetectionFilters(filt));
    afi::AfiPtpConfig ptp;           report("GetPtpConfig", sdk.GetPtpConfig(ptp));
    afi::AfiMountingPosition pos;    report("GetMountingPosition", sdk.GetMountingPosition(pos));
    afi::AfiRotation rot;            report("GetRotation", sdk.GetRotation(rot));
    afi::AfiSensorPosition spos;     report("GetSensorPosition", sdk.GetSensorPosition(spos));
    afi::AfiBootInfo boot;           report("GetBootInfo", sdk.GetBootInfo(boot));
    afi::AfiConfigStatus cstat;      report("GetConfigStatus", sdk.GetConfigStatus(cstat));
    afi::AfiModeStatus access;       report("GetMode", sdk.GetMode(access));

    printf("\n  Device: %s %s sw=%s\n", dev.model_name.c_str(),
           dev.serial_number.c_str(), dev.software_version.c_str());

    if (!write) {
        sdk.Stop();
        printf("\n%s (%d failures, read-only; --write adds write-backs)\n",
               g_failed ? "FAILED" : "ALL PASS", g_failed);
        return g_failed ? 1 : 0;
    }

    // ── Write-backs of the same values (diagnostic access mode) ──
    report("EnterMode(diagnostic)", sdk.EnterMode(afi::AfiAccessMode::kDiagnostic));
    report("SetRangeMode(same)", sdk.SetRangeMode(mode));
    report("SetDetectionThreshold(same)", sdk.SetDetectionThreshold(th));
    report("SetDetectionFilters(same)", sdk.SetDetectionFilters(filt));
    report("SetMountingPosition(same)", sdk.SetMountingPosition(pos));
    report("SetRotation(same)", sdk.SetRotation(rot));

    sdk.Stop();
    printf("\n%s (%d failures)\n", g_failed ? "FAILED" : "ALL PASS", g_failed);
    return g_failed ? 1 : 0;
}
