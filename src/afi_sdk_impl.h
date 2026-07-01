/**
 * @file afi_sdk_impl.h
 * @brief AfiSdk::Impl — internal implementation (PIMPL)
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_sdk.h"
#include "transport/udp_receiver.h"
#include "transport/tcp_client.h"
#include "transport/tcp_sender.h"
#include "transport/tcp_stream_client.h"
#include "config_api/config_client.h"
#include "protocol/someip.h"
#include "protocol/someip_tp.h"
#include "protocol/e2e.h"
#include "protocol/csii_builder.h"
#include "protocol/rdi_parser.h"
#include "protocol/shi_parser.h"
#include "protocol/spi_parser.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace afi {

class AfiSdk::Impl {
public:
    Impl() = default;
    ~Impl();

    int Init(const AfiSdkConfig& config);
    int Start();
    int Stop();
    bool IsRunning() const { return running_.load(); }

    // CSII (Host → Radar vehicle input)
    int SendVehicleInput(const AfiVehicleInput& input);

    // Callbacks
    std::function<void(const AfiRdiFrame&)>            rdi_cb_;
    std::function<void(const AfiShiMessage&)>          shi_cb_;
    std::function<void(const AfiSpiMessage&)>          spi_cb_;
    std::function<void(uint32_t, uint32_t)>            loss_cb_;

    // Config API (exposed for AfiSdk methods)
    ConfigClient* GetConfigClient() { return config_client_.get(); }

private:
    // ─── Thread functions ───
    void RdiThreadFunc();
    void ShiThreadFunc();
    void SpiThreadFunc();

    // ─── Process a single UDP datagram ───
    void ProcessRdiDatagram(const uint8_t* data, size_t len);
    void ProcessShiDatagram(const uint8_t* data, size_t len);
    void ProcessSpiDatagram(const uint8_t* data, size_t len);

    // ─── Strip (and optionally verify) the 20B E2E header ───
    // Advances payload/payload_len past the E2E header.
    // Returns false if the frame must be dropped.
    bool ConsumeE2eHeader(const uint8_t*& payload, size_t& payload_len,
                          const char* stream_name);

    // ─── Deliver parsed RDI frame (packet loss + callback) ───
    void DeliverRdiFrame(AfiRdiFrame& frame);

    // ─── Platform ───
    // MUST be first member: ensures WSAStartup before any socket operations.
    // On Linux/macOS this is a no-op.
    PlatformSocketInit socket_init_;

    // ─── State ───
    enum class State { kNotInit, kReady, kRunning, kStopped };
    std::atomic<State> state_{State::kNotInit};
    std::atomic<bool>  running_{false};
    AfiSdkConfig config_;

    // ─── Transport ───
    bool use_tcp_ = false;
    UdpReceiver rdi_udp_;
    UdpReceiver shi_udp_;
    UdpReceiver spi_udp_;
    std::unique_ptr<TcpStreamClient> rdi_tcp_;
    std::unique_ptr<TcpStreamClient> shi_tcp_;
    std::unique_ptr<TcpStreamClient> spi_tcp_;
    TcpClient   tcp_client_;
    std::unique_ptr<ConfigClient> config_client_;

    // ─── CSII outbound (lazy-connected on first SendVehicleInput) ───
    TcpSender   csii_sender_;
    std::mutex  csii_mutex_;
    uint32_t    csii_counter_ = 0;
    uint16_t    csii_session_ = 0;

    // ─── Protocol ───
    SomeipTpReassembler rdi_tp_;
    RdiParser rdi_parser_;
    ShiParser shi_parser_;
    SpiParser spi_parser_;

    // ─── Threads ───
    std::unique_ptr<std::thread> rdi_thread_;
    std::unique_ptr<std::thread> shi_thread_;
    std::unique_ptr<std::thread> spi_thread_;

    // ─── Stats ───
    std::atomic<uint32_t> rdi_frame_seq_{0};
    std::atomic<uint32_t> total_packets_{0};
    std::atomic<uint32_t> lost_packets_{0};
    uint32_t last_rdi_counter_ = 0;
    bool     first_rdi_ = true;
};

} // namespace afi
