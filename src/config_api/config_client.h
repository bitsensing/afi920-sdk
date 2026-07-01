/**
 * @file config_client.h
 * @brief Config API client (SOME/IP + JSON over TCP, protocol v3.0)
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include "afi/afi_error.h"
#include "transport/tcp_client.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace afi {

class ConfigClient {
public:
    explicit ConfigClient(TcpClient& tcp);

    // ─── Device Info & Status (0x001x) ───
    int GetDeviceInfo(AfiDeviceInfo& out);
    int GetSensorStatus(AfiSensorStatus& out);
    int GetDiagnosticInfo(AfiDiagnosticInfo& out);
    int ClearFaultLog();
    int Restart();
    int ResetAllSettings();
    int GetCommandHistoryInfo(AfiCommandHistory& out);

    // ─── Network (0x002x) ───
    int GetNetworkConfig(AfiNetworkConfig& out);
    /// Full update — includes the radar's own network fields, so the
    /// sensor reports restart_required=true. Restart via Restart().
    int SetNetworkConfig(const AfiNetworkConfig& config, bool* restart_required = nullptr);
    /// Partial update of a single stream destination (applies immediately)
    int SetStreamDestination(AfiStreamKind kind, const AfiStreamDestination& dst);

    // ─── Range Mode (0x003x) ───
    int GetRangeMode(AfiRangeMode& out);
    int SetRangeMode(AfiRangeMode mode);

    // ─── Detection (0x004x) ───
    int GetDetectionThreshold(AfiDetectionThreshold& out);
    int SetDetectionThreshold(const AfiDetectionThreshold& threshold);
    int GetDetectionFilters(AfiDetectionFilters& out);
    int SetDetectionFilters(const AfiDetectionFilters& filters);
    int GetDetectionDensity(AfiDetectionDensity& out);
    int SetDetectionDensity(const AfiDetectionDensity& density);

    // ─── Time Sync (0x005x) ───
    int GetPtpConfig(AfiPtpConfig& out);
    int SetPtpConfig(uint8_t profile);

    // ─── Mounting (0x007x) ───
    int GetMountingPosition(AfiMountingPosition& out);
    int SetMountingPosition(const AfiMountingPosition& pos);
    int GetRotation(AfiRotation& out);
    int SetRotation(const AfiRotation& rot);
    int GetSensorPosition(AfiSensorPosition& out);
    int SetSensorPosition(const AfiSensorPosition& pos);

    // ─── Config Persistence (0x009x) ───
    int SaveConfig();
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

    TcpClient& tcp_;
};

} // namespace afi
