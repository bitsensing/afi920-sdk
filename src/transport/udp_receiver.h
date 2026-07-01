/**
 * @file udp_receiver.h
 * @brief UDP socket receiver for data streams
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "transport/platform.h"
#include <cstdint>
#include <string>

namespace afi {

class UdpReceiver {
public:
    UdpReceiver() = default;
    ~UdpReceiver();

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    /**
     * @brief Open UDP socket and bind to port
     * @param port UDP port to bind
     * @param buffer_size SO_RCVBUF size
     * @param timeout_ms Receive timeout in milliseconds
     * @return 0 on success, negative on error
     */
    int Open(uint16_t port, uint32_t buffer_size = 16 * 1024 * 1024,
             int timeout_ms = 1000,
             const std::string& multicast_group = "");

    /**
     * @brief Receive a single UDP datagram
     * @param buf Output buffer
     * @param buf_size Buffer size
     * @param[out] src_addr Source address (null-terminated IP string, optional)
     * @param src_addr_size Size of src_addr buffer
     * @return Number of bytes received, 0 on timeout, negative on error
     */
    int Receive(uint8_t* buf, size_t buf_size,
                char* src_addr = nullptr, size_t src_addr_size = 0);

    /**
     * @brief Close the socket
     */
    void Close();

    bool IsOpen() const { return socket_ != kInvalidSocket; }

private:
    socket_t socket_ = kInvalidSocket;
};

} // namespace afi
