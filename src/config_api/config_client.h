/**
 * @file config_client.h
 * @brief Config API client (SOME/IP + E2E + JSON over TCP, interface v1.1.0)
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include "afi/afi_error.h"
#include "transport/tcp_client.h"
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace afi {

class ConfigClient {
public:
    explicit ConfigClient(TcpClient& tcp);

    /// Radar outcome of the last call made through this client.
    AfiSensorResult LastResult() const;

    // ─── Access Mode (0x00Ex) ───
    int GetMode(AfiModeStatus& out);
    int SetMode(AfiAccessMode mode, AfiModeStatus* out = nullptr);
    /// Walk Normal -> Diagnostic -> Programming up to `target` (see AfiSdk::EnterMode).
    int EnterMode(AfiAccessMode target);

    // ─── Device Info & Status (0x001x) ───
    int GetDeviceInfo(AfiDeviceInfo& out);
    int GetSensorStatus(AfiSensorStatus& out);
    int GetDiagnosticInfo(AfiDiagnosticInfo& out);
    int Restart();
    int ResetAllSettings();
    int GetCommandHistoryInfo(AfiCommandHistory& out);

    // ─── Network (0x002x) ───
    int GetNetworkConfig(AfiNetworkConfig& out);
    /// Full update — includes the radar's own network fields, so the
    /// sensor reports restart_required=true. Restart via Restart().
    int SetNetworkConfig(const AfiNetworkConfig& config, bool* restart_required = nullptr);
    /// Partial update of a single stream destination (NVM; applies after Restart)
    int SetStreamDestination(AfiStreamKind kind, const AfiStreamDestination& dst);

    // ─── Range Mode (0x003x) ───
    int GetRangeMode(AfiRangeMode& out);
    /// Stored in NVM; takes effect after Restart (restart_required is always true).
    int SetRangeMode(AfiRangeMode mode, bool* restart_required = nullptr);

    // ─── Detection (0x004x) ───
    int GetDetectionThreshold(AfiDetectionThreshold& out);
    int SetDetectionThreshold(const AfiDetectionThreshold& threshold);
    int GetDetectionFilters(AfiDetectionFilters& out);
    int SetDetectionFilters(const AfiDetectionFilters& filters);

    // ─── Time Sync (0x005x) ───
    int GetPtpConfig(AfiPtpConfig& out);
    int SetPtpConfig(uint8_t profile, bool* restart_required = nullptr);

    // ─── Mounting (0x007x) ───
    int GetMountingPosition(AfiMountingPosition& out);
    int SetMountingPosition(const AfiMountingPosition& pos);
    int GetRotation(AfiRotation& out);
    int SetRotation(const AfiRotation& rot);
    int GetSensorPosition(AfiSensorPosition& out);
    int SetSensorPosition(const AfiSensorPosition& pos);

    // ─── Firmware Update (0x008x) ───
    int FirmwareUpdateStart(const AfiOtaStartRequest& req, AfiOtaStartResponse& out);
    int FirmwareDataChunk(const uint8_t* data, size_t len,
                          AfiOtaChunkResult* out = nullptr);
    int FirmwareUpdateCommit();
    int FirmwareUpdateStatus(AfiOtaStatus& out);
    int FirmwareUpdateAbort();
    int FirmwareSwitchSlot(const std::string& update_type, AfiOtaSwitchSlotResult& out);
    int FirmwareUpdateReboot();
    int GetBootInfo(AfiBootInfo& out);

    // ─── Config Persistence (0x009x) ───
    int GetConfigStatus(AfiConfigStatus& out);
    int ResetConfigDefaults(const std::vector<std::string>& keys,
                            AfiResetConfigDefaultsResult* out = nullptr);


private:
    /**
     * @brief Send JSON request and receive JSON response
     * @param method_id SOME/IP method ID
     * @param request_json Request payload (default: empty object)
     * @param[out] response_data Parsed "data" field from response
     * @return kOk on success, error code on failure
     */
    int SendJsonRequest(uint16_t method_id,
                        const nlohmann::json& request_json,
                        nlohmann::json& response_data);

    /// Convenience for requests with empty payload
    int SendJsonRequest(uint16_t method_id, nlohmann::json& response_data);

    /// Send a raw (non-JSON) binary payload request; the reply is JSON.
    int SendRawRequest(uint16_t method_id,
                       const uint8_t* payload, size_t len,
                       nlohmann::json& response_data);

    /// One request/response; maps transport and radar failures to AfiError.
    int Exchange(uint16_t method_id, const std::vector<uint8_t>& body,
                 nlohmann::json& response_data);

    TcpClient& tcp_;
    mutable std::mutex result_mutex_;
    AfiSensorResult last_result_;
};

} // namespace afi
