/**
 * @file tcp_client.h
 * @brief TCP client for SOME/IP Config API
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include "transport/platform.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace afi {

/// Transport outcome of TcpClient::SendRequest (the radar's return code is
/// reported separately).
enum TcpResult {
    kTcpOk           =  0,
    kTcpTransport    = -1,  ///< Not connected, connect/send failed, peer closed
    kTcpTimeout      = -2,  ///< No complete response within the timeout
    kTcpProtocol     = -3,  ///< Response does not answer this request
    kTcpTooLarge     = -4,  ///< Request body above kMaxRequestBody; not sent
    kTcpLegacySensor = -5,  ///< Sensor speaks interface v1.0.x; not sent
};

class TcpClient {
public:
    /// Request body limit (Config API Overview §1). The radar closes the
    /// connection without a reply when Length < 28 or > 28 + this.
    static constexpr size_t kMaxRequestBody = 16384;

    TcpClient() = default;
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    /**
     * @brief Connect to sensor Config API
     *
     * Remembers the endpoint even when the connect fails, so a later
     * SendRequest() connects on demand. Every connection starts with a
     * GetDeviceInfo probe that decides the interface generation. A v1.0.x
     * sensor runs a handler even when the v1.1.0 body does not parse, so
     * after a v1.0.x answer the connection is closed and every later
     * SendRequest() returns kTcpLegacySensor without sending, until the
     * next Connect().
     *
     * @param ip Sensor IP address
     * @param port TCP port (default 30500)
     * @param timeout_ms Connection timeout
     * @return 0 on success (a v1.0.x sensor included), negative on error
     */
    int Connect(const std::string& ip, uint16_t port, int timeout_ms = 5000);

    /// Generation the last probe found (kUnknown before a probe answered).
    AfiInterfaceGeneration Generation() const { return generation_.load(); }

    /**
     * @brief Disconnect and forget the endpoint (no reconnect on demand)
     */
    void Disconnect();

    bool IsConnected() const { return socket_ != kInvalidSocket; }

    /// True once Connect() was called and Disconnect() was not.
    bool HasEndpoint() const { return reconnect_; }

    /// Default response timeout for SendRequest(timeout_ms < 0).
    void SetRequestTimeout(int timeout_ms) { request_timeout_ms_ = timeout_ms; }

    /// Verdict of the last response's E2E check (false before any response).
    bool LastResponseE2eOk() const { return last_e2e_ok_; }

    /**
     * @brief Send a complete SOME/IP request (header + E2E + body)
     *
     * Thread-safe. Builds the SOME/IP header and the 20B E2E header
     * (Data ID 0x6000 << 16 | method_id, CRC-32 over the body), sends the
     * frame, receives the response, and strips the response E2E header.
     * A failed response E2E check is logged as a warning and the body is
     * still returned (System Spec §7.5); LastResponseE2eOk() holds the verdict.
     *
     * Any transport or framing failure closes the connection, because a late
     * response left in the socket would be read as the next request's answer.
     * The next call reconnects to the remembered endpoint.
     *
     * @param method_id SOME/IP method ID
     * @param payload Request body (can be empty)
     * @param[out] return_code SOME/IP return code from response
     * @param[out] response_payload Response body after the E2E header
     * @param timeout_ms Response timeout (negative: SetRequestTimeout value)
     * @return TcpResult
     */
    int SendRequest(uint16_t method_id,
                    const std::vector<uint8_t>& payload,
                    uint8_t& return_code,
                    std::vector<uint8_t>& response_payload,
                    int timeout_ms = -1);

private:
    int ConnectLocked();
    void CloseLocked();
    int ProbeLocked();

    /// One request/response on the open socket. With `legacy_body` set, a
    /// response whose body starts with '{' (no E2E header) is accepted,
    /// returned as is, and reported through *legacy_body.
    int ExchangeLocked(uint16_t method_id, const std::vector<uint8_t>& payload,
                       uint8_t& return_code, std::vector<uint8_t>& response_payload,
                       int timeout_ms, bool* legacy_body);

    /**
     * @brief Receive exactly n bytes
     * @return kTcpOk, kTcpTimeout, or kTcpTransport
     */
    int RecvExact(uint8_t* buf, size_t n, int timeout_ms);

    /**
     * @brief Send all bytes
     */
    int SendAll(const uint8_t* buf, size_t n);

    socket_t socket_ = kInvalidSocket;
    std::string ip_;
    uint16_t port_ = 0;
    int connect_timeout_ms_ = 5000;
    int request_timeout_ms_ = 5000;
    bool reconnect_ = false;
    bool last_e2e_ok_ = false;
    uint16_t session_id_ = 0;
    uint32_t e2e_counter_ = 0;
    std::atomic<AfiInterfaceGeneration> generation_{AfiInterfaceGeneration::kUnknown};
    std::mutex mutex_;
};

} // namespace afi
