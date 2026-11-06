/**
 * @file tcp_stream_client.h
 * @brief TCP stream client for receiving SOME/IP data streams
 *
 * Connects to the sensor's TCP data port and receives complete SOME/IP
 * framed messages. TCP framing: read 8B (MessageID + Length), then
 * read Length more bytes. Total = 8 + Length bytes.
 *
 * Auto-reconnects on connection drop with exponential backoff.
 * Best-effort: TCP_NODELAY enabled, no application-level retransmission.
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "transport/platform.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace afi {

class TcpStreamClient {
public:
    TcpStreamClient() = default;
    ~TcpStreamClient();

    TcpStreamClient(const TcpStreamClient&) = delete;
    TcpStreamClient& operator=(const TcpStreamClient&) = delete;

    /**
     * @brief Connect to sensor TCP data port
     * @param ip    Sensor IP address
     * @param port  TCP port number
     * @param recv_buf_size  SO_RCVBUF size (default 512KB)
     * @param connect_timeout_ms  Connection timeout
     * @return 0 on success, negative on error
     */
    int Open(const std::string& ip, uint16_t port,
             uint32_t recv_buf_size = 512 * 1024,
             int connect_timeout_ms = 5000);

    /**
     * @brief Receive one complete SOME/IP message
     *
     * Reads the 8-byte SOME/IP prefix (MessageID + Length), then reads
     * the remaining Length bytes. Returns the full SOME/IP message
     * including header.
     *
     * @param buf      Output buffer
     * @param buf_size Buffer size
     * @return Number of bytes received, negative on error/disconnect
     */
    int Receive(uint8_t* buf, size_t buf_size);

    /**
     * @brief Receive one complete SOME/IP message into a dynamically sized buffer
     *
     * The frame size is taken from the SOME/IP Length field.  This keeps stream
     * reception independent of the currently compiled RDI/SHII/SPI/Raw
     * Data domain schema.  max_frame_size is only a transport memory-safety
     * ceiling; it is not a typed payload limit.
     *
     * @param frame          Output buffer, resized to the complete frame
     * @param max_frame_size Maximum accepted complete SOME/IP frame size
     * @return Number of bytes received, negative on error/disconnect
     */
    int Receive(std::vector<uint8_t>& frame, size_t max_frame_size);

    /**
     * @brief Close the TCP connection
     */
    void Close();

    bool IsOpen() const { return open_.load(); }

    /// Mark client as shutting down (unblocks reconnect loop)
    void SetStopping() { running_.store(false); }

private:
    int recv_exact(uint8_t* buf, size_t n);
    void configure_socket();
    int try_reconnect();

    std::string ip_;
    uint16_t port_ = 0;
    uint32_t recv_buf_size_ = 512 * 1024;
    int connect_timeout_ms_ = 5000;

    socket_t socket_ = kInvalidSocket;
    std::atomic<bool> open_{false};
    std::atomic<bool> running_{true};

    int reconnect_delay_ms_ = 1000;
    static constexpr int kMaxReconnectDelay = 30000;
    static constexpr int kMinReconnectDelay = 1000;
};

} // namespace afi
