// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "transport/tcp_client.h"
#include "protocol/e2e.h"
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
static constexpr uint8_t  kMsgTypeError       = 0x81;  // defined; the radar answers errors as 0x80
static constexpr uint16_t kClientId           = 0x0001;
static constexpr size_t   kSomeipHeaderSize   = 16;
static constexpr size_t   kE2eHeaderBytes     = 20;
static constexpr uint32_t kMaxResponsePayload = 4 * 1024 * 1024;  // 4 MB

TcpClient::~TcpClient() {
    Disconnect();
}

int TcpClient::Connect(const std::string& ip, uint16_t port, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    ip_ = ip;
    port_ = port;
    connect_timeout_ms_ = timeout_ms;
    reconnect_ = true;
    generation_.store(AfiInterfaceGeneration::kUnknown);
    return ConnectLocked();
}

int TcpClient::ConnectLocked() {
    CloseLocked();

    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == kInvalidSocket) {
        AFI_LOG_ERROR("Failed to create TCP socket: %d", platform_socket_error());
        return kTcpTransport;
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

    platform_set_recv_timeout(socket_, connect_timeout_ms_);
    platform_set_send_timeout(socket_, connect_timeout_ms_);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) != 1) {
        AFI_LOG_ERROR("Invalid IP address: %s", ip_.c_str());
        CloseLocked();
        return kTcpTransport;
    }

    if (::connect(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        AFI_LOG_ERROR("TCP connect to %s:%d failed: %d", ip_.c_str(), port_,
                      platform_socket_error());
        CloseLocked();
        return kTcpTransport;
    }

    session_id_ = 0;
    AFI_LOG_INFO("TCP connected to %s:%d", ip_.c_str(), port_);
    return ProbeLocked();
}

// A v1.0.x sensor (firmware 3.0.0 to 3.1.0) reads the JSON body from frame
// offset 16, so the E2E header of a v1.1.0 request lands in the JSON and the
// parse fails; the sensor still runs the handler with no arguments. Restart,
// ResetAllSettings, FirmwareSwitchSlot and FirmwareUpdateReboot need none, so
// a v1.1.0 request would act on the sensor. GetDeviceInfo is read-only on
// both generations, and its answer tells them apart: a v1.0.x body starts
// with the JSON '{', a v1.1.0 body with the E2E header.
int TcpClient::ProbeLocked() {
    static constexpr uint16_t kGetDeviceInfo = 0x0010;
    static const std::vector<uint8_t> kEmptyJson = {'{', '}'};
    uint8_t rc = 0;
    std::vector<uint8_t> body;
    bool legacy = false;
    const int r = ExchangeLocked(kGetDeviceInfo, kEmptyJson, rc, body,
                                 request_timeout_ms_, &legacy);
    if (r != kTcpOk) {
        AFI_LOG_ERROR("Config API %s:%d: interface probe failed", ip_.c_str(), port_);
        return r;
    }
    if (legacy) {
        generation_.store(AfiInterfaceGeneration::kV1_0);
        AFI_LOG_WARN("Config API %s:%d: the sensor speaks interface v1.0.x "
                     "(firmware 3.0.0 to 3.1.0); Config API calls return "
                     "kNotSupported without sending", ip_.c_str(), port_);
        CloseLocked();
        return kTcpOk;
    }
    generation_.store(AfiInterfaceGeneration::kV1_1);
    return kTcpOk;
}

void TcpClient::Disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    reconnect_ = false;
    if (socket_ != kInvalidSocket) {
        CloseLocked();
        AFI_LOG_INFO("TCP disconnected");
    }
}

void TcpClient::CloseLocked() {
    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
    }
}

int TcpClient::SendRequest(uint16_t method_id,
                            const std::vector<uint8_t>& payload,
                            uint8_t& return_code,
                            std::vector<uint8_t>& response_payload,
                            int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_e2e_ok_ = false;
    return_code = 0;
    response_payload.clear();
    if (timeout_ms < 0) timeout_ms = request_timeout_ms_;

    if (generation_.load() == AfiInterfaceGeneration::kV1_0) return kTcpLegacySensor;
    if (payload.size() > kMaxRequestBody) {
        AFI_LOG_ERROR("Config API 0x%04X: request body %zu B exceeds %zu B",
                      method_id, payload.size(), kMaxRequestBody);
        return kTcpTooLarge;
    }
    if (socket_ == kInvalidSocket) {
        if (!reconnect_ || ConnectLocked() != kTcpOk) return kTcpTransport;
        if (generation_.load() == AfiInterfaceGeneration::kV1_0) return kTcpLegacySensor;
    }
    return ExchangeLocked(method_id, payload, return_code, response_payload,
                          timeout_ms, nullptr);
}

int TcpClient::ExchangeLocked(uint16_t method_id,
                              const std::vector<uint8_t>& payload,
                              uint8_t& return_code,
                              std::vector<uint8_t>& response_payload,
                              int timeout_ms, bool* legacy_body) {
    // Increment session ID (1 → 0xFFFF, wrap)
    session_id_++;
    if (session_id_ == 0) session_id_ = 1;

    // Build SOME/IP header + E2E header + body into a single buffer.
    // The radar reads the body from frame offset 36 (Config API Overview §1).
    uint32_t message_id = (static_cast<uint32_t>(kServiceId) << 16) | method_id;
    uint32_t length = 8 + static_cast<uint32_t>(kE2eHeaderBytes + payload.size());
    uint32_t request_id = (static_cast<uint32_t>(kClientId) << 16) | session_id_;
    const uint32_t data_id = (static_cast<uint32_t>(kServiceId) << 16) | method_id;

    std::vector<uint8_t> frame(kSomeipHeaderSize + kE2eHeaderBytes + payload.size());
    detail::pack_be32(frame.data() + 0, message_id);
    detail::pack_be32(frame.data() + 4, length);
    detail::pack_be32(frame.data() + 8, request_id);
    frame[12] = kProtocolVersion;
    frame[13] = kInterfaceVersion;
    frame[14] = kMsgTypeRequest;
    frame[15] = 0x00; // return code

    uint8_t* body = frame.data() + kSomeipHeaderSize + kE2eHeaderBytes;
    if (!payload.empty()) {
        std::memcpy(body, payload.data(), payload.size());
    }
    e2e_write_header(frame.data() + kSomeipHeaderSize, data_id, ++e2e_counter_,
                     body, payload.size());

    if (SendAll(frame.data(), frame.size()) < 0) {
        AFI_LOG_ERROR("Failed to send request for method 0x%04X", method_id);
        CloseLocked();
        return kTcpTransport;
    }

    AFI_LOG_DEBUG("Sent request: method=0x%04X session=%d payload=%zu",
                  method_id, session_id_, payload.size());

    uint8_t resp_header[kSomeipHeaderSize];
    int rr = RecvExact(resp_header, kSomeipHeaderSize, timeout_ms);
    if (rr != kTcpOk) {
        AFI_LOG_ERROR("Config API 0x%04X: no response header (%s)", method_id,
                      rr == kTcpTimeout ? "timeout" : "connection lost");
        CloseLocked();
        return rr;
    }

    const uint32_t resp_message_id = detail::unpack_be32(resp_header + 0);
    const uint32_t resp_length     = detail::unpack_be32(resp_header + 4);
    const uint16_t resp_client     = detail::unpack_be16(resp_header + 8);
    const uint16_t resp_session    = detail::unpack_be16(resp_header + 10);
    const uint8_t  resp_proto      = resp_header[12];
    const uint8_t  resp_iface      = resp_header[13];
    const uint8_t  resp_msg_type   = resp_header[14];

    // The response must answer this request: echoed Message ID, Client ID
    // and Session ID, version 0x01, RESPONSE type, and room for the E2E
    // header (a probe also accepts a v1.0.x body without it). Anything else
    // means the stream no longer lines up.
    const uint32_t min_length = 8 + (legacy_body ? 1 : kE2eHeaderBytes);
    if (resp_message_id != message_id || resp_client != kClientId ||
        resp_session != session_id_ || resp_proto != kProtocolVersion ||
        resp_iface != kInterfaceVersion ||
        (resp_msg_type != kMsgTypeResponse && resp_msg_type != kMsgTypeError) ||
        resp_length < min_length ||
        resp_length - 8 > kMaxResponsePayload) {
        AFI_LOG_ERROR("Config API 0x%04X: response id 0x%08X client %u session %u "
                      "version %u/%u type 0x%02X length %u does not answer "
                      "session %u", method_id, resp_message_id, resp_client,
                      resp_session, resp_proto, resp_iface, resp_msg_type,
                      resp_length, session_id_);
        CloseLocked();
        return kTcpProtocol;
    }
    return_code = resp_header[15];

    const uint32_t resp_payload_size = resp_length - 8;
    response_payload.resize(resp_payload_size);
    rr = RecvExact(response_payload.data(), resp_payload_size, timeout_ms);
    if (rr != kTcpOk) {
        AFI_LOG_ERROR("Config API 0x%04X: response body (%u B) %s", method_id,
                      resp_payload_size,
                      rr == kTcpTimeout ? "timed out" : "lost");
        response_payload.clear();
        CloseLocked();
        return rr;
    }

    if (legacy_body && response_payload[0] == '{') {
        *legacy_body = true;
        return kTcpOk;
    }
    if (resp_payload_size < kE2eHeaderBytes) {
        AFI_LOG_ERROR("Config API 0x%04X: response body %u B has no E2E header",
                      method_id, resp_payload_size);
        response_payload.clear();
        CloseLocked();
        return kTcpProtocol;
    }

    // Verify and strip the response E2E header. A failed check is reported
    // and the body is still handed to the caller (System Spec §7.5).
    E2eHeader e2e;
    const E2eStatus st = e2e_validate(response_payload.data(),
                                      response_payload.size(), e2e, data_id);
    last_e2e_ok_ = (st == E2eStatus::kOk);
    if (!last_e2e_ok_) {
        AFI_LOG_WARN("Config API 0x%04X: response E2E %s "
                     "(counter=%u data_id=0x%08X length=%u)",
                     method_id, e2e_status_name(st), e2e.counter,
                     e2e.data_id, e2e.length);
    }
    response_payload.erase(response_payload.begin(),
                           response_payload.begin() + kE2eHeaderBytes);

    AFI_LOG_DEBUG("Received response: method=0x%04X rc=%d payload=%u",
                  method_id, return_code, resp_payload_size);
    return kTcpOk;
}

int TcpClient::RecvExact(uint8_t* buf, size_t n, int timeout_ms) {
    size_t received = 0;
    platform_set_recv_timeout(socket_, timeout_ms);

    while (received < n) {
        int r = ::recv(socket_, reinterpret_cast<char*>(buf + received),
                       static_cast<int>(n - received), 0);
        if (r == 0) {
            AFI_LOG_ERROR("TCP connection closed by peer");
            return kTcpTransport;
        }
        if (r < 0) {
            return platform_last_error_is_timeout() ? kTcpTimeout : kTcpTransport;
        }
        received += static_cast<size_t>(r);
    }
    return kTcpOk;
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
