// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "afi_sdk_impl.h"
#include "discovery/discovery.h"
#include "util/endian_util.h"
#include "util/logger.h"
#include "bts_iso23150_e2e.h"
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace afi {

namespace {

bool ResolveTransport(const std::string& default_transport,
                      const std::string& stream_transport,
                      const char* stream_name, bool& use_tcp) {
    const std::string& value = stream_transport.empty()
        ? default_transport : stream_transport;
    if (value == "tcp") {
        use_tcp = true;
        return true;
    }
    if (value == "udp") {
        use_tcp = false;
        return true;
    }
    AFI_LOG_ERROR("Invalid %s transport '%s' (expected tcp or udp)",
                  stream_name, value.c_str());
    return false;
}

bool ValidateStreamMessage(const uint8_t* data, size_t len,
                           uint16_t expected_event, const char* stream_name,
                           SomeipHeader& hdr) {
    if (someip_deserialize(data, len, hdr) < 0) return false;
    if (hdr.service_id != kSomeipServiceId || hdr.method_id != expected_event) {
        return false;
    }
    if (hdr.length < 8 || static_cast<size_t>(hdr.length) + 8 != len) {
        AFI_LOG_WARN("%s SOME/IP length mismatch: header=%u frame=%zu",
                     stream_name, hdr.length, len);
        return false;
    }
    const uint8_t base_type = static_cast<uint8_t>(
        hdr.message_type & static_cast<uint8_t>(~kSomeipMsgTypeTpFlag));
    if (hdr.protocol_version != kSomeipProtocolVersion ||
        hdr.interface_version != kSomeipInterfaceVersion ||
        base_type != kSomeipMsgTypeNotify || hdr.return_code != kSomeipReturnOk) {
        AFI_LOG_WARN("%s invalid SOME/IP envelope", stream_name);
        return false;
    }
    return true;
}

} // namespace

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
void AfiSdk::RegRecvCallback(std::function<void(const AfiShiMessage&, const AfiStreamMeta&)> cb) { impl_->shi_meta_cb_ = std::move(cb); }
void AfiSdk::RegRecvCallback(std::function<void(const AfiSpiMessage&, const AfiStreamMeta&)> cb) { impl_->spi_meta_cb_ = std::move(cb); }
void AfiSdk::RegRecvCallback(std::function<void(uint32_t, uint32_t)> cb) { impl_->loss_cb_ = std::move(cb); }
void AfiSdk::RegWirePayloadCallback(
    std::function<void(const AfiWirePayloadFrame&)> cb) {
    impl_->wire_payload_cb_ = std::move(cb);
}

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

AfiInterfaceGeneration AfiSdk::GetInterfaceGeneration() const {
    return impl_->Generation();
}

AfiSensorResult AfiSdk::GetLastSensorResult() const {
    auto* cc = impl_->GetConfigClient();
    return cc ? cc->LastResult() : AfiSensorResult{};
}

int AfiSdk::ReconnectConfigApi(int timeout_ms) {
    return impl_->ReconnectConfigApi(timeout_ms);
}

// Access Mode (0x00Ex)
int AfiSdk::GetMode(AfiModeStatus& out) { DELEGATE_CONFIG(GetMode, out); }
int AfiSdk::SetMode(AfiAccessMode m, AfiModeStatus* out) { DELEGATE_CONFIG(SetMode, m, out); }
int AfiSdk::EnterMode(AfiAccessMode t) { DELEGATE_CONFIG(EnterMode, t); }

// Device Info & Status (0x001x)
int AfiSdk::GetDeviceInfo(AfiDeviceInfo& out) { DELEGATE_CONFIG(GetDeviceInfo, out); }
int AfiSdk::GetSensorStatus(AfiSensorStatus& out) { DELEGATE_CONFIG(GetSensorStatus, out); }
int AfiSdk::GetDiagnosticInfo(AfiDiagnosticInfo& out) { DELEGATE_CONFIG(GetDiagnosticInfo, out); }
int AfiSdk::Restart() { DELEGATE_CONFIG(Restart); }
int AfiSdk::ResetAllSettings() { DELEGATE_CONFIG(ResetAllSettings); }
int AfiSdk::GetCommandHistoryInfo(AfiCommandHistory& out) { DELEGATE_CONFIG(GetCommandHistoryInfo, out); }

// Network (0x002x)
int AfiSdk::GetNetworkConfig(AfiNetworkConfig& out) { DELEGATE_CONFIG(GetNetworkConfig, out); }
int AfiSdk::SetNetworkConfig(const AfiNetworkConfig& c, bool* rr) { DELEGATE_CONFIG(SetNetworkConfig, c, rr); }
int AfiSdk::SetStreamDestination(AfiStreamKind k, const AfiStreamDestination& d) { DELEGATE_CONFIG(SetStreamDestination, k, d); }

// Range Mode (0x003x)
int AfiSdk::GetRangeMode(AfiRangeMode& out) { DELEGATE_CONFIG(GetRangeMode, out); }
int AfiSdk::SetRangeMode(AfiRangeMode m) { DELEGATE_CONFIG(SetRangeMode, m, nullptr); }
int AfiSdk::SetRangeMode(AfiRangeMode m, bool* rr) { DELEGATE_CONFIG(SetRangeMode, m, rr); }

// Detection (0x004x)
int AfiSdk::GetDetectionThreshold(AfiDetectionThreshold& out) { DELEGATE_CONFIG(GetDetectionThreshold, out); }
int AfiSdk::SetDetectionThreshold(const AfiDetectionThreshold& t) { DELEGATE_CONFIG(SetDetectionThreshold, t); }
int AfiSdk::GetDetectionFilters(AfiDetectionFilters& out) { DELEGATE_CONFIG(GetDetectionFilters, out); }
int AfiSdk::SetDetectionFilters(const AfiDetectionFilters& f) { DELEGATE_CONFIG(SetDetectionFilters, f); }

// Time Sync (0x005x)
int AfiSdk::GetPtpConfig(AfiPtpConfig& out) { DELEGATE_CONFIG(GetPtpConfig, out); }
int AfiSdk::SetPtpConfig(uint8_t profile) {
    return SetPtpConfig(profile, nullptr);
}
int AfiSdk::SetPtpConfig(uint8_t profile, bool* restart_required) {
    DELEGATE_CONFIG(SetPtpConfig, profile, restart_required);
}

// Mounting (0x007x)
int AfiSdk::GetMountingPosition(AfiMountingPosition& out) { DELEGATE_CONFIG(GetMountingPosition, out); }
int AfiSdk::SetMountingPosition(const AfiMountingPosition& p) { DELEGATE_CONFIG(SetMountingPosition, p); }
int AfiSdk::GetRotation(AfiRotation& out) { DELEGATE_CONFIG(GetRotation, out); }
int AfiSdk::SetRotation(const AfiRotation& r) { DELEGATE_CONFIG(SetRotation, r); }
int AfiSdk::GetSensorPosition(AfiSensorPosition& out) { DELEGATE_CONFIG(GetSensorPosition, out); }
int AfiSdk::SetSensorPosition(const AfiSensorPosition& p) { DELEGATE_CONFIG(SetSensorPosition, p); }

// Firmware Update (0x008x)
int AfiSdk::FirmwareUpdateStart(const AfiOtaStartRequest& r, AfiOtaStartResponse& out) { DELEGATE_CONFIG(FirmwareUpdateStart, r, out); }
int AfiSdk::FirmwareDataChunk(const uint8_t* d, size_t l) { DELEGATE_CONFIG(FirmwareDataChunk, d, l); }
int AfiSdk::FirmwareDataChunk(const uint8_t* d, size_t l, AfiOtaChunkResult* out) { DELEGATE_CONFIG(FirmwareDataChunk, d, l, out); }
int AfiSdk::FirmwareUpdateCommit() { DELEGATE_CONFIG(FirmwareUpdateCommit); }
int AfiSdk::FirmwareUpdateStatus(AfiOtaStatus& out) { DELEGATE_CONFIG(FirmwareUpdateStatus, out); }
int AfiSdk::FirmwareUpdateAbort() { DELEGATE_CONFIG(FirmwareUpdateAbort); }
int AfiSdk::FirmwareSwitchSlot(const std::string& t, AfiOtaSwitchSlotResult& out) { DELEGATE_CONFIG(FirmwareSwitchSlot, t, out); }
int AfiSdk::FirmwareUpdateReboot() { DELEGATE_CONFIG(FirmwareUpdateReboot); }
int AfiSdk::GetBootInfo(AfiBootInfo& out) { DELEGATE_CONFIG(GetBootInfo, out); }

// Config Persistence (0x009x)
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
    const State state = state_.load();
    if (state != State::kNotInit && state != State::kStopped) {
        AFI_LOG_ERROR("SDK already initialized");
        return kAlreadyRunning;
    }
    state_.store(State::kNotInit);

    config_ = config;

    // Init may fail after one or more earlier stream sockets were opened.
    // Keep the object retryable and avoid retaining ports from a partial
    // initialization, especially when a later per-stream transport fails.
    const auto fail_init = [this](int error) {
        if (rdi_tcp_) {
            rdi_tcp_->SetStopping();
            rdi_tcp_->Close();
            rdi_tcp_.reset();
        }
        if (shi_tcp_) {
            shi_tcp_->SetStopping();
            shi_tcp_->Close();
            shi_tcp_.reset();
        }
        if (spi_tcp_) {
            spi_tcp_->SetStopping();
            spi_tcp_->Close();
            spi_tcp_.reset();
        }
        rdi_udp_.Close();
        shi_udp_.Close();
        spi_udp_.Close();
        csii_sender_.Disconnect();
        csii_tcp_.Disconnect();
        tcp_client_.Disconnect();
        config_client_.reset();
        return error;
    };

    stream_generation_.store(AfiInterfaceGeneration::kUnknown);
    if (!config_.csii_transport.empty() && config_.csii_transport != "udp" &&
        config_.csii_transport != "tcp") {
        AFI_LOG_ERROR("Invalid CSII transport '%s' (expected tcp, udp or empty)",
                      config_.csii_transport.c_str());
        return fail_init(kInvalidParam);
    }

    if ((config_.enable_rdi &&
         !ResolveTransport(config_.stream_transport, config_.rdi_transport,
                           "RDI", rdi_use_tcp_)) ||
        (config_.enable_shi &&
         !ResolveTransport(config_.stream_transport, config_.shi_transport,
                           "SHII", shi_use_tcp_)) ||
        (config_.enable_spi &&
         !ResolveTransport(config_.stream_transport, config_.spi_transport,
                           "SPI", spi_use_tcp_))) {
        return fail_init(kInvalidParam);
    }

    // Connect Config API (TCP)
    if (config_.enable_config_api) {
        tcp_client_.SetRequestTimeout(config_.request_timeout_ms);
        int rc = tcp_client_.Connect(config_.sensor_ip, config_.config_port,
                                     config_.connect_timeout_ms);
        if (rc < 0) {
            AFI_LOG_WARN("Config API connect failed (non-fatal for data streaming); "
                         "Config API calls connect on demand");
        }
        // The endpoint is kept either way: a later call, or ReconnectConfigApi()
        // after a Restart, connects again.
        config_client_ = std::make_unique<ConfigClient>(tcp_client_);
    }

    // Open each stream using its configured transport.
    if (config_.enable_rdi) {
        if (rdi_use_tcp_) {
            rdi_tcp_ = std::make_unique<TcpStreamClient>();
            if (rdi_tcp_->Open(config_.sensor_ip, config_.rdi_port,
                               config_.socket_buffer_size, config_.connect_timeout_ms) < 0) {
                AFI_LOG_ERROR("Failed to connect RDI TCP on %s:%d",
                              config_.sensor_ip.c_str(), config_.rdi_port);
                return fail_init(kConnectFailed);
            }
        } else if (rdi_udp_.Open(config_.rdi_port, config_.socket_buffer_size) < 0) {
            AFI_LOG_ERROR("Failed to open RDI UDP receiver on port %d", config_.rdi_port);
            return fail_init(kConnectFailed);
        }
    }
    if (config_.enable_shi) {
        if (shi_use_tcp_) {
            shi_tcp_ = std::make_unique<TcpStreamClient>();
            if (shi_tcp_->Open(config_.sensor_ip, config_.shi_port,
                               config_.socket_buffer_size,
                               config_.connect_timeout_ms) < 0) {
                AFI_LOG_ERROR("Failed to connect SHII TCP on %s:%d",
                              config_.sensor_ip.c_str(), config_.shi_port);
                return fail_init(kConnectFailed);
            }
        } else if (shi_udp_.Open(config_.shi_port, config_.socket_buffer_size) < 0) {
            AFI_LOG_ERROR("Failed to open SHII UDP receiver on port %d", config_.shi_port);
            return fail_init(kConnectFailed);
        }
    }
    if (config_.enable_spi) {
        if (spi_use_tcp_) {
            spi_tcp_ = std::make_unique<TcpStreamClient>();
            if (spi_tcp_->Open(config_.sensor_ip, config_.spi_port,
                               config_.socket_buffer_size,
                               config_.connect_timeout_ms) < 0) {
                AFI_LOG_ERROR("Failed to connect SPI TCP on %s:%d",
                              config_.sensor_ip.c_str(), config_.spi_port);
                return fail_init(kConnectFailed);
            }
        } else if (spi_udp_.Open(config_.spi_port, config_.socket_buffer_size) < 0) {
            AFI_LOG_ERROR("Failed to open SPI UDP receiver on port %d", config_.spi_port);
            return fail_init(kConnectFailed);
        }
    }
    // UDP streams accept only the sensor's own datagrams (12_System_Log tells
    // radars apart by source IP); see SetExpectedSource.
    for (UdpReceiver* udp : {&rdi_udp_, &shi_udp_, &spi_udp_}) {
        if (udp->IsOpen()) udp->SetExpectedSource(config_.sensor_ip);
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

    // Close receivers to unblock recv.
    if (rdi_tcp_) { rdi_tcp_->SetStopping(); rdi_tcp_->Close(); }
    else rdi_udp_.Close();
    if (shi_tcp_) { shi_tcp_->SetStopping(); shi_tcp_->Close(); }
    else shi_udp_.Close();
    if (spi_tcp_) { spi_tcp_->SetStopping(); spi_tcp_->Close(); }
    else spi_udp_.Close();

    // Join threads
    if (rdi_thread_ && rdi_thread_->joinable()) rdi_thread_->join();
    if (shi_thread_ && shi_thread_->joinable()) shi_thread_->join();
    if (spi_thread_ && spi_thread_->joinable()) spi_thread_->join();

    rdi_thread_.reset();
    shi_thread_.reset();
    spi_thread_.reset();

    // Disconnect TCP
    csii_sender_.Disconnect();
    csii_tcp_.Disconnect();
    tcp_client_.Disconnect();
    config_client_.reset();

    state_.store(State::kStopped);
    AFI_LOG_INFO("SDK stopped");
    return kOk;
}

int AfiSdk::Impl::ReconnectConfigApi(int timeout_ms) {
    if (!config_client_) return kNotInitialized;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (tcp_client_.Connect(config_.sensor_ip, config_.config_port,
                                config_.connect_timeout_ms) == kTcpOk) {
            return kOk;
        }
        if (std::chrono::steady_clock::now() >= deadline) return kConnectFailed;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// ─── CSII (Host → Radar) ───

int AfiSdk::Impl::SendVehicleInput(const AfiVehicleInput& input) {
    if (state_.load() == State::kNotInit) return kNotInitialized;

    std::lock_guard<std::mutex> lock(csii_mutex_);

    const bool use_tcp = CsiiUsesTcp();
    if (use_tcp) {
        csii_sender_.Disconnect();
        if (!csii_tcp_.IsConnected() &&
            csii_tcp_.Connect(config_.sensor_ip, config_.csii_port,
                              config_.connect_timeout_ms) < 0) {
            return kConnectFailed;
        }
    } else {
        csii_tcp_.Disconnect();
        if (!csii_sender_.IsConnected() &&
            csii_sender_.Connect(config_.sensor_ip, config_.csii_port) < 0) {
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

    const int sent = use_tcp ? csii_tcp_.Send(frame, kCsiiFrameSize)
                             : csii_sender_.Send(frame, kCsiiFrameSize);
    return sent < 0 ? kConnectFailed : kOk;
}

// Interface v1.0.x receives CSII on TCP 30501 only, v1.1.0 on UDP 30501 only.
bool AfiSdk::Impl::CsiiUsesTcp() const {
    if (config_.csii_transport == "tcp") return true;
    if (config_.csii_transport == "udp") return false;
    return Generation() == AfiInterfaceGeneration::kV1_0;
}

AfiInterfaceGeneration AfiSdk::Impl::Generation() const {
    const AfiInterfaceGeneration probed = tcp_client_.Generation();
    return probed != AfiInterfaceGeneration::kUnknown ? probed
                                                      : stream_generation_.load();
}

// ─── Thread Functions ───

void AfiSdk::Impl::RdiThreadFunc() {
    platform_set_thread_name("afi-rdi");
    AFI_LOG_INFO("RDI thread started (%s port %d)",
                 rdi_use_tcp_ ? "TCP" : "UDP", config_.rdi_port);

    // UDP has a protocol-defined datagram ceiling. TCP frames are sized from
    // their SOME/IP length, independent of the current typed RDI schema.
    std::vector<uint8_t> buf(kAfiMaxUdpDatagramBytes);

    while (running_.load()) {
        int n = rdi_use_tcp_
            ? rdi_tcp_->Receive(buf, kAfiWireFrameSafetyLimit)
            : rdi_udp_.Receive(buf.data(), buf.size());
        if (n <= 0) continue;
        ProcessRdiDatagram(buf.data(), static_cast<size_t>(n));
    }

    AFI_LOG_INFO("RDI thread stopped");
}

void AfiSdk::Impl::ShiThreadFunc() {
    platform_set_thread_name("afi-shi");
    AFI_LOG_INFO("SHII thread started (%s port %d)",
                 shi_use_tcp_ ? "TCP" : "UDP", config_.shi_port);

    std::vector<uint8_t> buf(kAfiMaxUdpDatagramBytes);

    while (running_.load()) {
        int n = shi_use_tcp_
            ? shi_tcp_->Receive(buf, kAfiWireFrameSafetyLimit)
            : shi_udp_.Receive(buf.data(), buf.size());
        if (n <= 0) continue;
        ProcessShiDatagram(buf.data(), static_cast<size_t>(n));
    }

    AFI_LOG_INFO("SHII thread stopped");
}

void AfiSdk::Impl::SpiThreadFunc() {
    platform_set_thread_name("afi-spi");
    AFI_LOG_INFO("SPI thread started (%s port %d)",
                 spi_use_tcp_ ? "TCP" : "UDP", config_.spi_port);

    std::vector<uint8_t> buf(kAfiMaxUdpDatagramBytes);

    while (running_.load()) {
        int n = spi_use_tcp_
            ? spi_tcp_->Receive(buf, kAfiWireFrameSafetyLimit)
            : spi_udp_.Receive(buf.data(), buf.size());
        if (n <= 0) continue;
        ProcessSpiDatagram(buf.data(), static_cast<size_t>(n));
    }

    AFI_LOG_INFO("SPI thread stopped");
}


// ─── Datagram Processing ───

bool AfiSdk::Impl::ConsumeE2eHeader(const uint8_t*& payload, size_t& payload_len,
                                    const char* stream_name,
                                    uint32_t expected_data_id,
                                    uint64_t& failure_count,
                                    E2eHeader* parsed_header,
                                    bool* e2e_valid) {
    if (e2e_valid) *e2e_valid = false;
    if (payload_len < kE2eHeaderSize) {
        AFI_LOG_WARN("%s frame shorter than E2E header: %zu", stream_name, payload_len);
        return false;
    }

    E2eHeader e2e;
    const E2eStatus st = e2e_validate(payload, payload_len, e2e,
                                      expected_data_id, config_.validate_e2e);
    if (st == E2eStatus::kOk) {
        if (e2e_valid) *e2e_valid = config_.validate_e2e;
        stream_generation_.store(e2e.generation);
    } else {
        // Verification only: report the failure and keep the frame. The
        // consumer decides on discard using AfiWirePayloadFrame::e2e_valid.
        ++failure_count;
        if (failure_count <= 10 || (failure_count % 1000) == 0) {
            AFI_LOG_WARN("%s E2E %s (counter=%u data_id=0x%08X expected=0x%08X "
                         "length=%u body=%zu, failures=%llu); frame delivered",
                         stream_name, e2e_status_name(st), e2e.counter,
                         e2e.data_id, expected_data_id, e2e.length,
                         payload_len - kE2eHeaderSize,
                         static_cast<unsigned long long>(failure_count));
        }
        if (config_.e2e_strict) return false;
    }

    if (parsed_header) *parsed_header = e2e;

    payload += kE2eHeaderSize;
    payload_len -= kE2eHeaderSize;
    return true;
}

void AfiSdk::Impl::DeliverWirePayload(AfiStreamKind stream,
                                      const uint8_t* wire_payload,
                                      size_t wire_payload_len,
                                      const uint8_t* iso_payload,
                                      size_t iso_payload_len,
                                      uint64_t recv_timestamp_ns,
                                      const E2eHeader* e2e,
                                      bool e2e_valid) {
    if (!wire_payload_cb_ || !wire_payload) return;

    AfiWirePayloadFrame frame;
    frame.stream = stream;
    frame.recv_timestamp_ns = recv_timestamp_ns;
    if (iso_payload && iso_payload_len >= 18) {
        frame.sensor_timestamp_ns = detail::unpack_le64(iso_payload + 6);
        frame.message_counter = detail::unpack_le32(iso_payload + 14);
    }
    if (e2e) {
        frame.e2e_counter = e2e->counter;
        frame.e2e_checked = config_.validate_e2e;
        frame.e2e_valid = e2e_valid;
        frame.generation = e2e->generation;
    }
    frame.payload.assign(wire_payload, wire_payload + wire_payload_len);
    wire_payload_cb_(frame);
}

void AfiSdk::Impl::TrackRdiLoss(uint32_t e2e_counter) {
    total_packets_.fetch_add(1);
    if (first_rdi_) {
        first_rdi_ = false;
        last_rdi_e2e_counter_ = e2e_counter;
        return;
    }
    // The E2E Counter advances once per send slot, so a gap is a missed
    // slot (System Spec §8.2). A decrease marks a radar restart and starts a
    // new baseline (§8.4). A repeat is not counted as loss.
    const uint32_t delta = e2e_counter - last_rdi_e2e_counter_;
    last_rdi_e2e_counter_ = e2e_counter;
    if (delta == 0 || delta >= 0x80000000u) return;
    if (delta > 1) {
        lost_packets_.fetch_add(delta - 1);
        if (loss_cb_) loss_cb_(total_packets_.load(), lost_packets_.load());
    }
}

void AfiSdk::Impl::ProcessRdiDatagram(const uint8_t* data, size_t len) {
    SomeipHeader hdr;
    if (!ValidateStreamMessage(data, len, kEventRdi, "RDI", hdr)) return;

    bool is_tp = someip_is_tp(hdr.message_type);
    size_t offset = kSomeipHeaderSize;

    const uint8_t* payload;
    size_t payload_len;

    if (is_tp) {
        // TP segment
        if (len < kSomeipHeaderSize + kSomeipTpHeaderSize) return;

        SomeipTpHeader tp;
        someip_tp_deserialize(data + offset, tp);
        if (!tp.valid) {
            rdi_tp_.Reset();
            return;
        }
        offset += kSomeipTpHeaderSize;

        size_t seg_len = len - offset;
        bool complete = rdi_tp_.Feed(data + offset, seg_len, tp.offset_bytes,
                                     tp.more_segments, hdr.session_id);

        if (!complete) return; // wait for more segments

        payload = rdi_tp_.GetPayload();
        payload_len = rdi_tp_.GetPayloadSize();
    } else {
        if (rdi_tp_.InProgress()) rdi_tp_.Reset();
        payload = data + offset;
        payload_len = len - offset;
    }

    // Preserve the complete E2E+ISO payload before the typed parser mutates
    // the interpretation or rejects a future schema.
    const uint8_t* wire_payload = payload;
    const size_t wire_payload_len = payload_len;
    E2eHeader e2e;
    bool e2e_valid = false;
    if (!ConsumeE2eHeader(payload, payload_len, "RDI", BTS_E2E_DATA_ID_RDI,
                          e2e_failures_[0], &e2e, &e2e_valid)) {
        if (is_tp) rdi_tp_.Reset();
        return;
    }

    const uint64_t recv_timestamp_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    DeliverWirePayload(AfiStreamKind::kDetection, wire_payload,
                       wire_payload_len, payload, payload_len,
                       recv_timestamp_ns, &e2e, e2e_valid);

    // The Counter is outside the CRC; a frame that failed its check does not
    // move the loss baseline.
    if (!config_.validate_e2e || e2e_valid) TrackRdiLoss(e2e.counter);

    // Typed decode is a separate, opt-in consumer of the payload path.
    if (rdi_cb_) {
        AfiRdiFrame frame{};
        frame.recv_timestamp_ns = recv_timestamp_ns;
        frame.frame_seq = rdi_frame_seq_.fetch_add(1);
        frame.e2e_counter = e2e.counter;
        frame.e2e_checked = config_.validate_e2e;
        frame.e2e_valid = e2e_valid;
        frame.generation = e2e.generation;
        if (rdi_parser_.Parse(payload, payload_len, frame) == 0) {
            rdi_cb_(frame);
        }
    }

    // Reset TP buffer after parsing (payload pointer must remain valid during Parse)
    if (is_tp) rdi_tp_.Reset();
}

void AfiSdk::Impl::ProcessShiDatagram(const uint8_t* data, size_t len) {
    SomeipHeader hdr;
    if (!ValidateStreamMessage(data, len, kEventShii, "SHII", hdr)) return;
    if (someip_is_tp(hdr.message_type)) return;

    // SHII is small (E2E 20B + variable payload ≤60B), no TP expected
    const uint8_t* payload = data + kSomeipHeaderSize;
    size_t payload_len = len - kSomeipHeaderSize;
    const uint8_t* wire_payload = payload;
    const size_t wire_payload_len = payload_len;
    E2eHeader e2e;
    bool e2e_valid = false;
    if (!ConsumeE2eHeader(payload, payload_len, "SHII",
                          BTS_E2E_DATA_ID_SHII, e2e_failures_[1],
                          &e2e, &e2e_valid)) return;

    const uint64_t recv_timestamp_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    DeliverWirePayload(AfiStreamKind::kHealth, wire_payload,
                       wire_payload_len, payload, payload_len,
                       recv_timestamp_ns, &e2e, e2e_valid);

    if (shi_cb_ || shi_meta_cb_) {
        AfiShiMessage msg{};
        if (shi_parser_.Parse(payload, payload_len, msg) == 0) {
            if (shi_meta_cb_) {
                shi_meta_cb_(msg, AfiStreamMeta{recv_timestamp_ns, e2e.counter,
                                                config_.validate_e2e, e2e_valid,
                                                e2e.generation});
            } else {
                shi_cb_(msg);
            }
        }
    }
}

void AfiSdk::Impl::ProcessSpiDatagram(const uint8_t* data, size_t len) {
    SomeipHeader hdr;
    if (!ValidateStreamMessage(data, len, kEventSpi, "SPI", hdr)) return;
    if (someip_is_tp(hdr.message_type)) return;

    // SPI is typically small (~500B), no TP expected
    const uint8_t* payload = data + kSomeipHeaderSize;
    size_t payload_len = len - kSomeipHeaderSize;
    const uint8_t* wire_payload = payload;
    const size_t wire_payload_len = payload_len;
    E2eHeader e2e;
    bool e2e_valid = false;
    if (!ConsumeE2eHeader(payload, payload_len, "SPI", BTS_E2E_DATA_ID_SPI,
                          e2e_failures_[2], &e2e, &e2e_valid)) return;

    const uint64_t recv_timestamp_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    DeliverWirePayload(AfiStreamKind::kPerformance, wire_payload,
                       wire_payload_len, payload, payload_len,
                       recv_timestamp_ns, &e2e, e2e_valid);

    if (spi_cb_ || spi_meta_cb_) {
        AfiSpiMessage msg{};
        if (spi_parser_.Parse(payload, payload_len, msg) == 0) {
            if (spi_meta_cb_) {
                spi_meta_cb_(msg, AfiStreamMeta{recv_timestamp_ns, e2e.counter,
                                                config_.validate_e2e, e2e_valid,
                                                e2e.generation});
            } else {
                spi_cb_(msg);
            }
        }
    }
}


} // namespace afi
