// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "transport/tcp_client.h"
#include "util/endian_util.h"
#include "util/logger.h"
#include <cstring>
#ifdef _WIN32
#include <mstcpip.h>
#else
#include <netinet/tcp.h>
#endif

namespace afi {

// SOME/IP constants
static constexpr uint16_t kServiceId          = 0x6000;
static constexpr uint8_t  kProtocolVersion    = 0x01;
static constexpr uint8_t  kInterfaceVersion   = 0x01;
static constexpr uint8_t  kMsgTypeRequest     = 0x00;
static constexpr uint8_t  kMsgTypeResponse    = 0x80;
static constexpr uint16_t kClientId           = 0x0001;
static constexpr size_t   kSomeipHeaderSize   = 16;
static constexpr uint32_t kMaxResponsePayload = 4 * 1024 * 1024;  // 4 MB

TcpClient::~TcpClient() {
    Disconnect();
}

int TcpClient::Connect(const std::string& ip, uint16_t port, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (socket_ != kInvalidSocket) {
        AFI_LOG_WARN("TcpClient already connected, disconnecting first");
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
    }

    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == kInvalidSocket) {
        AFI_LOG_ERROR("Failed to create TCP socket: %d", platform_socket_error());
        return -1;
    }

    // TCP_NODELAY for low latency
    int nodelay = 1;
    setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

    // SO_KEEPALIVE (IDLE=5s, INTVL=1s, CNT=3)
    int keepalive = 1;
    setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE,
               reinterpret_cast<const char*>(&keepalive), sizeof(keepalive));
#ifdef _WIN32
    struct tcp_keepalive ka = {1, 5000, 1000};
    DWORD bytes_returned = 0;
    WSAIoctl(socket_, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
             nullptr, 0, &bytes_returned, nullptr, nullptr);
#elif defined(__linux__)
    int idle = 5, intvl = 1, cnt = 3;
    setsockopt(socket_, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(socket_, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(socket_, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif

    // Set timeouts
    platform_set_recv_timeout(socket_, timeout_ms);
    platform_set_send_timeout(socket_, timeout_ms);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        AFI_LOG_ERROR("Invalid IP address: %s", ip.c_str());
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
        return -1;
    }

    if (::connect(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        AFI_LOG_ERROR("TCP connect to %s:%d failed: %d", ip.c_str(), port, platform_socket_error());
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
        return -1;
    }

    session_id_ = 0;
    AFI_LOG_INFO("TCP connected to %s:%d", ip.c_str(), port);
    return 0;
}

void TcpClient::Disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
        AFI_LOG_INFO("TCP disconnected");
    }
}

int TcpClient::SendRequest(uint16_t method_id,
                            const std::vector<uint8_t>& payload,
                            uint8_t& return_code,
                            std::vector<uint8_t>& response_payload,
                            int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (socket_ == kInvalidSocket) {
        AFI_LOG_ERROR("Not connected");
        return -1;
    }

    // Increment session ID (1 → 0xFFFF, wrap)
    session_id_++;
    if (session_id_ == 0) session_id_ = 1;

    // Build SOME/IP frame (header + payload) into single buffer
    uint32_t message_id = (static_cast<uint32_t>(kServiceId) << 16) | method_id;
    uint32_t length = 8 + static_cast<uint32_t>(payload.size());
    uint32_t request_id = (static_cast<uint32_t>(kClientId) << 16) | session_id_;

    std::vector<uint8_t> frame(kSomeipHeaderSize + payload.size());
    detail::pack_be32(frame.data() + 0, message_id);
    detail::pack_be32(frame.data() + 4, length);
    detail::pack_be32(frame.data() + 8, request_id);
    frame[12] = kProtocolVersion;
    frame[13] = kInterfaceVersion;
    frame[14] = kMsgTypeRequest;
    frame[15] = 0x00; // return code

    if (!payload.empty()) {
        std::memcpy(frame.data() + kSomeipHeaderSize, payload.data(), payload.size());
    }

    // Send header + payload in single call
    if (SendAll(frame.data(), frame.size()) < 0) {
        AFI_LOG_ERROR("Failed to send request for method 0x%04X", method_id);
        return -1;
    }

    AFI_LOG_DEBUG("Sent request: method=0x%04X session=%d payload=%zu",
                  method_id, session_id_, payload.size());

    // Receive response header (16 bytes)
    uint8_t resp_header[kSomeipHeaderSize];
    if (RecvExact(resp_header, kSomeipHeaderSize, timeout_ms) < 0) {
        AFI_LOG_ERROR("Failed to receive response header for method 0x%04X", method_id);
        return -1;
    }

    // Parse response header
    uint32_t resp_length = detail::unpack_be32(resp_header + 4);
    uint8_t  resp_msg_type = resp_header[14];
    return_code = resp_header[15];

    // Validate response
    if (resp_msg_type != kMsgTypeResponse && resp_msg_type != 0x81 /* ERROR */) {
        AFI_LOG_ERROR("Unexpected message type 0x%02X for method 0x%04X",
                      resp_msg_type, method_id);
        return -1;
    }

    // Receive response payload
    uint32_t resp_payload_size = (resp_length > 8) ? (resp_length - 8) : 0;

    if (resp_payload_size > kMaxResponsePayload) {
        AFI_LOG_ERROR("Response payload too large: %u bytes (max %u) for method 0x%04X",
                      resp_payload_size, kMaxResponsePayload, method_id);
        return -1;
    }

    response_payload.resize(resp_payload_size);

    if (resp_payload_size > 0) {
        if (RecvExact(response_payload.data(), resp_payload_size, timeout_ms) < 0) {
            AFI_LOG_ERROR("Failed to receive response payload (%u bytes) for method 0x%04X",
                          resp_payload_size, method_id);
            return -1;
        }
    }

    AFI_LOG_DEBUG("Received response: method=0x%04X rc=%d payload=%u",
                  method_id, return_code, resp_payload_size);

    return 0;
}

int TcpClient::RecvExact(uint8_t* buf, size_t n, int timeout_ms) {
    size_t received = 0;
    platform_set_recv_timeout(socket_, timeout_ms);

    while (received < n) {
        int r = ::recv(socket_, reinterpret_cast<char*>(buf + received),
                       static_cast<int>(n - received), 0);
        if (r <= 0) {
            if (r == 0) {
                AFI_LOG_ERROR("TCP connection closed by peer");
            }
            return -1;
        }
        received += static_cast<size_t>(r);
    }
    return 0;
}

int TcpClient::SendAll(const uint8_t* buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        int s = ::send(socket_, reinterpret_cast<const char*>(buf + sent),
                       static_cast<int>(n - sent), 0);
        if (s <= 0) return -1;
        sent += static_cast<size_t>(s);
    }
    return 0;
}

} // namespace afi
