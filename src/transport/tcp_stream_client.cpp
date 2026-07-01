/**
 * @file tcp_stream_client.cpp
 * @brief TCP stream client implementation
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "transport/tcp_stream_client.h"
#include "util/logger.h"

#include <cstring>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

namespace afi {

TcpStreamClient::~TcpStreamClient() {
    Close();
}

int TcpStreamClient::Open(const std::string& ip, uint16_t port,
                           uint32_t recv_buf_size, int connect_timeout_ms) {
    ip_ = ip;
    port_ = port;
    recv_buf_size_ = recv_buf_size;
    connect_timeout_ms_ = connect_timeout_ms;
    running_.store(true);

    // Close existing socket if any
    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
        open_.store(false);
    }

    socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidSocket) {
        AFI_LOG_ERROR("TCP socket creation failed");
        return -1;
    }

    // Non-blocking connect with timeout
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        platform_close_socket(fd);
        return -1;
    }

    int ret = ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

#ifdef _WIN32
    if (ret == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        platform_close_socket(fd);
        return -1;
    }
#else
    if (ret < 0 && errno != EINPROGRESS) {
        platform_close_socket(fd);
        return -1;
    }
#endif

    // Wait for connect
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    struct timeval tv;
    tv.tv_sec = connect_timeout_ms / 1000;
    tv.tv_usec = (connect_timeout_ms % 1000) * 1000;

#ifdef _WIN32
    int sel = ::select(0, nullptr, &wfds, nullptr, &tv);
#else
    int sel = ::select(static_cast<int>(fd) + 1, nullptr, &wfds, nullptr, &tv);
#endif
    if (sel <= 0) {
        platform_close_socket(fd);
        return -1;
    }

    // Verify connect result
    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR,
               reinterpret_cast<char*>(&err), &errlen);
    if (err != 0) {
        platform_close_socket(fd);
        return -1;
    }

    // Restore blocking mode
#ifdef _WIN32
    u_long block = 0;
    ioctlsocket(fd, FIONBIO, &block);
#else
    int blk_flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, blk_flags & ~O_NONBLOCK);
#endif

    socket_ = fd;
    configure_socket();
    open_.store(true);
    reconnect_delay_ms_ = kMinReconnectDelay;
    return 0;
}

int TcpStreamClient::Receive(uint8_t* buf, size_t buf_size) {
    if (!open_.load()) {
        return try_reconnect();
    }

    // Read 8 bytes: SOME/IP header first half (MessageID[4] + Length[4])
    uint8_t hdr[8];
    if (recv_exact(hdr, 8) != 0) {
        open_.store(false);
        return -1;
    }

    // Parse Length field (big-endian uint32 at offset 4)
    uint32_t length = (static_cast<uint32_t>(hdr[4]) << 24) |
                      (static_cast<uint32_t>(hdr[5]) << 16) |
                      (static_cast<uint32_t>(hdr[6]) << 8)  |
                      static_cast<uint32_t>(hdr[7]);

    size_t total = 8u + static_cast<size_t>(length);
    if (total > buf_size) {
        AFI_LOG_ERROR("TCP message too large: %zu > %zu", total, buf_size);
        open_.store(false);
        return -1;
    }

    // Copy header into caller's buffer
    std::memcpy(buf, hdr, 8);

    // Read remaining bytes
    if (recv_exact(buf + 8, static_cast<size_t>(length)) != 0) {
        open_.store(false);
        return -1;
    }

    return static_cast<int>(total);
}

void TcpStreamClient::Close() {
    running_.store(false);
    open_.store(false);
    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
    }
}

int TcpStreamClient::recv_exact(uint8_t* buf, size_t n) {
    size_t received = 0;
    while (received < n) {
#ifdef _WIN32
        int ret = ::recv(socket_, reinterpret_cast<char*>(buf + received),
                         static_cast<int>(n - received), 0);
        if (ret <= 0) return -1;
#else
        ssize_t ret = ::recv(socket_, buf + received, n - received, 0);
        if (ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ret == 0) return -1;  // connection closed
#endif
        received += static_cast<size_t>(ret);
    }
    return 0;
}

void TcpStreamClient::configure_socket() {
    // TCP_NODELAY: best-effort, no Nagle buffering
    int flag = 1;
    setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));

    // SO_KEEPALIVE
    int keepalive = 1;
    setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE,
               reinterpret_cast<const char*>(&keepalive), sizeof(keepalive));

#ifdef _WIN32
    struct tcp_keepalive ka = {1, 5000, 1000};
    DWORD bytes_returned = 0;
    WSAIoctl(socket_, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
             nullptr, 0, &bytes_returned, nullptr, nullptr);
#else
    int idle = 5;
    setsockopt(socket_, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    int intvl = 1;
    setsockopt(socket_, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    int cnt = 3;
    setsockopt(socket_, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif

    // SO_RCVBUF
    int rcvbuf = static_cast<int>(recv_buf_size_);
    setsockopt(socket_, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
}

int TcpStreamClient::try_reconnect() {
    if (!running_.load()) return -1;

    if (socket_ != kInvalidSocket) {
        platform_close_socket(socket_);
        socket_ = kInvalidSocket;
    }
    open_.store(false);

    std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms_));

    if (Open(ip_, port_, recv_buf_size_, connect_timeout_ms_) == 0) {
        AFI_LOG_INFO("TCP reconnected to %s:%d", ip_.c_str(), port_);
        return 0;
    }

    // Exponential backoff
    reconnect_delay_ms_ = (std::min)(reconnect_delay_ms_ * 2, kMaxReconnectDelay);
    return -1;
}

} // namespace afi
