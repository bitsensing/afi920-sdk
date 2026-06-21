// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "config_api/config_client.h"
#include "protocol/someip.h"
#include "util/logger.h"
#include <exception>
#include <nlohmann/json.hpp>

namespace afi {

using json = nlohmann::json;

// Safe json extraction: returns default if key is missing OR has wrong type.
// Prevents nlohmann::json::type_error crash when sensor returns unexpected types.
template<typename T>
static T safe_value(const json& j, const char* key, const T& default_val) {
    auto it = j.find(key);
    if (it == j.end()) return default_val;
    try { return it->get<T>(); }
    catch (const json::type_error&) { return default_val; }
}

static std::vector<uint8_t> default_cfar_table() {
    return std::vector<uint8_t>(AfiDetectionThreshold::kCfarTableLength,
                                AfiDetectionThreshold::kDefaultCfarLevel);
}

static std::vector<uint8_t> safe_u8_vector(const json& j, const char* key,
                                           const std::vector<uint8_t>& default_val) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return default_val;

    std::vector<uint8_t> out;
    out.reserve(it->size());
    for (const auto& item : *it) {
        if (!item.is_number_integer() && !item.is_number_unsigned()) return default_val;
        try {
            int value = item.get<int>();
            if (value < 0 || value > 255) return default_val;
            out.push_back(static_cast<uint8_t>(value));
        } catch (const std::exception&) {
            return default_val;
        }
    }
    return out;
}

static bool is_valid_cfar_table(const std::vector<uint8_t>& table) {
    if (table.size() != AfiDetectionThreshold::kCfarTableLength) return false;
    for (uint8_t level : table) {
        if (level < 1 || level > 9) return false;
    }
    return true;
}

ConfigClient::ConfigClient(TcpClient& tcp) : tcp_(tcp) {}

// ─── Core Request/Response ───

int ConfigClient::SendJsonRequest(uint16_t method_id,
                                   const json& request_json,
                                   json& response_data) {
    if (!tcp_.IsConnected()) return kDisconnected;

    // Serialize request payload
    std::string req_str = request_json.dump();
    std::vector<uint8_t> payload(req_str.begin(), req_str.end());

    // Send and receive
    uint8_t return_code = 0;
    std::vector<uint8_t> resp_payload;
    int rc = tcp_.SendRequest(method_id, payload, return_code, resp_payload);
    if (rc < 0) return kConnectFailed;

    // Check SOME/IP return code
    if (return_code != kSomeipReturnOk) {
        AFI_LOG_ERROR("Config API 0x%04X: SOME/IP return code %d", method_id, return_code);
        return kSensorError;
    }

    // Parse JSON response
    if (resp_payload.empty()) {
        response_data = json::object();
        return kOk;
    }

    try {
        auto resp = json::parse(resp_payload.begin(), resp_payload.end());

        // Check JSON-level return code
        if (resp.contains("return_code") && resp["return_code"].get<int>() != 0) {
            int code = resp["return_code"].get<int>();
            std::string name = resp.value("return_name", "UNKNOWN");
            AFI_LOG_ERROR("Config API 0x%04X: sensor error %d (%s)",
                         method_id, code, name.c_str());
            return kSensorError;
        }

        response_data = resp.value("data", json::object());
        return kOk;
    } catch (const std::exception& e) {
        AFI_LOG_ERROR("Config API 0x%04X: JSON parse error: %s", method_id, e.what());
        return kProtocolError;
    }
}

int ConfigClient::SendJsonRequest(uint16_t method_id, json& response_data) {
    return SendJsonRequest(method_id, json::object(), response_data);
}

// ─── Device Info & Status (0x001x) ───

int ConfigClient::GetDeviceInfo(AfiDeviceInfo& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetDeviceInfo), data);
    if (rc != kOk) return rc;
    out.serial_number    = safe_value(data, "serial_number", std::string());
    out.mac_address      = safe_value(data, "mac_address", std::string());
    out.model_name       = safe_value(data, "model_name", std::string());
    out.hw_version       = safe_value(data, "hw_version", std::string());
    out.software_version = safe_value(data, "software_version", std::string());
    out.bootloader_hash  = safe_value(data, "bootloader_hash", std::string());
    out.driver_hash      = safe_value(data, "driver_hash", std::string());
    out.fw_hash          = safe_value(data, "fw_hash", std::string());
    return kOk;
}

int ConfigClient::GetSensorStatus(AfiSensorStatus& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetSensorStatus), data);
    if (rc != kOk) return rc;
    out.operation_state      = safe_value(data, "operation_state", std::string());
    out.operation_state_code = safe_value(data, "operation_state_code", static_cast<uint8_t>(0));
    out.temperature_mcu      = safe_value(data, "temperature_mcu", 0.0f);
    out.temperature_rf       = safe_value(data, "temperature_rf", 0.0f);
    out.voltage              = safe_value(data, "voltage", 0.0f);
    out.uptime_sec           = safe_value(data, "uptime_sec", 0u);
    return kOk;
}

int ConfigClient::GetDiagnosticInfo(AfiDiagnosticInfo& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetDiagnosticInfo), data);
    if (rc != kOk) return rc;
    out.combined_state       = safe_value(data, "combined_state", std::string());
    out.combined_state_code  = safe_value(data, "combined_state_code", static_cast<uint8_t>(0));
    out.max_fault_level      = safe_value(data, "max_fault_level", std::string());
    out.max_fault_level_code = safe_value(data, "max_fault_level_code", static_cast<uint8_t>(0));
    out.trigger_fault_id     = safe_value(data, "trigger_fault_id", std::string());
    out.active_fault_count   = safe_value(data, "active_fault_count", static_cast<uint16_t>(0));
    out.m7_snapshot_stale    = safe_value(data, "m7_snapshot_stale", false);
    out.m7_stale_cycles      = safe_value(data, "m7_stale_cycles", 0u);
    out.faults.clear();
    if (data.contains("faults") && data["faults"].is_array()) {
        for (auto& f : data["faults"]) {
            AfiFaultEntry entry;
            entry.id         = safe_value(f, "id", std::string());
            entry.name       = safe_value(f, "name", std::string());
            entry.source     = safe_value(f, "source", std::string());
            entry.level      = safe_value(f, "level", std::string());
            entry.level_code = safe_value(f, "level_code", static_cast<uint8_t>(0));
            entry.active     = safe_value(f, "active", false);
            out.faults.push_back(std::move(entry));
        }
    }
    return kOk;
}

int ConfigClient::ClearFaultLog() {
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kClearFaultLog), data);
}

int ConfigClient::Restart() {
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kRestart), data);
}

int ConfigClient::ResetAllSettings() {
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kResetAllSettings), data);
}

int ConfigClient::GetCommandHistoryInfo(AfiCommandHistory& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetCommandHistoryInfo), data);
    if (rc != kOk) return rc;
    out.total_commands = safe_value(data, "total_commands", 0u);
    out.success_count  = safe_value(data, "success_count", 0u);
    out.fail_count     = safe_value(data, "fail_count", 0u);
    return kOk;
}

// ─── Network (0x002x) ───

static void parse_stream_destination(const json& data, const char* prefix,
                                     uint16_t default_port, AfiStreamDestination& out) {
    std::string p(prefix);
    out.ip       = safe_value(data, (p + "_ip").c_str(), std::string());
    out.port     = safe_value(data, (p + "_port").c_str(), default_port);
    out.protocol = safe_value(data, (p + "_protocol").c_str(), std::string("tcp"));
}

int ConfigClient::GetNetworkConfig(AfiNetworkConfig& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetNetworkConfig), data);
    if (rc != kOk) return rc;
    out.ip_address  = safe_value(data, "ip_address", std::string());
    out.subnet_mask = safe_value(data, "subnet_mask", std::string());
    out.gateway     = safe_value(data, "gateway", std::string());
    out.api_port    = safe_value(data, "api_port", static_cast<uint16_t>(30500));
    out.vlan_id     = safe_value(data, "vlan_id", static_cast<uint16_t>(0));
    parse_stream_destination(data, "detection", 30509, out.detection);
    parse_stream_destination(data, "health", 30510, out.health);
    parse_stream_destination(data, "performance", 30511, out.performance);
    return kOk;
}

int ConfigClient::SetNetworkConfig(const AfiNetworkConfig& config, bool* restart_required) {
    json req = {
        {"ip_address", config.ip_address},
        {"subnet_mask", config.subnet_mask},
        {"gateway", config.gateway},
        {"api_port", config.api_port},
        {"vlan_id", config.vlan_id},
        {"detection_ip", config.detection.ip},
        {"detection_port", config.detection.port},
        {"detection_protocol", config.detection.protocol},
        {"health_ip", config.health.ip},
        {"health_port", config.health.port},
        {"health_protocol", config.health.protocol},
        {"performance_ip", config.performance.ip},
        {"performance_port", config.performance.port},
        {"performance_protocol", config.performance.protocol},
    };
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kSetNetworkConfig), req, data);
    if (rc != kOk) return rc;
    if (restart_required) *restart_required = safe_value(data, "restart_required", false);
    return kOk;
}

int ConfigClient::SetStreamDestination(AfiStreamKind kind, const AfiStreamDestination& dst) {
    const char* prefix = nullptr;
    switch (kind) {
        case AfiStreamKind::kDetection:   prefix = "detection"; break;
        case AfiStreamKind::kHealth:      prefix = "health"; break;
        case AfiStreamKind::kPerformance: prefix = "performance"; break;
    }
    if (!prefix) return kInvalidParam;

    std::string p(prefix);
    json req;
    req[p + "_ip"] = dst.ip;
    req[p + "_port"] = dst.port;
    if (!dst.protocol.empty()) req[p + "_protocol"] = dst.protocol;

    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetNetworkConfig), req, data);
}

// ─── Range Mode (0x003x) ───

int ConfigClient::GetRangeMode(AfiRangeMode& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetRangeMode), data);
    if (rc != kOk) return rc;
    out = static_cast<AfiRangeMode>(safe_value(data, "range_mode", static_cast<uint8_t>(0)));
    return kOk;
}

int ConfigClient::SetRangeMode(AfiRangeMode mode) {
    json req = {{"range_mode", static_cast<uint8_t>(mode)}};
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetRangeMode), req, data);
}

// ─── Detection (0x004x) ───

int ConfigClient::GetDetectionThreshold(AfiDetectionThreshold& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetDetectionThreshold), data);
    if (rc != kOk) return rc;
    out.snr_dB = safe_value(data, "snr_threshold_db", 12.0f);
    out.cfar_table_first_frame =
        safe_u8_vector(data, "cfar_table_first_frame", default_cfar_table());
    out.cfar_table_second_frame =
        safe_u8_vector(data, "cfar_table_second_frame", default_cfar_table());
    return kOk;
}

int ConfigClient::SetDetectionThreshold(const AfiDetectionThreshold& threshold) {
    if (!is_valid_cfar_table(threshold.cfar_table_first_frame) ||
        !is_valid_cfar_table(threshold.cfar_table_second_frame)) {
        return kInvalidParam;
    }

    json req = {
        {"snr_threshold_db", threshold.snr_dB},
        {"cfar_table_first_frame", threshold.cfar_table_first_frame},
        {"cfar_table_second_frame", threshold.cfar_table_second_frame},
    };
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetDetectionThreshold), req, data);
}

int ConfigClient::GetDetectionFilters(AfiDetectionFilters& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetDetectionFilters), data);
    if (rc != kOk) return rc;
    out.filter_fov                 = safe_value(data, "filter_fov", true);
    out.filter_aps_noise           = safe_value(data, "filter_aps_noise", false);
    out.filter_ego_specular        = safe_value(data, "filter_ego_specular", false);
    out.filter_ground_sidelobe     = safe_value(data, "filter_ground_sidelobe", false);
    out.filter_upper_structure     = safe_value(data, "filter_upper_structure", false);
    out.filter_multibounce_2x      = safe_value(data, "filter_multibounce_2x", true);
    out.filter_structure_multipath = safe_value(data, "filter_structure_multipath", false);
    out.filter_specular_mirror     = safe_value(data, "filter_specular_mirror", true);
    out.filter_wheel_microdoppler  = safe_value(data, "filter_wheel_microdoppler", true);
    return kOk;
}

int ConfigClient::SetDetectionFilters(const AfiDetectionFilters& filters) {
    json req = {
        {"filter_fov", filters.filter_fov},
        {"filter_aps_noise", filters.filter_aps_noise},
        {"filter_ego_specular", filters.filter_ego_specular},
        {"filter_ground_sidelobe", filters.filter_ground_sidelobe},
        {"filter_upper_structure", filters.filter_upper_structure},
        {"filter_multibounce_2x", filters.filter_multibounce_2x},
        {"filter_structure_multipath", filters.filter_structure_multipath},
        {"filter_specular_mirror", filters.filter_specular_mirror},
        {"filter_wheel_microdoppler", filters.filter_wheel_microdoppler},
    };
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetDetectionFilters), req, data);
}

int ConfigClient::GetDetectionDensity(AfiDetectionDensity& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetDetectionDensity), data);
    if (rc != kOk) return rc;
    out.range_density = safe_value(data, "range_density", true);
    out.angle_density = safe_value(data, "angle_density", true);
    return kOk;
}

int ConfigClient::SetDetectionDensity(const AfiDetectionDensity& density) {
    json req = {
        {"range_density", density.range_density},
        {"angle_density", density.angle_density},
    };
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetDetectionDensity), req, data);
}

// ─── Time Sync (0x005x) ───

int ConfigClient::GetPtpConfig(AfiPtpConfig& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetPtpConfig), data);
    if (rc != kOk) return rc;
    out.profile            = safe_value(data, "profile", static_cast<uint8_t>(0));
    out.profile_name       = safe_value(data, "profile_name", std::string());
    out.domain             = safe_value(data, "domain", static_cast<uint8_t>(0));
    out.priority1          = safe_value(data, "priority1", static_cast<uint8_t>(128));
    out.priority2          = safe_value(data, "priority2", static_cast<uint8_t>(128));
    out.state              = safe_value(data, "state", std::string());
    out.state_code         = safe_value(data, "state_code", static_cast<uint8_t>(0));
    out.offset_ns          = safe_value(data, "offset_ns", static_cast<int64_t>(0));
    out.mean_path_delay_ns = safe_value(data, "mean_path_delay_ns", static_cast<int64_t>(0));
    out.master_clock_id    = safe_value(data, "master_clock_id", std::string());
    return kOk;
}

int ConfigClient::SetPtpConfig(uint8_t profile) {
    json req = {{"profile", profile}};
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetPtpConfig), req, data);
}

// ─── Mounting (0x007x) ───

int ConfigClient::GetMountingPosition(AfiMountingPosition& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetMountingPosition), data);
    if (rc != kOk) return rc;
    out.x = safe_value(data, "offset_x_m", 0.0f);
    out.y = safe_value(data, "offset_y_m", 0.0f);
    out.z = safe_value(data, "offset_z_m", 0.0f);
    return kOk;
}

int ConfigClient::SetMountingPosition(const AfiMountingPosition& pos) {
    json req = {{"offset_x_m", pos.x}, {"offset_y_m", pos.y}, {"offset_z_m", pos.z}};
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetMountingPosition), req, data);
}

int ConfigClient::GetRotation(AfiRotation& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetRotation), data);
    if (rc != kOk) return rc;
    out.roll  = safe_value(data, "roll_deg", 0.0f);
    out.pitch = safe_value(data, "pitch_deg", 0.0f);
    out.yaw   = safe_value(data, "yaw_deg", 0.0f);
    return kOk;
}

int ConfigClient::SetRotation(const AfiRotation& rot) {
    json req = {{"roll_deg", rot.roll}, {"pitch_deg", rot.pitch}, {"yaw_deg", rot.yaw}};
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetRotation), req, data);
}

int ConfigClient::GetSensorPosition(AfiSensorPosition& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetSensorPosition), data);
    if (rc != kOk) return rc;
    out.position_code = safe_value(data, "position_code", static_cast<uint8_t>(0));
    return kOk;
}

int ConfigClient::SetSensorPosition(const AfiSensorPosition& pos) {
    json req = {{"position_code", pos.position_code}};
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetSensorPosition), req, data);
}

// ─── Config Persistence (0x009x) ───

int ConfigClient::SaveConfig() {
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSaveConfig), data);
}

int ConfigClient::GetConfigStatus(AfiConfigStatus& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetConfigStatus), data);
    if (rc != kOk) return rc;
    out.load_status      = safe_value(data, "load_status", static_cast<uint8_t>(0));
    out.load_status_name = safe_value(data, "load_status_name", std::string());
    return kOk;
}

int ConfigClient::ResetConfigDefaults(const std::vector<std::string>& keys,
                                      AfiResetConfigDefaultsResult* out) {
    if (keys.empty()) return kInvalidParam;
    for (const auto& key : keys) {
        if (key.empty()) return kInvalidParam;
    }

    json req = {{"keys", keys}};
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kResetConfigDefaults),
                             req, data);
    if (rc != kOk) return rc;
    if (out) out->reset_fields = safe_value(data, "reset_fields", 0u);
    return kOk;
}

} // namespace afi
