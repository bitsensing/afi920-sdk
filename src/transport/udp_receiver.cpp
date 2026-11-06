// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "transport/udp_receiver.h"
#include "util/logger.h"
#include <cstring>
#include <string>

namespace afi {

UdpReceiver::~UdpReceiver() {
    Close();
}

int UdpReceiver::Open(uint16_t port, uint32_t buffer_size, int timeout_ms,
                       const std::string& multicast_group) {
    if (socket_ != kInvalidSocket) {
        AFI_LOG_WARN("UdpReceiver already open, closing first");
        Close();
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidSocket) {
        AFI_LOG_ERROR("Failed to create UDP socket: %d", platform_socket_error());
        return -1;
    }

    // Allow port reuse
    int reuse = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // Set receive buffer size
    int buf_sz = static_cast<int>(buffer_size);
    setsockopt(socket_, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&buf_sz), sizeof(buf_sz));

    // Set receive timeout
    platform_set_recv_timeout(socket_, timeout_ms);

    // Bind to port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        AFI_LOG_ERROR("Failed to bind UDP port %d: %d", port, platform_socket_error());
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
        return -1;
    }

    // Join multicast group if specified
    if (!multicast_group.empty()) {
        struct ip_mreq mreq{};
        if (inet_pton(AF_INET, multicast_group.c_str(), &mreq.imr_multiaddr) == 1) {
            mreq.imr_interface.s_addr = INADDR_ANY;
            if (setsockopt(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                           reinterpret_cast<const char*>(&mreq), sizeof(mreq)) < 0) {
                AFI_LOG_WARN("Failed to join multicast group %s: %d",
                             multicast_group.c_str(), platform_socket_error());
            } else {
                AFI_LOG_INFO("Joined multicast group %s", multicast_group.c_str());
            }
        } else {
            AFI_LOG_WARN("Invalid multicast address: %s", multicast_group.c_str());
        }
    }

    AFI_LOG_INFO("UDP receiver opened on port %d", port);
    return 0;
}

int UdpReceiver::Receive(uint8_t* buf, size_t buf_size,
                          char* src_addr, size_t src_addr_size) {
    if (socket_ == kInvalidSocket) return -1;

    struct sockaddr_in sender{};
    socklen_t sender_len = sizeof(sender);

    int n = ::recvfrom(socket_, reinterpret_cast<char*>(buf),
                       static_cast<int>(buf_size), 0,
                       reinterpret_cast<struct sockaddr*>(&sender), &sender_len);

    if (n < 0) {
        int err = platform_socket_error();
#ifdef _WIN32
        if (err == WSAETIMEDOUT) return 0;
#else
        if (err == EAGAIN || err == EWOULDBLOCK) return 0;
#endif
        return -1;
    }

    if (filter_source_ && sender.sin_addr.s_addr != expected_source_.s_addr) {
        ++foreign_drops_;
        return 0;
    }

    if (src_addr && src_addr_size > 0) {
        inet_ntop(AF_INET, &sender.sin_addr, src_addr, static_cast<socklen_t>(src_addr_size));
    }

    return n;
}

bool UdpReceiver::SetExpectedSource(const std::string& ip) {
    filter_source_ = false;
    if (ip.empty()) return true;
    if (inet_pton(AF_INET, ip.c_str(), &expected_source_) != 1) {
        AFI_LOG_WARN("UDP source filter: '%s' is not an IPv4 address; accepting any source",
                     ip.c_str());
        return false;
    }
    filter_source_ = true;
    return true;
}

void UdpReceiver::Close() {
    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
        AFI_LOG_DEBUG("UDP receiver closed");
    }
}

// ─── UdpSender ───

UdpSender::~UdpSender() {
    Disconnect();
}

int UdpSender::Connect(const std::string& ip, uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        AFI_LOG_ERROR("UdpSender: invalid IP address: %s", ip.c_str());
        return -1;
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidSocket) {
        AFI_LOG_ERROR("UdpSender: failed to create socket: %d", platform_socket_error());
        return -1;
    }

    if (::connect(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        AFI_LOG_ERROR("UdpSender: connect to %s:%d failed: %d",
                      ip.c_str(), port, platform_socket_error());
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
        return -1;
    }

    AFI_LOG_INFO("UDP sender ready for %s:%d", ip.c_str(), port);
    return 0;
}

void UdpSender::Disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
    }
}

int UdpSender::Send(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (socket_ == kInvalidSocket || !data) return -1;

    const int n = ::send(socket_, reinterpret_cast<const char*>(data),
                         static_cast<int>(len), 0);
    if (n < 0 || static_cast<size_t>(n) != len) {
        // A connected UDP socket reports an earlier ICMP unreachable on the
        // next call. The socket stays usable, so keep it open.
        AFI_LOG_WARN("UdpSender: send failed: %d", platform_socket_error());
        return -1;
    }
    return 0;
}

} // namespace afi
