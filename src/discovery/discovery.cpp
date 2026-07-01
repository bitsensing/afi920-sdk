// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "discovery/discovery.h"
#include "protocol/someip.h"
#include "transport/platform.h"
#include "util/endian_util.h"
#include "util/logger.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <set>

namespace afi {

std::vector<AfiSensorInfo> Discovery::Scan(int timeout_ms, uint16_t port,
                                            const std::string& bind_ip) {
    std::vector<AfiSensorInfo> results;

    PlatformSocketInit init;

    // Create UDP socket
    socket_t sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == kInvalidSocket) {
        AFI_LOG_ERROR("Discovery: failed to create socket");
        return results;
    }

    // Enable broadcast
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    // Set receive timeout
    platform_set_recv_timeout(sock, timeout_ms);

    // Allow port reuse
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // Bind to specific or any interface for receiving responses
    struct sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    if (!bind_ip.empty()) {
        if (::inet_pton(AF_INET, bind_ip.c_str(), &bind_addr.sin_addr) <= 0) {
            AFI_LOG_ERROR("Discovery: invalid bind_ip '%s', using INADDR_ANY", bind_ip.c_str());
            bind_addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            AFI_LOG_INFO("Discovery: binding to %s", bind_ip.c_str());
        }
    } else {
        bind_addr.sin_addr.s_addr = INADDR_ANY;
    }
    bind_addr.sin_port = htons(0);  // ephemeral port
    ::bind(sock, reinterpret_cast<struct sockaddr*>(&bind_addr), sizeof(bind_addr));

    // Build SOME/IP Discovery request
    // MessageID = 0x600000C0, Length = 8 (empty payload), REQUEST type
    uint8_t request[kSomeipHeaderSize + 2]; // header + "{}"
    uint32_t message_id = (static_cast<uint32_t>(kSomeipServiceId) << 16) |
                           static_cast<uint16_t>(MethodId::kDiscoverRequest);
    uint32_t length = 8 + 2; // 8 (header fields after length) + 2 ("{}")

    detail::pack_be32(request + 0, message_id);
    detail::pack_be32(request + 4, length);
    detail::pack_be32(request + 8, 0x00010001); // client_id=1, session_id=1
    request[12] = 0x01; // protocol version
    request[13] = 0x01; // interface version
    request[14] = kSomeipMsgTypeRequest;
    request[15] = kSomeipReturnOk;
    request[16] = '{';
    request[17] = '}';

    // Send broadcast
    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = INADDR_BROADCAST;

    int sent = ::sendto(sock, reinterpret_cast<const char*>(request), sizeof(request), 0,
                        reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
    if (sent < 0) {
        AFI_LOG_ERROR("Discovery: failed to send broadcast: %d", platform_socket_error());
        platform_close_socket(sock);
        return results;
    }

    AFI_LOG_INFO("Discovery: broadcast sent to port %d", port);

    // Collect responses
    std::set<std::string> seen; // dedup by ip+serial
    uint8_t buf[2048];

    while (true) {
        struct sockaddr_in sender{};
        socklen_t sender_len = sizeof(sender);

        int n = ::recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                           reinterpret_cast<struct sockaddr*>(&sender), &sender_len);
        if (n <= 0) break; // timeout or error

        // Need at least SOME/IP header
        if (static_cast<size_t>(n) < kSomeipHeaderSize) continue;

        // Parse SOME/IP header
        SomeipHeader hdr;
        if (someip_deserialize(buf, n, hdr) < 0) continue;

        // Check for Discovery response (0x00C1)
        if (hdr.method_id != static_cast<uint16_t>(MethodId::kDiscoverResponse)) continue;

        // Extract JSON payload
        size_t payload_offset = kSomeipHeaderSize;
        size_t payload_len = static_cast<size_t>(n) - payload_offset;
        if (payload_len == 0) continue;

        AfiSensorInfo info;
        if (ParseResponse(buf + payload_offset, payload_len, info) == 0) {
            std::string key = info.ip + ":" + info.serial_number;
            if (seen.find(key) == seen.end()) {
                seen.insert(key);
                AFI_LOG_INFO("Discovery: found %s (%s) at %s",
                            info.model.c_str(), info.serial_number.c_str(),
                            info.ip.c_str());
                results.push_back(std::move(info));
            }
        }
    }

    platform_close_socket(sock);
    AFI_LOG_INFO("Discovery: found %zu sensor(s)", results.size());
    return results;
}

int Discovery::ParseResponse(const uint8_t* data, size_t len, AfiSensorInfo& out) {
    try {
        auto j = nlohmann::json::parse(data, data + len);

        // Check return code
        if (j.contains("return_code") && j["return_code"].get<int>() != 0) {
            return -1;
        }

        auto& d = j.contains("data") ? j["data"] : j;

        // Device identity
        if (d.contains("serial_number"))  out.serial_number = d["serial_number"].get<std::string>();
        if (d.contains("mac_address"))    out.mac_address = d["mac_address"].get<std::string>();
        if (d.contains("model_name"))     out.model = d["model_name"].get<std::string>();

        // Version
        if (d.contains("hw_version"))       out.hw_version = d["hw_version"].get<std::string>();
        if (d.contains("software_version")) out.software_version = d["software_version"].get<std::string>();
        if (d.contains("bootloader_hash"))  out.bootloader_hash = d["bootloader_hash"].get<std::string>();
        if (d.contains("driver_hash"))      out.driver_hash = d["driver_hash"].get<std::string>();
        if (d.contains("fw_hash"))          out.fw_hash = d["fw_hash"].get<std::string>();

        // Network
        if (d.contains("ip_address"))     out.ip = d["ip_address"].get<std::string>();
        if (d.contains("subnet_mask"))    out.subnet_mask = d["subnet_mask"].get<std::string>();
        if (d.contains("gateway"))        out.gateway = d["gateway"].get<std::string>();

        // Ports
        if (d.contains("api_port"))           out.port = d["api_port"].get<uint16_t>();
        if (d.contains("detection_port"))     out.detection_port = d["detection_port"].get<uint16_t>();
        if (d.contains("health_port"))        out.health_port = d["health_port"].get<uint16_t>();
        if (d.contains("performance_port"))   out.performance_port = d["performance_port"].get<uint16_t>();

        // Data stream protocols
        if (d.contains("detection_protocol"))    out.detection_protocol = d["detection_protocol"].get<std::string>();
        if (d.contains("health_protocol"))       out.health_protocol = d["health_protocol"].get<std::string>();
        if (d.contains("performance_protocol"))  out.performance_protocol = d["performance_protocol"].get<std::string>();

        // Host destination IPs
        if (d.contains("host_ip"))           out.host_ip = d["host_ip"].get<std::string>();
        if (d.contains("detection_ip"))      out.detection_ip = d["detection_ip"].get<std::string>();
        if (d.contains("health_ip"))         out.health_ip = d["health_ip"].get<std::string>();
        if (d.contains("performance_ip"))    out.performance_ip = d["performance_ip"].get<std::string>();

        // Mounting / position
        if (d.contains("position_code"))  out.position_code = d["position_code"].get<uint8_t>();
        if (d.contains("offset_x_m"))    out.offset_x = d["offset_x_m"].get<float>();
        if (d.contains("offset_y_m"))    out.offset_y = d["offset_y_m"].get<float>();
        if (d.contains("offset_z_m"))    out.offset_z = d["offset_z_m"].get<float>();
        if (d.contains("yaw_deg"))       out.yaw_deg = d["yaw_deg"].get<float>();
        if (d.contains("pitch_deg"))     out.pitch_deg = d["pitch_deg"].get<float>();
        if (d.contains("roll_deg"))      out.roll_deg = d["roll_deg"].get<float>();

        // Range mode
        if (d.contains("range_mode")) {
            out.range_mode = static_cast<AfiRangeMode>(d["range_mode"].get<uint8_t>());
        }
        if (d.contains("range_mode_name"))  out.range_mode_name = d["range_mode_name"].get<std::string>();

        // Operation status
        if (d.contains("radar_active"))  out.radar_active = d["radar_active"].get<bool>();
        if (d.contains("radar_mode"))    out.radar_mode = d["radar_mode"].get<std::string>();

        // Scan status (object)
        if (d.contains("scan_status") && d["scan_status"].is_object()) {
            auto& ss = d["scan_status"];
            out.scan_index     = ss.value("scan_index", 0u);
            out.uptime_seconds = ss.value("uptime_seconds", 0u);
        }

        // PTP status
        if (d.contains("ptp_state"))      out.ptp_state = d["ptp_state"].get<std::string>();
        if (d.contains("ptp_offset_ns"))  out.ptp_offset_ns = d["ptp_offset_ns"].get<int64_t>();

        return 0;
    } catch (const std::exception& e) {
        AFI_LOG_ERROR("Discovery: JSON parse error: %s", e.what());
        return -1;
    }
}

} // namespace afi
