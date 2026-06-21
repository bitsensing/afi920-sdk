/**
 * @file tcp_client.h
 * @brief TCP client for SOME/IP Config API
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "transport/platform.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace afi {

class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    /**
     * @brief Connect to sensor Config API
     * @param ip Sensor IP address
     * @param port TCP port (default 30500)
     * @param timeout_ms Connection timeout
     * @return 0 on success, negative on error
     */
    int Connect(const std::string& ip, uint16_t port, int timeout_ms = 5000);

    /**
     * @brief Disconnect from sensor
     */
    void Disconnect();

    bool IsConnected() const { return socket_ != kInvalidSocket; }

    /**
     * @brief Send a complete SOME/IP request (header + payload)
     *
     * Thread-safe. Builds SOME/IP header, sends header+payload,
     * receives response, parses response header.
     *
     * @param method_id SOME/IP method ID
     * @param payload Request payload (can be empty)
     * @param[out] return_code SOME/IP return code from response
     * @param[out] response_payload Response payload data
     * @param timeout_ms Response timeout
     * @return 0 on success, negative on error
     */
    int SendRequest(uint16_t method_id,
                    const std::vector<uint8_t>& payload,
                    uint8_t& return_code,
                    std::vector<uint8_t>& response_payload,
                    int timeout_ms = 5000);

private:
    /**
     * @brief Receive exactly n bytes
     */
    int RecvExact(uint8_t* buf, size_t n, int timeout_ms);

    /**
     * @brief Send all bytes
     */
    int SendAll(const uint8_t* buf, size_t n);

    socket_t socket_ = kInvalidSocket;
    uint16_t session_id_ = 0;
    std::mutex mutex_;
};

} // namespace afi
