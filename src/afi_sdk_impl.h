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

// Generic transport safety ceilings. These are deliberately independent of
// every version-specific domain schema: receive buffers and TP reassembly grow
// to the size announced by the wire protocol, up to this memory guard.
inline constexpr size_t kAfiWireFrameSafetyLimit = 64u * 1024u * 1024u;
inline constexpr size_t kAfiWirePayloadSafetyLimit =
    kAfiWireFrameSafetyLimit - kSomeipHeaderSize;
inline constexpr size_t kAfiMaxUdpDatagramBytes = 65536u;

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

    // Config API probe first, then the last E2E-valid stream frame.
    AfiInterfaceGeneration Generation() const;

    // Callbacks
    std::function<void(const AfiRdiFrame&)>            rdi_cb_;
    std::function<void(const AfiShiMessage&)>          shi_cb_;
    std::function<void(const AfiSpiMessage&)>          spi_cb_;
    std::function<void(const AfiShiMessage&, const AfiStreamMeta&)> shi_meta_cb_;
    std::function<void(const AfiSpiMessage&, const AfiStreamMeta&)> spi_meta_cb_;
    std::function<void(uint32_t, uint32_t)>            loss_cb_;
    std::function<void(const AfiWirePayloadFrame&)>    wire_payload_cb_;

    // Config API (exposed for AfiSdk methods)
    ConfigClient* GetConfigClient() { return config_client_.get(); }
    int ReconnectConfigApi(int timeout_ms);

private:
    // ─── Thread functions ───
    void RdiThreadFunc();
    void ShiThreadFunc();
    void SpiThreadFunc();

    // ─── Process a single UDP datagram ───
    void ProcessRdiDatagram(const uint8_t* data, size_t len);
    void ProcessShiDatagram(const uint8_t* data, size_t len);
    void ProcessSpiDatagram(const uint8_t* data, size_t len);

    // ─── Strip and verify the 20B E2E header ───
    // Advances payload/payload_len past the E2E header. A failed check
    // (length, Data ID, CRC, zero CRC) is logged as a rate-limited warning
    // and the frame is still delivered; *e2e_valid reports the result.
    // Returns false only when the frame is shorter than the E2E header, or
    // when e2e_strict is set and the check failed.
    bool ConsumeE2eHeader(const uint8_t*& payload, size_t& payload_len,
                          const char* stream_name, uint32_t expected_data_id,
                          uint64_t& failure_count,
                          E2eHeader* parsed_header, bool* e2e_valid);

    // Deliver a complete transport-normalized SOME/IP payload before the
    // version-specific parser sees it.
    void DeliverWirePayload(AfiStreamKind stream,
                            const uint8_t* wire_payload,
                            size_t wire_payload_len,
                            const uint8_t* iso_payload,
                            size_t iso_payload_len,
                            uint64_t recv_timestamp_ns,
                            const E2eHeader* e2e,
                            bool e2e_valid);

    // ─── RDI slot loss from the E2E Counter (System Spec §8.2) ───
    void TrackRdiLoss(uint32_t e2e_counter);

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
    bool rdi_use_tcp_ = false;
    bool shi_use_tcp_ = false;
    bool spi_use_tcp_ = false;
    UdpReceiver rdi_udp_;
    UdpReceiver shi_udp_;
    UdpReceiver spi_udp_;
    std::unique_ptr<TcpStreamClient> rdi_tcp_;
    std::unique_ptr<TcpStreamClient> shi_tcp_;
    std::unique_ptr<TcpStreamClient> spi_tcp_;
    TcpClient   tcp_client_;
    std::unique_ptr<ConfigClient> config_client_;

    // Generation of the last stream frame that passed the E2E check.
    std::atomic<AfiInterfaceGeneration> stream_generation_{AfiInterfaceGeneration::kUnknown};

    // ─── CSII outbound (30501, socket created on first SendVehicleInput) ───
    // UDP on interface v1.1.0, TCP on v1.0.x (see CsiiUsesTcp).
    bool CsiiUsesTcp() const;
    TcpSender   csii_tcp_;
    UdpSender   csii_sender_;
    std::mutex  csii_mutex_;
    uint32_t    csii_counter_ = 0;
    uint16_t    csii_session_ = 0;

    // ─── Protocol ───
    SomeipTpReassembler rdi_tp_{kAfiWirePayloadSafetyLimit};
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
    uint32_t last_rdi_e2e_counter_ = 0;
    bool     first_rdi_ = true;

    // E2E check failures per stream (indexed by AfiStreamKind). Each entry
    // is touched only by its own receiver thread.
    uint64_t e2e_failures_[3] = {0, 0, 0};
};

} // namespace afi
