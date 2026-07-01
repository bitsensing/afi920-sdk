// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "transport/tcp_sender.h"
#include "util/logger.h"
#include <cstring>
#ifdef _WIN32
#include <mstcpip.h>
#else
#include <netinet/tcp.h>
#endif

namespace afi {

TcpSender::~TcpSender() {
    Disconnect();
}

int TcpSender::Connect(const std::string& ip, uint16_t port, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
    }

    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == kInvalidSocket) {
        AFI_LOG_ERROR("TcpSender: failed to create socket: %d", platform_socket_error());
        return -1;
    }

    // TCP_NODELAY for low latency (cyclic 100ms frames)
    int nodelay = 1;
    setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

    int keepalive = 1;
    setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE,
               reinterpret_cast<const char*>(&keepalive), sizeof(keepalive));

    platform_set_send_timeout(socket_, timeout_ms);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        AFI_LOG_ERROR("TcpSender: invalid IP address: %s", ip.c_str());
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
        return -1;
    }

    if (::connect(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        AFI_LOG_ERROR("TcpSender: connect to %s:%d failed: %d",
                      ip.c_str(), port, platform_socket_error());
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
        return -1;
    }

    AFI_LOG_INFO("TcpSender: connected to %s:%d", ip.c_str(), port);
    return 0;
}

void TcpSender::Disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
    }
}

int TcpSender::Send(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (socket_ == kInvalidSocket) return -1;
    if (!data || len == 0) return -1;

    size_t sent = 0;
    while (sent < len) {
        int n = ::send(socket_, reinterpret_cast<const char*>(data + sent),
                       static_cast<int>(len - sent), 0);
        if (n <= 0) {
            AFI_LOG_ERROR("TcpSender: send failed: %d", platform_socket_error());
            platform_close_socket(socket_);
            socket_ = kInvalidSocket;
            return -1;
        }
        sent += static_cast<size_t>(n);
    }
    return 0;
}

} // namespace afi
