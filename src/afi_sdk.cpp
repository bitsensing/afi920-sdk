// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "afi_sdk_impl.h"
#include "discovery/discovery.h"
#include "util/logger.h"
#include <chrono>
#include <cstring>
#include <vector>

namespace afi {

// ─── AfiSdk (facade) ───

AfiSdk::AfiSdk() : impl_(std::make_unique<Impl>()) {}
AfiSdk::~AfiSdk() = default;

int AfiSdk::Init(const AfiSdkConfig& config) { return impl_->Init(config); }
int AfiSdk::Start() { return impl_->Start(); }
int AfiSdk::Stop() { return impl_->Stop(); }
bool AfiSdk::IsRunning() const { return impl_->IsRunning(); }

void AfiSdk::RegRecvCallback(std::function<void(const AfiRdiFrame&)> cb) { impl_->rdi_cb_ = std::move(cb); }
void AfiSdk::RegRecvCallback(std::function<void(const AfiShiMessage&)> cb) { impl_->shi_cb_ = std::move(cb); }
void AfiSdk::RegRecvCallback(std::function<void(const AfiSpiMessage&)> cb) { impl_->spi_cb_ = std::move(cb); }
void AfiSdk::RegRecvCallback(std::function<void(uint32_t, uint32_t)> cb) { impl_->loss_cb_ = std::move(cb); }

int AfiSdk::SendVehicleInput(const AfiVehicleInput& input) { return impl_->SendVehicleInput(input); }

std::vector<AfiSensorInfo> AfiSdk::Discover(int timeout_ms, uint16_t port,
                                             const std::string& bind_ip) {
    return Discovery::Scan(timeout_ms, port, bind_ip);
}

// Config API delegation
#define DELEGATE_CONFIG(method, ...) \
    do { auto* _cc = impl_->GetConfigClient(); \
         if (!_cc) return kNotInitialized; \
         return _cc->method(__VA_ARGS__); } while(0)

// Device Info & Status (0x001x)
int AfiSdk::GetDeviceInfo(AfiDeviceInfo& out) { DELEGATE_CONFIG(GetDeviceInfo, out); }
int AfiSdk::GetSensorStatus(AfiSensorStatus& out) { DELEGATE_CONFIG(GetSensorStatus, out); }
int AfiSdk::GetDiagnosticInfo(AfiDiagnosticInfo& out) { DELEGATE_CONFIG(GetDiagnosticInfo, out); }
int AfiSdk::ClearFaultLog() { DELEGATE_CONFIG(ClearFaultLog); }
int AfiSdk::Restart() { DELEGATE_CONFIG(Restart); }
int AfiSdk::ResetAllSettings() { DELEGATE_CONFIG(ResetAllSettings); }
int AfiSdk::GetCommandHistoryInfo(AfiCommandHistory& out) { DELEGATE_CONFIG(GetCommandHistoryInfo, out); }

// Network (0x002x)
int AfiSdk::GetNetworkConfig(AfiNetworkConfig& out) { DELEGATE_CONFIG(GetNetworkConfig, out); }
int AfiSdk::SetNetworkConfig(const AfiNetworkConfig& c, bool* rr) { DELEGATE_CONFIG(SetNetworkConfig, c, rr); }
int AfiSdk::SetStreamDestination(AfiStreamKind k, const AfiStreamDestination& d) { DELEGATE_CONFIG(SetStreamDestination, k, d); }

// Range Mode (0x003x)
int AfiSdk::GetRangeMode(AfiRangeMode& out) { DELEGATE_CONFIG(GetRangeMode, out); }
int AfiSdk::SetRangeMode(AfiRangeMode m) { DELEGATE_CONFIG(SetRangeMode, m); }

// Detection (0x004x)
int AfiSdk::GetDetectionThreshold(AfiDetectionThreshold& out) { DELEGATE_CONFIG(GetDetectionThreshold, out); }
int AfiSdk::SetDetectionThreshold(const AfiDetectionThreshold& t) { DELEGATE_CONFIG(SetDetectionThreshold, t); }
int AfiSdk::GetDetectionFilters(AfiDetectionFilters& out) { DELEGATE_CONFIG(GetDetectionFilters, out); }
int AfiSdk::SetDetectionFilters(const AfiDetectionFilters& f) { DELEGATE_CONFIG(SetDetectionFilters, f); }
int AfiSdk::GetDetectionDensity(AfiDetectionDensity& out) { DELEGATE_CONFIG(GetDetectionDensity, out); }
int AfiSdk::SetDetectionDensity(const AfiDetectionDensity& d) { DELEGATE_CONFIG(SetDetectionDensity, d); }

// Time Sync (0x005x)
int AfiSdk::GetPtpConfig(AfiPtpConfig& out) { DELEGATE_CONFIG(GetPtpConfig, out); }
int AfiSdk::SetPtpConfig(uint8_t profile) { DELEGATE_CONFIG(SetPtpConfig, profile); }

// Mounting (0x007x)
int AfiSdk::GetMountingPosition(AfiMountingPosition& out) { DELEGATE_CONFIG(GetMountingPosition, out); }
int AfiSdk::SetMountingPosition(const AfiMountingPosition& p) { DELEGATE_CONFIG(SetMountingPosition, p); }
int AfiSdk::GetRotation(AfiRotation& out) { DELEGATE_CONFIG(GetRotation, out); }
int AfiSdk::SetRotation(const AfiRotation& r) { DELEGATE_CONFIG(SetRotation, r); }
int AfiSdk::GetSensorPosition(AfiSensorPosition& out) { DELEGATE_CONFIG(GetSensorPosition, out); }
int AfiSdk::SetSensorPosition(const AfiSensorPosition& p) { DELEGATE_CONFIG(SetSensorPosition, p); }

// Config Persistence (0x009x)
int AfiSdk::SaveConfig() { DELEGATE_CONFIG(SaveConfig); }
int AfiSdk::GetConfigStatus(AfiConfigStatus& out) { DELEGATE_CONFIG(GetConfigStatus, out); }
int AfiSdk::ResetConfigDefaults(const std::vector<std::string>& keys,
                                AfiResetConfigDefaultsResult* out) {
    DELEGATE_CONFIG(ResetConfigDefaults, keys, out);
}

#undef DELEGATE_CONFIG

// ─── Impl ───

AfiSdk::Impl::~Impl() {
    Stop();
}

int AfiSdk::Impl::Init(const AfiSdkConfig& config) {
    if (state_.load() != State::kNotInit) {
        AFI_LOG_ERROR("SDK already initialized");
        return kAlreadyRunning;
    }

    config_ = config;

    // Connect Config API (TCP)
    if (config_.enable_config_api) {
        int rc = tcp_client_.Connect(config_.sensor_ip, config_.config_port,
                                     config_.connect_timeout_ms);
        if (rc < 0) {
            AFI_LOG_WARN("Config API connect failed (non-fatal for data streaming)");
        } else {
            config_client_ = std::make_unique<ConfigClient>(tcp_client_);
        }
    }

    // Open data stream receivers (TCP or UDP)
    use_tcp_ = (config_.stream_transport == "tcp");

    if (use_tcp_) {
        AFI_LOG_INFO("Using TCP transport for data streams");
        if (config_.enable_rdi) {
            rdi_tcp_ = std::make_unique<TcpStreamClient>();
            if (rdi_tcp_->Open(config_.sensor_ip, config_.rdi_port,
                               config_.socket_buffer_size, config_.connect_timeout_ms) < 0) {
                AFI_LOG_ERROR("Failed to connect RDI TCP on %s:%d",
                              config_.sensor_ip.c_str(), config_.rdi_port);
                return kConnectFailed;
            }
        }
        if (config_.enable_shi) {
            shi_tcp_ = std::make_unique<TcpStreamClient>();
            if (shi_tcp_->Open(config_.sensor_ip, config_.shi_port,
                               4 * 1024, config_.connect_timeout_ms) < 0) {
                AFI_LOG_ERROR("Failed to connect SHII TCP on %s:%d",
                              config_.sensor_ip.c_str(), config_.shi_port);
                return kConnectFailed;
            }
        }
        if (config_.enable_spi) {
            spi_tcp_ = std::make_unique<TcpStreamClient>();
            if (spi_tcp_->Open(config_.sensor_ip, config_.spi_port,
                               4 * 1024, config_.connect_timeout_ms) < 0) {
                AFI_LOG_ERROR("Failed to connect SPI TCP on %s:%d",
                              config_.sensor_ip.c_str(), config_.spi_port);
                return kConnectFailed;
            }
        }
    } else {
        AFI_LOG_INFO("Using UDP transport for data streams");
        if (config_.enable_rdi) {
            if (rdi_udp_.Open(config_.rdi_port, config_.socket_buffer_size) < 0) {
                AFI_LOG_ERROR("Failed to open RDI receiver on port %d", config_.rdi_port);
                return kConnectFailed;
            }
        }
        if (config_.enable_shi) {
            if (shi_udp_.Open(config_.shi_port, config_.socket_buffer_size) < 0) {
                AFI_LOG_ERROR("Failed to open SHII receiver on port %d", config_.shi_port);
                return kConnectFailed;
            }
        }
        if (config_.enable_spi) {
            if (spi_udp_.Open(config_.spi_port, config_.socket_buffer_size) < 0) {
                AFI_LOG_ERROR("Failed to open SPI receiver on port %d", config_.spi_port);
                return kConnectFailed;
            }
        }
    }

    state_.store(State::kReady);
    AFI_LOG_INFO("SDK initialized for sensor %s", config_.sensor_ip.c_str());
    return kOk;
}

int AfiSdk::Impl::Start() {
    if (state_.load() != State::kReady) {
        AFI_LOG_ERROR("SDK not in Ready state");
        return kNotInitialized;
    }

    running_.store(true);

    if (config_.enable_rdi) {
        rdi_thread_ = std::make_unique<std::thread>(&Impl::RdiThreadFunc, this);
    }
    if (config_.enable_shi) {
        shi_thread_ = std::make_unique<std::thread>(&Impl::ShiThreadFunc, this);
    }
    if (config_.enable_spi) {
        spi_thread_ = std::make_unique<std::thread>(&Impl::SpiThreadFunc, this);
    }

    state_.store(State::kRunning);
    AFI_LOG_INFO("SDK started");
    return kOk;
}

int AfiSdk::Impl::Stop() {
    if (!running_.load() && state_.load() == State::kNotInit) return kOk;

    running_.store(false);

    // Close receivers to unblock recv
    if (use_tcp_) {
        if (rdi_tcp_) { rdi_tcp_->SetStopping(); rdi_tcp_->Close(); }
        if (shi_tcp_) { shi_tcp_->SetStopping(); shi_tcp_->Close(); }
        if (spi_tcp_) { spi_tcp_->SetStopping(); spi_tcp_->Close(); }
    } else {
        rdi_udp_.Close();
        shi_udp_.Close();
        spi_udp_.Close();
    }

    // Join threads
    if (rdi_thread_ && rdi_thread_->joinable()) rdi_thread_->join();
    if (shi_thread_ && shi_thread_->joinable()) shi_thread_->join();
    if (spi_thread_ && spi_thread_->joinable()) spi_thread_->join();

    rdi_thread_.reset();
    shi_thread_.reset();
    spi_thread_.reset();

    // Disconnect TCP
    csii_sender_.Disconnect();
    tcp_client_.Disconnect();
    config_client_.reset();

    state_.store(State::kStopped);
    AFI_LOG_INFO("SDK stopped");
    return kOk;
}

// ─── CSII (Host → Radar) ───

int AfiSdk::Impl::SendVehicleInput(const AfiVehicleInput& input) {
    if (state_.load() == State::kNotInit) return kNotInitialized;

    std::lock_guard<std::mutex> lock(csii_mutex_);

    if (!csii_sender_.IsConnected()) {
        if (csii_sender_.Connect(config_.sensor_ip, config_.csii_port,
                                 config_.connect_timeout_ms) < 0) {
            return kConnectFailed;
        }
    }

    uint64_t ts = input.timestamp_ns;
    if (ts == 0) {
        ts = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    csii_session_++;
    if (csii_session_ == 0) csii_session_ = 1;

    uint8_t frame[kCsiiFrameSize];
    csii_build_frame(input, csii_counter_++, csii_session_, ts, frame);

    if (csii_sender_.Send(frame, kCsiiFrameSize) < 0) return kConnectFailed;
    return kOk;
}

// ─── Thread Functions ───

void AfiSdk::Impl::RdiThreadFunc() {
    platform_set_thread_name("afi-rdi");
    AFI_LOG_INFO("RDI thread started (%s port %d)",
                 use_tcp_ ? "TCP" : "UDP", config_.rdi_port);

    // Large enough for a full v3.0 RDI frame (SOME/IP + E2E + max RDI payload).
    // Derived from the interface constant so it tracks BTS_RDI_MAX_DETECTIONS.
    static constexpr size_t kBufSize = BTS_RDI_MAX_PAYLOAD_SIZE + 4 * 1024;
    std::vector<uint8_t> buf(kBufSize);

    while (running_.load()) {
        int n = use_tcp_ ? rdi_tcp_->Receive(buf.data(), kBufSize)
                         : rdi_udp_.Receive(buf.data(), kBufSize);
        if (n <= 0) continue;
        ProcessRdiDatagram(buf.data(), static_cast<size_t>(n));
    }

    AFI_LOG_INFO("RDI thread stopped");
}

void AfiSdk::Impl::ShiThreadFunc() {
    platform_set_thread_name("afi-shi");
    AFI_LOG_INFO("SHII thread started (%s port %d)",
                 use_tcp_ ? "TCP" : "UDP", config_.shi_port);

    static constexpr size_t kBufSize = 2048;
    uint8_t buf[kBufSize];

    while (running_.load()) {
        int n = use_tcp_ ? shi_tcp_->Receive(buf, kBufSize)
                         : shi_udp_.Receive(buf, kBufSize);
        if (n <= 0) continue;
        ProcessShiDatagram(buf, static_cast<size_t>(n));
    }

    AFI_LOG_INFO("SHII thread stopped");
}

void AfiSdk::Impl::SpiThreadFunc() {
    platform_set_thread_name("afi-spi");
    AFI_LOG_INFO("SPI thread started (%s port %d)",
                 use_tcp_ ? "TCP" : "UDP", config_.spi_port);

    static constexpr size_t kBufSize = 2048;
    uint8_t buf[kBufSize];

    while (running_.load()) {
        int n = use_tcp_ ? spi_tcp_->Receive(buf, kBufSize)
                         : spi_udp_.Receive(buf, kBufSize);
        if (n <= 0) continue;
        ProcessSpiDatagram(buf, static_cast<size_t>(n));
    }

    AFI_LOG_INFO("SPI thread stopped");
}

// ─── Datagram Processing ───

bool AfiSdk::Impl::ConsumeE2eHeader(const uint8_t*& payload, size_t& payload_len,
                                    const char* stream_name) {
    if (payload_len < kE2eHeaderSize) {
        AFI_LOG_WARN("%s frame shorter than E2E header: %zu", stream_name, payload_len);
        return false;
    }

    if (config_.validate_e2e) {
        E2eHeader e2e;
        E2eStatus st = e2e_validate(payload, payload_len, e2e);
        if (st == E2eStatus::kCrcMismatch) {
            AFI_LOG_WARN("%s E2E CRC mismatch (counter=%u, data_id=0x%08X)",
                         stream_name, e2e.counter, e2e.data_id);
            if (config_.e2e_strict) return false;
        }
    }

    payload += kE2eHeaderSize;
    payload_len -= kE2eHeaderSize;
    return true;
}

void AfiSdk::Impl::DeliverRdiFrame(AfiRdiFrame& frame) {
    total_packets_.fetch_add(1);
    if (!first_rdi_) {
        uint32_t expected = last_rdi_counter_ + 1;
        if (frame.message.message_counter != expected) {
            uint32_t gap = frame.message.message_counter - expected;
            lost_packets_.fetch_add(gap);
            if (loss_cb_) loss_cb_(total_packets_.load(), lost_packets_.load());
        }
    } else {
        first_rdi_ = false;
    }
    last_rdi_counter_ = frame.message.message_counter;

    if (rdi_cb_) rdi_cb_(frame);
}

void AfiSdk::Impl::ProcessRdiDatagram(const uint8_t* data, size_t len) {
    if (len < kSomeipHeaderSize) return;

    // Parse SOME/IP header
    SomeipHeader hdr;
    if (someip_deserialize(data, len, hdr) < 0) return;

    // Verify it's an RDI event
    if (hdr.method_id != kEventRdi) return;

    bool is_tp = someip_is_tp(hdr.message_type);
    size_t offset = kSomeipHeaderSize;

    const uint8_t* payload;
    size_t payload_len;

    if (is_tp) {
        // TP segment
        if (len < kSomeipHeaderSize + kSomeipTpHeaderSize) return;

        SomeipTpHeader tp;
        someip_tp_deserialize(data + offset, tp);
        offset += kSomeipTpHeaderSize;

        size_t seg_len = len - offset;
        bool complete = rdi_tp_.Feed(data + offset, seg_len, tp.offset_bytes, tp.more_segments);

        if (!complete) return; // wait for more segments

        payload = rdi_tp_.GetPayload();
        payload_len = rdi_tp_.GetPayloadSize();
    } else {
        payload = data + offset;
        payload_len = len - offset;
    }

    // E2E header (20B) precedes the ISO 23150 payload
    if (!ConsumeE2eHeader(payload, payload_len, "RDI")) {
        if (is_tp) rdi_tp_.Reset();
        return;
    }

    // Common path: timestamp → parse → deliver
    AfiRdiFrame frame{};
    frame.recv_timestamp_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    frame.frame_seq = rdi_frame_seq_.fetch_add(1);

    if (rdi_parser_.Parse(payload, payload_len, frame) == 0) {
        DeliverRdiFrame(frame);
    }

    // Reset TP buffer after parsing (payload pointer must remain valid during Parse)
    if (is_tp) rdi_tp_.Reset();
}

void AfiSdk::Impl::ProcessShiDatagram(const uint8_t* data, size_t len) {
    if (len < kSomeipHeaderSize) return;

    SomeipHeader hdr;
    if (someip_deserialize(data, len, hdr) < 0) return;
    if (hdr.method_id != kEventShii) return;

    // SHII is small (E2E 20B + variable payload ≤60B), no TP expected
    const uint8_t* payload = data + kSomeipHeaderSize;
    size_t payload_len = len - kSomeipHeaderSize;
    if (!ConsumeE2eHeader(payload, payload_len, "SHII")) return;

    AfiShiMessage msg{};
    if (shi_parser_.Parse(payload, payload_len, msg) == 0) {
        if (shi_cb_) shi_cb_(msg);
    }
}

void AfiSdk::Impl::ProcessSpiDatagram(const uint8_t* data, size_t len) {
    if (len < kSomeipHeaderSize) return;

    SomeipHeader hdr;
    if (someip_deserialize(data, len, hdr) < 0) return;
    if (hdr.method_id != kEventSpi) return;

    // SPI is typically small (~500B), no TP expected
    const uint8_t* payload = data + kSomeipHeaderSize;
    size_t payload_len = len - kSomeipHeaderSize;
    if (!ConsumeE2eHeader(payload, payload_len, "SPI")) return;

    AfiSpiMessage msg{};
    if (spi_parser_.Parse(payload, payload_len, msg) == 0) {
        if (spi_cb_) spi_cb_(msg);
    }
}

} // namespace afi
