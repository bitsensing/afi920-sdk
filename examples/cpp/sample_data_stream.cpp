/**
 * @file sample_data_stream.cpp
 * @brief Sample: Receive RDI/SHII/SPI data streams from AFI920 sensor
 *
 * Demonstrates simultaneous reception of all three data stream types:
 *   - RDI (Radar Detection Interface) — point cloud detections
 *   - SHII (Sensor Health Information) — sensor health status
 *   - SPI (Sensor Performance Interface) — FOV, object types, reference targets
 *
 * Usage: sample_data_stream [sensor_ip] [duration_sec]
 *
 * Copyright 2026 bitsensing Inc. All rights reserved.
 */
#include <afi/afi_sdk.h>
#include <cstdio>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>

// ── Per-stream statistics ──

struct StreamStats {
    std::atomic<uint32_t> frames{0};
    std::atomic<uint64_t> bytes{0};
};

static std::atomic<bool> g_running{true};
static StreamStats g_rdi, g_shi, g_spi;

// First RDI frame snapshot
static std::mutex g_rdi_mutex;
static bool g_rdi_first_captured = false;
static uint32_t g_rdi_first_num_det = 0;
static uint32_t g_rdi_first_counter = 0;
static uint64_t g_rdi_first_timestamp = 0;
static float g_rdi_first_det_range = 0;
static float g_rdi_first_det_az = 0;
static float g_rdi_first_det_el = 0;
static float g_rdi_first_det_vel = 0;
static float g_rdi_first_det_rcs = 0;

static void signal_handler(int) { g_running.store(false); }

static const char* shi_defect_str(bts_shi_sensor_defect_recognised_t d) {
    switch (d) {
        case BTS_SHI_SDR_FULLY_FUNCTIONAL:     return "Fully Functional";
        case BTS_SHI_SDR_NOT_FULLY_FUNCTIONAL:  return "Not Fully Functional";
        case BTS_SHI_SDR_OUT_OF_ORDER:          return "Out of Order";
        default:                                return "Unknown";
    }
}

static const char* shi_voltage_str(bts_shi_supply_voltage_status_t v) {
    switch (v) {
        case BTS_SHI_SVS_LOW:            return "Low";
        case BTS_SHI_SVS_PRE_LOW:        return "Pre-Low";
        case BTS_SHI_SVS_WITHIN_LIMITS:  return "Within Limits";
        case BTS_SHI_SVS_PRE_HIGH:       return "Pre-High";
        case BTS_SHI_SVS_HIGH:           return "High";
        default:                         return "Unknown";
    }
}

static const char* shi_temp_str(bts_shi_sensor_temperature_status_t t) {
    switch (t) {
        case BTS_SHI_STS_UNDER_TEMPERATURE:     return "Under";
        case BTS_SHI_STS_PRE_UNDER_TEMPERATURE:  return "Pre-Under";
        case BTS_SHI_STS_TEMPERATURE_IN_LIMITS:  return "In Limits";
        case BTS_SHI_STS_PRE_OVER_TEMPERATURE:   return "Pre-Over";
        case BTS_SHI_STS_OVER_TEMPERATURE:       return "Over";
        default:                                 return "Unknown";
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);

    const char* sensor_ip = (argc > 1) ? argv[1] : "192.168.10.150";
    int duration = (argc > 2) ? std::atoi(argv[2]) : 10;

    printf("AFI920 Data Stream Receiver\n");
    printf("  Sensor:   %s\n", sensor_ip);
    printf("  Duration: %ds\n\n", duration);

    // Configure — enable all three streams, no Config API needed
    afi::AfiSdkConfig config;
    config.sensor_ip = sensor_ip;
    config.enable_rdi = true;
    config.enable_shi = true;
    config.enable_spi = true;
    config.enable_config_api = false;

    afi::AfiSdk sdk;

    // ── RDI callback ──
    sdk.RegRecvCallback([](const afi::AfiRdiFrame& frame) {
        g_rdi.frames.fetch_add(1);

        // Capture first frame details
        std::lock_guard<std::mutex> lock(g_rdi_mutex);
        if (!g_rdi_first_captured) {
            g_rdi_first_captured = true;
            g_rdi_first_num_det = frame.message.num_detections;
            g_rdi_first_counter = frame.message.message_counter;
            g_rdi_first_timestamp = frame.message.timestamp;
            if (frame.message.num_detections > 0 && frame.message.detections) {
                const auto& d = frame.message.detections[0];
                g_rdi_first_det_range = d.position_radial_distance;
                g_rdi_first_det_az = d.position_azimuth;
                g_rdi_first_det_el = d.position_elevation;
                g_rdi_first_det_vel = d.relative_velocity_radial;
                g_rdi_first_det_rcs = d.radar_cross_section;
            }
        }
    });

    // ── SHII callback ──
    sdk.RegRecvCallback([](const afi::AfiShiMessage& msg) {
        g_shi.frames.fetch_add(1);

        uint32_t fc = g_shi.frames.load();
        if (fc == 1) {
            printf("[SHII] First message received\n");
            printf("  Defect:      %s\n", shi_defect_str(msg.sensor_defect_recognised));
            printf("  Voltage:     %s\n", shi_voltage_str(msg.supply_voltage_status));
            printf("  Temperature: %s\n", shi_temp_str(msg.sensor_temperature_status));
        }
    });

    // ── SPI callback ──
    sdk.RegRecvCallback([](const afi::AfiSpiMessage& msg) {
        g_spi.frames.fetch_add(1);

        uint32_t fc = g_spi.frames.load();
        if (fc == 1) {
            printf("[SPI]  First message received\n");
            printf("  FOV segments:  %d\n", msg.num_fov_segments);
            printf("  Object types:  %d\n", msg.num_recognisable_object_types);
            printf("  Ref targets:   %d\n", msg.num_reference_target_types);
            if (msg.num_fov_segments > 0) {
                const auto& fov = msg.fov_segments[0];
                printf("  FOV[0]: az=[%.1f, %.1f] deg  el=[%.1f, %.1f] deg\n",
                       fov.azimuth_begin * 57.2958f, fov.azimuth_end * 57.2958f,
                       fov.elevation_begin * 57.2958f, fov.elevation_end * 57.2958f);
            }
        }
    });

    // ── Packet loss callback ──
    sdk.RegRecvCallback([](uint32_t total, uint32_t lost) {
        printf("*** Packet loss: %u lost / %u total (%.2f%%)\n",
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

    printf("Listening on UDP ports:\n");
    printf("  RDI:  %d\n", config.rdi_port);
    printf("  SHII: %d\n", config.shi_port);
    printf("  SPI:  %d\n", config.spi_port);
    printf("\nReceiving for %d seconds... (Ctrl+C to stop early)\n\n", duration);

    // Wait for duration or Ctrl+C
    auto start = std::chrono::steady_clock::now();
    auto deadline = start + std::chrono::seconds(duration);
    while (g_running.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();

    sdk.Stop();

    // ── Print results (same table format as Python) ──
    uint32_t rdi_f = g_rdi.frames.load();
    uint32_t shi_f = g_shi.frames.load();
    uint32_t spi_f = g_spi.frames.load();

    printf("\n============================================================\n");
    printf("  %-6s %8s %8s\n", "Stream", "Frames", "Rate");
    printf("  %-6s %8s %8s\n", "------", "--------", "--------");
    if (elapsed > 0) {
        printf("  %-6s %8u %7.1f Hz\n", "RDI",  rdi_f, static_cast<double>(rdi_f) / elapsed);
        printf("  %-6s %8u %7.1f Hz\n", "SHII", shi_f, static_cast<double>(shi_f) / elapsed);
        printf("  %-6s %8u %7.1f Hz\n", "SPI",  spi_f, static_cast<double>(spi_f) / elapsed);
    } else {
        printf("  %-6s %8u %8s\n", "RDI",  rdi_f, "-");
        printf("  %-6s %8u %8s\n", "SHII", shi_f, "-");
        printf("  %-6s %8u %8s\n", "SPI",  spi_f, "-");
    }

    // ── First RDI frame details ──
    {
        std::lock_guard<std::mutex> lock(g_rdi_mutex);
        if (g_rdi_first_captured) {
            printf("\n--- First RDI Frame ---\n");
            printf("  Detections: %u\n", g_rdi_first_num_det);
            printf("  Counter:    %u\n", g_rdi_first_counter);
            printf("  Timestamp:  %llu ns\n", static_cast<unsigned long long>(g_rdi_first_timestamp));
            if (g_rdi_first_num_det > 0) {
                printf("  Detection #0: range=%.2fm az=%.3frad el=%.3frad "
                       "vel=%.2fm/s rcs=%.1fdBm2\n",
                       g_rdi_first_det_range, g_rdi_first_det_az, g_rdi_first_det_el,
                       g_rdi_first_det_vel, g_rdi_first_det_rcs);
            }
        }
    }

    uint32_t total = rdi_f + shi_f + spi_f;
    printf("\nTotal: %u frames in %lld seconds\n", total, static_cast<long long>(elapsed));
    printf("Done.\n");

    return 0;
}
