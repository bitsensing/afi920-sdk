/**
 * @file tcp_sender.h
 * @brief Minimal TCP client for outbound data streams (CSII)
 *
 * Connect-and-send only; no response is expected (SOME/IP NOTIFICATION).
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "transport/platform.h"
#include <cstdint>
#include <mutex>
#include <string>

namespace afi {

class TcpSender {
public:
    TcpSender() = default;
    ~TcpSender();

    TcpSender(const TcpSender&) = delete;
    TcpSender& operator=(const TcpSender&) = delete;

    /**
     * @brief Connect to the sensor
     * @return 0 on success, negative on error
     */
    int Connect(const std::string& ip, uint16_t port, int timeout_ms = 5000);

    void Disconnect();

    bool IsConnected() const { return socket_ != kInvalidSocket; }

    /**
     * @brief Send a complete frame (thread-safe)
     * @return 0 on success, negative on error (connection is closed on error)
     */
    int Send(const uint8_t* data, size_t len);

private:
    socket_t socket_ = kInvalidSocket;
    std::mutex mutex_;
};

} // namespace afi
