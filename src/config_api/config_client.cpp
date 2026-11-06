// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "config_api/config_client.h"
#include "protocol/someip.h"
#include "util/logger.h"
#include "bts_iso23150_e2e.h"
#include <cmath>
#include <cstdio>
#include <cstring>
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
    catch (const std::exception&) { return default_val; }
}

static bool parse_cfar_table(const json& j, const char* key,
                             std::vector<uint8_t>& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() ||
        it->size() != AfiDetectionThreshold::kCfarTableLength) {
        return false;
    }

    out.clear();
    out.reserve(it->size());
    for (const auto& item : *it) {
        if (!item.is_number_integer() && !item.is_number_unsigned()) return false;
        try {
            int value = item.get<int>();
            if (value < 1 || value > 9) return false;
            out.push_back(static_cast<uint8_t>(value));
        } catch (const std::exception&) {
            return false;
        }
    }
    return true;
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
    const std::string req_str = request_json.dump();
    return Exchange(method_id, std::vector<uint8_t>(req_str.begin(), req_str.end()),
                    response_data);
}

AfiSensorResult ConfigClient::LastResult() const {
    std::lock_guard<std::mutex> lock(result_mutex_);
    return last_result_;
}

static int ErrorFromTransport(int tcp_result) {
    switch (tcp_result) {
        case kTcpTimeout:  return kTimeout;
        case kTcpProtocol: return kProtocolError;
        case kTcpTooLarge: return kInvalidParam;
        default:           return kDisconnected;
    }
}

int ConfigClient::Exchange(uint16_t method_id, const std::vector<uint8_t>& body,
                           json& response_data) {
    AfiSensorResult result;
    response_data = json::object();
    const auto finish = [&](int error) {
        std::lock_guard<std::mutex> lock(result_mutex_);
        last_result_ = result;
        return error;
    };

    if (!tcp_.HasEndpoint()) return finish(kDisconnected);

    uint8_t header_rc = 0;
    std::vector<uint8_t> resp_payload;
    const int tr = tcp_.SendRequest(method_id, body, header_rc, resp_payload);
    if (tr == kTcpLegacySensor) {
        result.return_code = kRcNotSupported;
        result.return_name = AfiReturnCodeName(kRcNotSupported);
        result.error_message = "interface v1.0.x sensor: Config API request not sent";
        return finish(kNotSupported);
    }
    if (tr != kTcpOk) return finish(ErrorFromTransport(tr));
    result.e2e_ok = tcp_.LastResponseE2eOk();

    json resp = json::object();
    if (!resp_payload.empty()) {
        try {
            resp = json::parse(resp_payload.begin(), resp_payload.end());
        } catch (const std::exception& e) {
            AFI_LOG_ERROR("Config API 0x%04X: JSON parse error: %s", method_id, e.what());
            return finish(kProtocolError);
        }
    }

    // The radar puts the same code in the header and in the body; the body
    // wins when both are present.
    int rc = header_rc;
    if (resp.is_object()) {
        auto it = resp.find("return_code");
        if (it != resp.end() && it->is_number_integer()) rc = it->get<int>();
    }
    json data = resp.is_object() ? resp.value("data", json::object()) : json::object();
    result.return_code = rc;
    result.return_name = safe_value(resp, "return_name",
                                    std::string(AfiReturnCodeName(rc)));
    if (data.is_object()) {
        result.error_message = safe_value(data, "error_message", std::string());
    }
    response_data = data;

    if (rc != kRcSuccess) {
        AFI_LOG_ERROR("Config API 0x%04X: sensor returned %d (%s)%s%s", method_id, rc,
                      result.return_name.c_str(),
                      result.error_message.empty() ? "" : ": ",
                      result.error_message.c_str());
        return finish(AfiErrorFromReturnCode(rc));
    }
    return finish(kOk);
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
    parse_stream_destination(data, "objects", 30512, out.objects);
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
    // Interface v1.1.0 defines only the detection, health, and performance
    // stream keys (02_Network.md 02.2). `objects` is kept for source
    // compatibility and is never sent.
    if (!config.objects.ip.empty() || config.objects.port != 0 ||
        !config.objects.protocol.empty()) {
        AFI_LOG_WARN("SetNetworkConfig: objects_* is not defined on interface "
                     "v1.1.0 and is not sent");
    }
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
        case AfiStreamKind::kObjects:
            AFI_LOG_WARN("SetStreamDestination: objects_* is not defined on "
                         "interface v1.1.0");
            return kInvalidParam;
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

int ConfigClient::SetRangeMode(AfiRangeMode mode, bool* restart_required) {
    if (static_cast<uint8_t>(mode) > static_cast<uint8_t>(AfiRangeMode::kUltraDualRange)) {
        return kInvalidParam;
    }
    json req = {{"range_mode", static_cast<uint8_t>(mode)}};
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kSetRangeMode), req, data);
    if (rc == kOk && restart_required) {
        *restart_required = safe_value(data, "restart_required", true);
    }
    return rc;
}

// ─── Detection (0x004x) ───

int ConfigClient::GetDetectionThreshold(AfiDetectionThreshold& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetDetectionThreshold), data);
    if (rc != kOk) return rc;
    out.snr_dB = safe_value(data, "snr_threshold_db", 0.0f);
    if (!std::isfinite(out.snr_dB) || out.snr_dB < 1.0f || out.snr_dB > 9.0f ||
        !parse_cfar_table(data, "cfar_table_first_frame",
                          out.cfar_table_first_frame)) {
        return kProtocolError;
    }
    out.has_cfar_table_second_frame = data.contains("cfar_table_second_frame");
    if (out.has_cfar_table_second_frame) {
        if (!parse_cfar_table(data, "cfar_table_second_frame",
                              out.cfar_table_second_frame)) {
            return kProtocolError;
        }
    } else {
        out.cfar_table_second_frame.clear();
    }
    return kOk;
}

int ConfigClient::SetDetectionThreshold(const AfiDetectionThreshold& threshold) {
    if (!std::isfinite(threshold.snr_dB) || threshold.snr_dB < 1.0f ||
        threshold.snr_dB > 9.0f ||
        !is_valid_cfar_table(threshold.cfar_table_first_frame) ||
        (threshold.has_cfar_table_second_frame &&
         !is_valid_cfar_table(threshold.cfar_table_second_frame))) {
        return kInvalidParam;
    }

    json req = {
        {"snr_threshold_db", threshold.snr_dB},
        {"cfar_table_first_frame", threshold.cfar_table_first_frame},
    };
    if (threshold.has_cfar_table_second_frame) {
        req["cfar_table_second_frame"] = threshold.cfar_table_second_frame;
    }
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetDetectionThreshold), req, data);
}

int ConfigClient::GetDetectionFilters(AfiDetectionFilters& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetDetectionFilters), data);
    if (rc != kOk) return rc;
    const AfiDetectionFilters d;
    out.filter_ego_specular        = safe_value(data, "filter_ego_specular", d.filter_ego_specular);
    out.filter_ground_sidelobe     = safe_value(data, "filter_ground_sidelobe", d.filter_ground_sidelobe);
    out.filter_upper_structure     = safe_value(data, "filter_upper_structure", d.filter_upper_structure);
    out.filter_multibounce_2x      = safe_value(data, "filter_multibounce_2x", d.filter_multibounce_2x);
    out.filter_structure_multipath = safe_value(data, "filter_structure_multipath", d.filter_structure_multipath);
    out.filter_specular_mirror     = safe_value(data, "filter_specular_mirror", d.filter_specular_mirror);
    out.filter_wheel_microdoppler  = safe_value(data, "filter_wheel_microdoppler", d.filter_wheel_microdoppler);
    out.filter_ground_reflection   = safe_value(data, "filter_ground_reflection", d.filter_ground_reflection);
    return kOk;
}

int ConfigClient::SetDetectionFilters(const AfiDetectionFilters& filters) {
    json req = {
        {"filter_ego_specular", filters.filter_ego_specular},
        {"filter_ground_sidelobe", filters.filter_ground_sidelobe},
        {"filter_upper_structure", filters.filter_upper_structure},
        {"filter_multibounce_2x", filters.filter_multibounce_2x},
        {"filter_structure_multipath", filters.filter_structure_multipath},
        {"filter_specular_mirror", filters.filter_specular_mirror},
        {"filter_wheel_microdoppler", filters.filter_wheel_microdoppler},
        {"filter_ground_reflection", filters.filter_ground_reflection},
    };
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kSetDetectionFilters), req, data);
}

// ─── Time Sync (0x005x) ───

int ConfigClient::GetPtpConfig(AfiPtpConfig& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetPtpConfig), data);
    if (rc != kOk) return rc;
    out.profile            = safe_value(data, "profile", static_cast<uint8_t>(0));
    out.profile_name       = safe_value(data, "profile_name", std::string());
    out.configured_profile = safe_value(data, "configured_profile", out.profile);
    out.configured_profile_name =
        safe_value(data, "configured_profile_name", out.profile_name);
    out.restart_required   = safe_value(data, "restart_required", false);
    out.domain             = safe_value(data, "domain", static_cast<uint8_t>(0));
    out.priority1          = safe_value(data, "priority1", static_cast<uint8_t>(255));
    out.priority2          = safe_value(data, "priority2", static_cast<uint8_t>(255));
    out.state              = safe_value(data, "state", std::string());
    out.state_code         = safe_value(data, "state_code", static_cast<uint8_t>(0));
    out.offset_ns          = safe_value(data, "offset_ns", static_cast<int64_t>(0));
    out.mean_path_delay_ns = safe_value(data, "mean_path_delay_ns", static_cast<int64_t>(0));
    out.master_clock_id    = safe_value(data, "master_clock_id", std::string());
    out.grid_offset_ns     = safe_value(data, "grid_offset_ns", static_cast<int64_t>(0));
    out.grid_state         = safe_value(data, "grid_state", std::string());
    return kOk;
}

int ConfigClient::SetPtpConfig(uint8_t profile, bool* restart_required) {
    if (profile > 2) return kInvalidParam;
    json req = {{"profile", profile}};
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kSetPtpConfig), req, data);
    if (rc != kOk) return rc;
    if (restart_required) {
        *restart_required = safe_value(data, "restart_required", false);
    }
    return kOk;
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

// ─── Firmware Update (0x008x) ───

// The radar parses a crc32 string without "0x" as decimal (07.1), so bare
// hex would be misread; give 8 bare hex digits their prefix.
static std::string normalize_crc32(const std::string& crc) {
    const bool bare_hex = crc.size() == 8 &&
        crc.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
    return bare_hex ? "0x" + crc : crc;
}

int ConfigClient::FirmwareUpdateStart(const AfiOtaStartRequest& req_in, AfiOtaStartResponse& out) {
    json req = {
        {"update_type", req_in.update_type},
        {"component", req_in.component},
        {"total_size", req_in.total_size},
    };
    if (req_in.crc32.empty()) {
        AFI_LOG_WARN("FirmwareUpdateStart %s: no crc32, the radar skips the image check",
                     req_in.component.c_str());
    } else {
        req["crc32"] = normalize_crc32(req_in.crc32);
    }
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kFirmwareUpdateStart), req, data);
    if (rc != kOk) return rc;
    out.max_chunk_size = safe_value(data, "max_chunk_size", 4096u);
    out.target_slot    = safe_value(data, "target_slot", std::string());
    return kOk;
}

int ConfigClient::FirmwareDataChunk(const uint8_t* chunk_data, size_t len,
                                    AfiOtaChunkResult* out) {
    json data;
    int rc = SendRawRequest(static_cast<uint16_t>(MethodId::kFirmwareDataChunk),
                            chunk_data, len, data);
    if (rc == kOk && out) {
        out->received_bytes = safe_value(data, "received_bytes", 0u);
        out->total_bytes = safe_value(data, "total_bytes", 0u);
    }
    return rc;
}

int ConfigClient::FirmwareUpdateCommit() {
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kFirmwareUpdateCommit), data);
}

int ConfigClient::FirmwareUpdateStatus(AfiOtaStatus& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kFirmwareUpdateStatus), data);
    if (rc != kOk) return rc;
    out.state             = safe_value(data, "state", std::string());
    out.progress_pct      = safe_value(data, "progress_pct", static_cast<uint8_t>(0));
    out.current_component = safe_value(data, "current_component", std::string());
    out.error_message     = safe_value(data, "error_message", std::string());
    return kOk;
}

int ConfigClient::FirmwareUpdateAbort() {
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kFirmwareUpdateAbort), data);
}

int ConfigClient::FirmwareSwitchSlot(const std::string& update_type, AfiOtaSwitchSlotResult& out) {
    json req = {{"update_type", update_type}};
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kFirmwareSwitchSlot), req, data);
    if (rc != kOk) return rc;
    out.previous_active = safe_value(data, "previous_active", std::string());
    out.new_active      = safe_value(data, "new_active", std::string());
    return kOk;
}

int ConfigClient::FirmwareUpdateReboot() {
    json data;
    return SendJsonRequest(static_cast<uint16_t>(MethodId::kFirmwareUpdateReboot), data);
}

int ConfigClient::GetBootInfo(AfiBootInfo& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetBootInfo), data);
    if (rc != kOk) return rc;
    out.fw_update_active      = safe_value(data, "fw_update_active", std::string());
    out.fw_update_valid_a     = safe_value(data, "fw_update_valid_a", false);
    out.fw_update_valid_b     = safe_value(data, "fw_update_valid_b", false);
    out.fw_update_boot_count  = safe_value(data, "fw_update_boot_count", 0u);
    out.boot_info_status      = safe_value(data, "boot_info_status", std::string());
    return kOk;
}

// ─── Config Persistence (0x009x) ───

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

// ─── Access Mode (0x00Ex) ───

static bool parse_mode_status(const json& data, AfiModeStatus& out) {
    const int mode = safe_value(data, "access_mode", -1);
    if (mode < 0 || mode > 2) return false;
    out.access_mode = static_cast<AfiAccessMode>(mode);
    out.access_mode_name = safe_value(data, "access_mode_name", std::string());
    out.elapsed_ms = safe_value(data, "elapsed_ms", 0u);
    out.streams_halted = safe_value(data, "streams_halted", 0) != 0;
    return true;
}

int ConfigClient::GetMode(AfiModeStatus& out) {
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kGetMode), data);
    if (rc != kOk) return rc;
    return parse_mode_status(data, out) ? kOk : kProtocolError;
}

int ConfigClient::SetMode(AfiAccessMode mode, AfiModeStatus* out) {
    // Normal is reachable only by reboot; the radar answers INVALID_PARAM.
    if (mode != AfiAccessMode::kDiagnostic && mode != AfiAccessMode::kProgramming) {
        return kInvalidParam;
    }
    json req = {{"access_mode", static_cast<int>(mode)}};
    json data;
    int rc = SendJsonRequest(static_cast<uint16_t>(MethodId::kSetMode), req, data);
    if (rc != kOk) return rc;
    if (out && !parse_mode_status(data, *out)) return kProtocolError;
    return kOk;
}

int ConfigClient::EnterMode(AfiAccessMode target) {
    if (target == AfiAccessMode::kNormal) return kInvalidParam;
    AfiModeStatus cur;
    int rc = GetMode(cur);
    if (rc != kOk) return rc;
    if (cur.access_mode == target) return kOk;
    // Programming has no way back to Diagnostic except a reboot.
    if (cur.access_mode == AfiAccessMode::kProgramming) return kAccessDenied;
    if (cur.access_mode == AfiAccessMode::kNormal) {
        rc = SetMode(AfiAccessMode::kDiagnostic);
        if (rc != kOk || target == AfiAccessMode::kDiagnostic) return rc;
    }
    return SetMode(AfiAccessMode::kProgramming);
}

// ─── Raw Binary Request ───

int ConfigClient::SendRawRequest(uint16_t method_id,
                                  const uint8_t* payload, size_t len,
                                  json& response_data) {
    if (!payload && len > 0) return kInvalidParam;
    std::vector<uint8_t> body;
    if (len > 0) body.assign(payload, payload + len);
    return Exchange(method_id, body, response_data);
}

} // namespace afi
