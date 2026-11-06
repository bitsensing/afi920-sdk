/**
 * @file afi_sdk.h
 * @brief AFI920 4D Imaging Radar SDK
 *
 * Main facade class for interacting with AFI920 sensors.
 * Provides data streaming (RDI/SHII/SPI/PMOI), sensor discovery,
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
 * For multi-sensor, create one AfiSdk instance per sensor. Over TCP (the
 * default) every instance keeps the default ports. Over UDP the host binds the
 * stream ports, so each sensor needs its own (+100 per sensor), written to that
 * sensor's stream destinations:
 * @code
 *   afi::AfiSdk sdk1, sdk2;
 *   afi::AfiSdkConfig config1, config2;
 *   config1.sensor_ip = "192.168.10.150"; config1.stream_transport = "udp";
 *   config2.sensor_ip = "192.168.10.151"; config2.stream_transport = "udp";
 *   config2.rdi_port = 30609; config2.shi_port = 30610; config2.spi_port = 30611;
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
     * Begins receiving data on the configured TCP/UDP stream endpoints.
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

    /// SHII callback that also receives the E2E outcome and receive time.
    /// Replaces the plain SHII callback when both are registered.
    void RegRecvCallback(std::function<void(const AfiShiMessage&, const AfiStreamMeta&)> cb);

    /// SPI callback that also receives the E2E outcome and receive time.
    /// Replaces the plain SPI callback when both are registered.
    void RegRecvCallback(std::function<void(const AfiSpiMessage&, const AfiStreamMeta&)> cb);

    /**
     * @brief Register PMOI (Potentially Moving Object Interface) callback
     * @param cb Called for each PMOI frame (from receiver thread)
     *
     * Needs AfiSdkConfig::enable_pmoi. Sensors that send PMOI (Event 0x8005)
     * report objects_port in Discovery; interface v1.1.0 reserves
     * the event and does not send it.
     */
    void RegRecvCallback(std::function<void(const AfiPmoiFrame&)> cb);

    /**
     * @brief Register the RDI send-slot loss callback
     *
     * Loss is counted from gaps in the RDI E2E Counter, which advances once
     * per send slot (System Spec §8.2). MessageCounter repeats on DQ 6
     * resends and is not used. A counter decrease (radar restart) starts a
     * new baseline. Only frames whose E2E check passed advance the count
     * (the Counter is outside the CRC); with validate_e2e off every frame
     * does. A gap includes slots the radar did not send to this host, such
     * as those before the first sealed scan or while a TCP connection was
     * backlogged.
     * @param cb Called with RDI (total_frames, lost_frames) from its receiver thread
     */
    void RegRecvCallback(std::function<void(uint32_t total, uint32_t lost)> cb);

    /**
     * @brief Register a parser-independent wire-payload callback.
     *
     * The callback receives reassembled RDI, SHII, SPI, and PMOI payloads
     * before their version-specific parsers run. E2E is verified but
     * never used to drop a frame (unless e2e_strict is set); the result is in
     * AfiWirePayloadFrame::e2e_valid. TCP receive sizing and TP reassembly
     * are domain-schema-independent up to the SDK's generic 64 MiB per-frame
     * transport safety ceiling.
     */
    void RegWirePayloadCallback(
        std::function<void(const AfiWirePayloadFrame&)> cb);


    // ─── Vehicle Input (CSII, Host → Radar) ───

    /**
     * @brief Send vehicle dynamics to the sensor (CSII stream)
     *
     * Publishes one CSII message (Event 0x8001) to the sensor's port 30501
     * with the 20B E2E header (CRC-32): one UDP datagram on interface v1.1.0,
     * over a TCP connection on v1.0.x (AfiSdkConfig::csii_transport). Call
     * cyclically (100 ms recommended); the SDK maintains the message/E2E
     * counters and creates the socket on first use.
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

    // ─── Config API: connection and last result ───

    /**
     * @brief Radar outcome of the last Config API call
     *
     * Holds the radar's return code and name (e.g. SESSION_REQUIRED), the
     * firmware update error_message, and the response E2E verdict. The int
     * each Config API method returns maps the same code to an AfiError.
     */
    AfiSensorResult GetLastSensorResult() const;

    /**
     * @brief Interface generation of the sensor
     *
     * From the GetDeviceInfo probe that opens each Config API connection,
     * else from the last stream frame that passed the E2E check; kUnknown
     * before either. On kV1_0 every Config API call returns kNotSupported
     * without sending, while RDI, SHII, SPI and CSII work as on kV1_1.
     */
    AfiInterfaceGeneration GetInterfaceGeneration() const;

    /**
     * @brief Reconnect the Config API, retrying until timeout_ms
     *
     * Restart (0x0014) and FirmwareUpdateReboot (0x0086) answer before the
     * radar resets, so the connection dies after the reply. Config API calls
     * also reconnect on demand; this waits for the radar to come back.
     *
     * @return kOk, kNotInitialized, or kConnectFailed after timeout_ms
     */
    int ReconnectConfigApi(int timeout_ms = 60000);

    // ─── Config API: Access Mode (0x00Ex) ───

    /// Current access mode, time in it, and whether data streams are halted.
    int GetMode(AfiModeStatus& out);

    /**
     * @brief Enter the next access mode (SetMode 0x00E1)
     *
     * Diagnostic is accepted from Normal, Programming only from Diagnostic.
     * Entering Programming halts RDI/SHII/SPI, and only a reboot
     * returns to Normal. Re-entry or a reverse step answers kAccessDenied;
     * Programming requested from Normal answers kSessionRequired.
     */
    int SetMode(AfiAccessMode mode, AfiModeStatus* out = nullptr);

    /**
     * @brief Reach `target` from the current mode (GetMode, then SetMode steps)
     *
     * kOk when already there. kAccessDenied when the radar is in Programming
     * and `target` is Diagnostic (reboot first). Config SETs need
     * kDiagnostic; the firmware update chain needs kProgramming.
     */
    int EnterMode(AfiAccessMode target);

    // ─── Config API: Device Info & Status (0x001x) ───

    int GetDeviceInfo(AfiDeviceInfo& out);
    int GetSensorStatus(AfiSensorStatus& out);
    int GetDiagnosticInfo(AfiDiagnosticInfo& out);
    int Restart();
    int ResetAllSettings();
    int GetCommandHistoryInfo(AfiCommandHistory& out);

    // ─── Config API: Network (0x002x) ───

    int GetNetworkConfig(AfiNetworkConfig& out);
    /// Full update of the radar network and stream destinations (diagnostic
    /// mode; NVM; applies after Restart, restart_required=true).
    int SetNetworkConfig(const AfiNetworkConfig& config, bool* restart_required = nullptr);
    /// Partial update of one stream destination (NVM; applies after Restart)
    int SetStreamDestination(AfiStreamKind kind, const AfiStreamDestination& dst);

    // ─── Config API: Range Mode (0x003x) ───

    int GetRangeMode(AfiRangeMode& out);
    /// Stored in NVM; takes effect after Restart (the sensor reports
    /// restart_required=true).
    int SetRangeMode(AfiRangeMode mode);
    int SetRangeMode(AfiRangeMode mode, bool* restart_required);

    // ─── Config API: Detection (0x004x) ───

    int GetDetectionThreshold(AfiDetectionThreshold& out);
    int SetDetectionThreshold(const AfiDetectionThreshold& threshold);
    int GetDetectionFilters(AfiDetectionFilters& out);
    int SetDetectionFilters(const AfiDetectionFilters& filters);

    // ─── Config API: Time Sync (0x005x) ───

    /// Returns PTP profile plus live sync status (state, offset, master)
    int GetPtpConfig(AfiPtpConfig& out);
    /// Select 0=gPTP, 1=1588v2-L2, or 2=1588v2-UDP (reboot-to-apply)
    int SetPtpConfig(uint8_t profile);
    /// Same operation, also returning whether the sensor requires a restart
    int SetPtpConfig(uint8_t profile, bool* restart_required);

    // ─── Config API: Mounting (0x007x) ───

    int GetMountingPosition(AfiMountingPosition& out);
    int SetMountingPosition(const AfiMountingPosition& pos);
    int GetRotation(AfiRotation& out);
    int SetRotation(const AfiRotation& rot);
    int GetSensorPosition(AfiSensorPosition& out);
    int SetSensorPosition(const AfiSensorPosition& pos);

    // ─── Config API: Firmware Update (0x008x) ───

    int FirmwareUpdateStart(const AfiOtaStartRequest& req, AfiOtaStartResponse& out);
    int FirmwareDataChunk(const uint8_t* data, size_t len);
    /// Same, reporting the accepted total: resume at out->received_bytes.
    int FirmwareDataChunk(const uint8_t* data, size_t len, AfiOtaChunkResult* out);
    int FirmwareUpdateCommit();
    int FirmwareUpdateStatus(AfiOtaStatus& out);
    int FirmwareUpdateAbort();
    int FirmwareSwitchSlot(const std::string& update_type, AfiOtaSwitchSlotResult& out);
    int FirmwareUpdateReboot();
    int GetBootInfo(AfiBootInfo& out);

    // ─── Config API: Config Persistence (0x009x) ───

    int GetConfigStatus(AfiConfigStatus& out);
    int ResetConfigDefaults(const std::vector<std::string>& keys,
                            AfiResetConfigDefaultsResult* out = nullptr);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace afi
