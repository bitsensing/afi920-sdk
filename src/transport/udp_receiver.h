/**
 * @file udp_receiver.h
 * @brief UDP socket receiver for data streams and UDP sender for CSII
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
     * @brief Accept datagrams only from this IPv4 source (empty = any)
     *
     * Radars are told apart by source IP. Without a filter, a second radar
     * sending to the same port would mix into this stream and its SOME/IP-TP
     * segments into this reassembler. Filtered datagrams read as a timeout
     * and are counted by ForeignDrops().
     * @return false when `ip` is not a dotted IPv4 address (filter left off)
     */
    bool SetExpectedSource(const std::string& ip);

    uint64_t ForeignDrops() const { return foreign_drops_; }

    /**
     * @brief Close the socket
     */
    void Close();

    bool IsOpen() const { return socket_ != kInvalidSocket; }

private:
    socket_t socket_ = kInvalidSocket;
    bool filter_source_ = false;
    in_addr expected_source_{};
    uint64_t foreign_drops_ = 0;
};

/**
 * @brief Connected UDP sender for outbound notifications (CSII, UDP 30501)
 *
 * One Send() is one datagram. There is no session; Connect() only fixes the
 * destination address on the socket.
 */
class UdpSender {
public:
    UdpSender() = default;
    ~UdpSender();

    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    /**
     * @brief Create the socket and fix the destination
     * @return 0 on success, negative on error
     */
    int Connect(const std::string& ip, uint16_t port);

    void Disconnect();

    bool IsConnected() const { return socket_ != kInvalidSocket; }

    /**
     * @brief Send one datagram (thread-safe)
     * @return 0 on success, negative on error
     */
    int Send(const uint8_t* data, size_t len);

private:
    socket_t socket_ = kInvalidSocket;
    std::mutex mutex_;
};

} // namespace afi
