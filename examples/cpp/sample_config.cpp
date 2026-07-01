/**
 * @file sample_config.cpp
 * @brief Sample: Read and write sensor configuration via Config API
 *
 * Usage: sample_config [sensor_ip]
 */
#include <afi/afi_sdk.h>
#include <cstddef>
#include <cstdio>

static void print_result(const char* name, int rc) {
    if (rc == afi::kOk) return;
    printf("  %s: %s (%d)\n", name, afi::AfiErrorStr(rc), rc);
}

static void print_cfar_table(const char* name, const std::vector<uint8_t>& table) {
    printf("  %s: [", name);
    for (std::size_t i = 0; i < table.size(); ++i) {
        printf("%s%u", i == 0 ? "" : ", ", static_cast<unsigned>(table[i]));
    }
    printf("]\n");
}

int main(int argc, char* argv[]) {
    afi::AfiSdkConfig config;
    if (argc > 1) config.sensor_ip = argv[1];
    config.enable_rdi = false;  // Config API only
    config.enable_config_api = true;

    printf("Connecting to %s:%d ...\n", config.sensor_ip.c_str(), config.config_port);

    afi::AfiSdk sdk;
    int rc = sdk.Init(config);
    if (rc != afi::kOk) {
        printf("Init failed: %s (%d)\n", afi::AfiErrorStr(rc), rc);
        return 1;
    }

    // --- Device Info ---
    printf("\n=== Device Info ===\n");

    afi::AfiDeviceInfo dev;
    rc = sdk.GetDeviceInfo(dev);
    print_result("GetDeviceInfo", rc);
    if (rc == afi::kOk) {
        printf("  Serial:     %s\n", dev.serial_number.c_str());
        printf("  Model:      %s\n", dev.model_name.c_str());
        printf("  HW Version: %s\n", dev.hw_version.c_str());
        printf("  SW Version: %s\n", dev.software_version.c_str());
        printf("  Hashes:     bl=%s drv=%s fw=%s\n",
               dev.bootloader_hash.c_str(), dev.driver_hash.c_str(), dev.fw_hash.c_str());
        printf("  MAC:        %s\n", dev.mac_address.c_str());
    }

    afi::AfiSensorStatus status;
    rc = sdk.GetSensorStatus(status);
    print_result("GetSensorStatus", rc);
    if (rc == afi::kOk) {
        printf("  Op State:   %s (%d)\n", status.operation_state.c_str(), status.operation_state_code);
        printf("  MCU Temp:   %.1f C\n", status.temperature_mcu);
        printf("  RF Temp:    %.1f C\n", status.temperature_rf);
        printf("  Voltage:    %.2f V\n", status.voltage);
        printf("  Uptime:     %u sec\n", status.uptime_sec);
    }

    afi::AfiDiagnosticInfo diag;
    rc = sdk.GetDiagnosticInfo(diag);
    print_result("GetDiagnosticInfo", rc);
    if (rc == afi::kOk) {
        printf("  Combined:   %s (%d), active faults: %u\n",
               diag.combined_state.c_str(), diag.combined_state_code,
               diag.active_fault_count);
        for (const auto& f : diag.faults) {
            printf("    [%s] %s %s level=%s active=%d\n",
                   f.source.c_str(), f.id.c_str(), f.name.c_str(),
                   f.level.c_str(), f.active ? 1 : 0);
        }
    }

    // --- Network ---
    printf("\n=== Network Config ===\n");

    afi::AfiNetworkConfig net;
    rc = sdk.GetNetworkConfig(net);
    print_result("GetNetworkConfig", rc);
    if (rc == afi::kOk) {
        printf("  Radar IP:   %s\n", net.ip_address.c_str());
        printf("  Subnet:     %s\n", net.subnet_mask.c_str());
        printf("  Gateway:    %s\n", net.gateway.c_str());
        printf("  API Port:   %d\n", net.api_port);
        printf("  Detection:  %s:%d (%s)\n", net.detection.ip.c_str(),
               net.detection.port, net.detection.protocol.c_str());
        printf("  Health:     %s:%d (%s)\n", net.health.ip.c_str(),
               net.health.port, net.health.protocol.c_str());
        printf("  Perf:       %s:%d (%s)\n", net.performance.ip.c_str(),
               net.performance.port, net.performance.protocol.c_str());
    }

    // --- Range Mode ---
    printf("\n=== Range Mode ===\n");

    afi::AfiRangeMode mode;
    rc = sdk.GetRangeMode(mode);
    print_result("GetRangeMode", rc);
    if (rc == afi::kOk) {
        const char* mode_names[] = {"DR", "LR", "MR", "SR", "ULR", "USR"};
        uint8_t m = static_cast<uint8_t>(mode);
        printf("  Mode: %s (%d)\n", (m < 6) ? mode_names[m] : "Unknown", m);
    }

    // --- Detection ---
    printf("\n=== Detection ===\n");

    afi::AfiDetectionThreshold thresh;
    rc = sdk.GetDetectionThreshold(thresh);
    print_result("GetDetectionThreshold", rc);
    if (rc == afi::kOk) {
        printf("  SNR:  %.1f dB\n", thresh.snr_dB);
        print_cfar_table("CFAR first", thresh.cfar_table_first_frame);
        print_cfar_table("CFAR second", thresh.cfar_table_second_frame);
    }

    afi::AfiDetectionFilters filters;
    rc = sdk.GetDetectionFilters(filters);
    print_result("GetDetectionFilters", rc);
    if (rc == afi::kOk) {
        printf("  FOV:               %s\n", filters.filter_fov ? "ON" : "OFF");
        printf("  APS noise:         %s\n", filters.filter_aps_noise ? "ON" : "OFF");
        printf("  Ego specular:      %s\n", filters.filter_ego_specular ? "ON" : "OFF");
        printf("  Ground sidelobe:   %s\n", filters.filter_ground_sidelobe ? "ON" : "OFF");
        printf("  Upper structure:   %s\n", filters.filter_upper_structure ? "ON" : "OFF");
        printf("  Multibounce 2x:    %s\n", filters.filter_multibounce_2x ? "ON" : "OFF");
        printf("  Struct multipath:  %s\n", filters.filter_structure_multipath ? "ON" : "OFF");
        printf("  Specular mirror:   %s\n", filters.filter_specular_mirror ? "ON" : "OFF");
        printf("  Wheel microdopp.:  %s\n", filters.filter_wheel_microdoppler ? "ON" : "OFF");
    }

    afi::AfiDetectionDensity density;
    rc = sdk.GetDetectionDensity(density);
    print_result("GetDetectionDensity", rc);
    if (rc == afi::kOk) {
        printf("  Range density: %s\n", density.range_density ? "ON" : "OFF");
        printf("  Angle density: %s\n", density.angle_density ? "ON" : "OFF");
    }

    // --- Time Sync ---
    printf("\n=== Time Sync (PTP) ===\n");

    afi::AfiPtpConfig ptp;
    rc = sdk.GetPtpConfig(ptp);
    print_result("GetPtpConfig", rc);
    if (rc == afi::kOk) {
        printf("  Profile: %d (%s)\n", ptp.profile, ptp.profile_name.c_str());
        printf("  State:   %s, offset=%lld ns, master=%s\n",
               ptp.state.c_str(), static_cast<long long>(ptp.offset_ns),
               ptp.master_clock_id.c_str());
    }

    // --- Mounting ---
    printf("\n=== Mounting ===\n");

    afi::AfiMountingPosition pos;
    rc = sdk.GetMountingPosition(pos);
    print_result("GetMountingPosition", rc);
    if (rc == afi::kOk) {
        printf("  Position: (%.3f, %.3f, %.3f) m\n", pos.x, pos.y, pos.z);
    }

    afi::AfiRotation rot;
    rc = sdk.GetRotation(rot);
    print_result("GetRotation", rc);
    if (rc == afi::kOk) {
        printf("  Rotation: roll=%.1f pitch=%.1f yaw=%.1f deg\n",
               rot.roll, rot.pitch, rot.yaw);
    }

    sdk.Stop();
    printf("\nDone.\n");
    return 0;
}
