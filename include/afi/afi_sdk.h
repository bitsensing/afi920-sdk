/**
 * @file afi_sdk.h
 * @brief AFI920 4D Imaging Radar SDK
 *
 * Main facade class for interacting with AFI920 sensors.
 * Provides data streaming (RDI/SHII/SPI), sensor discovery,
 * and configuration API.
 *
 * Usage:
 * @code
 *   afi::AfiSdk sdk;
 *   afi::AfiSdkConfig config;
 *   config.sensor_ip = "192.168.10.150";
 *
 *   sdk.Init(config);
 *   sdk.RegRecvCallback([](const afi::AfiRdiFrame& frame) {
 *       printf("Detections: %d\n", frame.message.num_detections);
 *   });
 *   sdk.Start();
 *   // ... application loop ...
 *   sdk.Stop();
 * @endcode
 *
 * For multi-sensor, create one AfiSdk instance per sensor:
 * @code
 *   afi::AfiSdk sdk1, sdk2;
 *   config1.sensor_ip = "192.168.10.150"; config1.rdi_port = 30509;
 *   config2.sensor_ip = "192.168.10.151"; config2.rdi_port = 30609;
 *   sdk1.Init(config1); sdk2.Init(config2);
 * @endcode
 *
 * @copyright Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi_types.h"
#include "afi_config.h"
#include "afi_error.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace afi {

class AfiSdk {
public:
    AfiSdk();
    ~AfiSdk();

    AfiSdk(const AfiSdk&) = delete;
    AfiSdk& operator=(const AfiSdk&) = delete;

    // ─── Lifecycle ───

    /**
     * @brief Initialize SDK with given configuration
     * @param config SDK configuration
     * @return kOk on success, negative error code on failure
     */
    int Init(const AfiSdkConfig& config);

    /**
     * @brief Start data reception threads
     *
     * Begins receiving data on configured UDP ports.
     * Callbacks will be invoked from receiver threads.
     * @return kOk on success
     */
    int Start();

    /**
     * @brief Stop all threads and disconnect
     * @return kOk on success
     */
    int Stop();

    /**
     * @brief Check if SDK is currently receiving data
     */
    bool IsRunning() const;

    // ─── Data Callbacks ───

    /**
     * @brief Register RDI (Radar Detection Interface) callback
     * @param cb Called for each RDI frame (from receiver thread)
     */
    void RegRecvCallback(std::function<void(const AfiRdiFrame&)> cb);

    /**
     * @brief Register SHII (Sensor Health Information) callback
     * @param cb Called for each SHII message (from receiver thread)
     */
    void RegRecvCallback(std::function<void(const AfiShiMessage&)> cb);

    /**
     * @brief Register SPI (Sensor Performance Interface) callback
     * @param cb Called for each SPI message (from receiver thread)
     */
    void RegRecvCallback(std::function<void(const AfiSpiMessage&)> cb);

    /**
     * @brief Register packet loss callback
     * @param cb Called with (total_packets, lost_packets) from receiver thread
     */
    void RegRecvCallback(std::function<void(uint32_t total, uint32_t lost)> cb);

    // ─── Vehicle Input (CSII, Host → Radar) ───

    /**
     * @brief Send vehicle dynamics to the sensor (CSII stream)
     *
     * Publishes one CSII message (Event 0x8001) over TCP 30501 with the
     * E2E Profile 7 header. Call cyclically (100 ms recommended); the SDK
     * maintains the message/E2E counters and connects on first use.
     *
     * @param input Vehicle dynamics and sensor operation command
     * @return kOk on success, negative error code on failure
     */
    int SendVehicleInput(const AfiVehicleInput& input);

    // ─── Discovery ───

    /**
     * @brief Discover AFI920 sensors on the network
     *
     * Sends UDP broadcast on port 30520 and collects responses.
     * Static method — no AfiSdk instance required.
     *
     * @param timeout_ms Discovery timeout in milliseconds (default: 3000)
     * @param port Discovery port (default: 30520)
     * @param bind_ip Local IP to bind for broadcast (empty = all interfaces)
     * @return Vector of discovered sensor information
     */
    static std::vector<AfiSensorInfo> Discover(int timeout_ms = 3000,
                                                uint16_t port = 30520,
                                                const std::string& bind_ip = "");

    // ─── Config API: Device Info & Status (0x001x) ───

    int GetDeviceInfo(AfiDeviceInfo& out);
    int GetSensorStatus(AfiSensorStatus& out);
    int GetDiagnosticInfo(AfiDiagnosticInfo& out);
    int ClearFaultLog();
    int Restart();
    int ResetAllSettings();
    int GetCommandHistoryInfo(AfiCommandHistory& out);

    // ─── Config API: Network (0x002x) ───

    int GetNetworkConfig(AfiNetworkConfig& out);
    /// Full update. The sensor reports restart_required=true when radar
    /// network fields change — apply with Restart().
    int SetNetworkConfig(const AfiNetworkConfig& config, bool* restart_required = nullptr);
    /// Partial update of one stream destination (applies immediately)
    int SetStreamDestination(AfiStreamKind kind, const AfiStreamDestination& dst);

    // ─── Config API: Range Mode (0x003x) ───

    int GetRangeMode(AfiRangeMode& out);
    int SetRangeMode(AfiRangeMode mode);

    // ─── Config API: Detection (0x004x) ───

    int GetDetectionThreshold(AfiDetectionThreshold& out);
    int SetDetectionThreshold(const AfiDetectionThreshold& threshold);
    int GetDetectionFilters(AfiDetectionFilters& out);
    int SetDetectionFilters(const AfiDetectionFilters& filters);
    int GetDetectionDensity(AfiDetectionDensity& out);
    int SetDetectionDensity(const AfiDetectionDensity& density);

    // ─── Config API: Time Sync (0x005x) ───

    /// Returns PTP profile plus live sync status (state, offset, master)
    int GetPtpConfig(AfiPtpConfig& out);
    /// Only the profile is configurable (currently profile 0 only)
    int SetPtpConfig(uint8_t profile);

    // ─── Config API: Mounting (0x007x) ───

    int GetMountingPosition(AfiMountingPosition& out);
    int SetMountingPosition(const AfiMountingPosition& pos);
    int GetRotation(AfiRotation& out);
    int SetRotation(const AfiRotation& rot);
    int GetSensorPosition(AfiSensorPosition& out);
    int SetSensorPosition(const AfiSensorPosition& pos);

    // ─── Config API: Config Persistence (0x009x) ───

    int SaveConfig();
    int GetConfigStatus(AfiConfigStatus& out);
    int ResetConfigDefaults(const std::vector<std::string>& keys,
                            AfiResetConfigDefaultsResult* out = nullptr);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace afi
