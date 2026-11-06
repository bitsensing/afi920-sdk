/**
 * @file test_config_transport.cpp
 * @brief Config API transport behaviour against a scripted loopback radar
 *
 * Pins what the client does when an answer is wrong, late, or refused
 * (Config API Overview §1, System Spec §6): the response must echo
 * Message/Client/Session ID with version 0x01; any mismatch, timeout, or
 * lost connection drops the socket so a late answer is never read as the
 * next one, and the next call reconnects. Radar return codes map to
 * distinct AfiError values and the last outcome stays readable. Every
 * connection opens with a GetDeviceInfo probe; an interface v1.0.x answer
 * (JSON without the E2E header) makes every later call kNotSupported
 * without sending.
 */
#include "afi/afi_sdk.h"
#include "config_api/config_client.h"
#include "protocol/e2e.h"
#include "transport/platform.h"
#include "transport/tcp_client.h"
#include "util/endian_util.h"
#include "legacy_vectors.h"
#include "test_common.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace afi;

namespace {

struct Request {
    uint16_t method = 0;
    uint16_t client = 0;
    uint16_t session = 0;
    size_t body_len = 0;
    std::string body;
};

struct Reply {
    bool silent = false;
    std::string json = R"({"return_code":0,"return_name":"SUCCESS","data":{}})";
    uint8_t rc = 0;
    int session_delta = 0;
    uint8_t proto = 0x01;
};

bool recv_exact(socket_t s, uint8_t* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        const int r = ::recv(s, reinterpret_cast<char*>(buf + got),
                             static_cast<int>(n - got), 0);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

// JSON body of the GetDeviceInfo answer a firmware 3.0.0 sensor (interface
// v1.0.x) gave to a v1.1.0 request.
std::string legacy_device_info() {
    const std::vector<uint8_t> rsp = legacy_file("getdeviceinfo_rsp.bin");
    return rsp.size() > 16 ? std::string(rsp.begin() + 16, rsp.end()) : std::string();
}

/// Interface v1.0.x framing: SOME/IP header and JSON, no E2E header.
std::vector<uint8_t> legacy_frame_for(const Request& req, const std::string& json) {
    std::vector<uint8_t> f(16 + json.size(), 0);
    detail::pack_be32(f.data(), 0x60000000u | req.method);
    detail::pack_be32(f.data() + 4, static_cast<uint32_t>(8 + json.size()));
    detail::pack_be16(f.data() + 8, req.client);
    detail::pack_be16(f.data() + 10, req.session);
    f[12] = 0x01;
    f[13] = 0x01;
    f[14] = 0x80;
    std::memcpy(f.data() + 16, json.data(), json.size());
    return f;
}

std::vector<uint8_t> frame_for(const Request& req, const Reply& r) {
    std::vector<uint8_t> f(16 + 20 + r.json.size(), 0);
    const uint32_t message_id = 0x60000000u | req.method;
    detail::pack_be32(f.data(), message_id);
    detail::pack_be32(f.data() + 4, static_cast<uint32_t>(8 + 20 + r.json.size()));
    detail::pack_be16(f.data() + 8, req.client);
    detail::pack_be16(f.data() + 10, static_cast<uint16_t>(req.session + r.session_delta));
    f[12] = r.proto;
    f[13] = 0x01;
    f[14] = 0x80;
    f[15] = r.rc;
    std::memcpy(f.data() + 36, r.json.data(), r.json.size());
    e2e_write_header(f.data() + 16, message_id, 1, f.data() + 36, r.json.size());
    return f;
}

/// Accepts connections in turn; script(index, request) decides each reply.
/// A v1.1.0 radar answers the GetDeviceInfo probe that opens each connection
/// itself, so the script and requests() see only the calls under test. A
/// legacy (v1.0.x) radar answers every request, the probe included, with
/// legacy_device_info() and records all of them.
class FakeRadar {
public:
    explicit FakeRadar(std::function<Reply(int, const Request&)> script,
                       bool legacy = false)
        : script_(std::move(script)), legacy_(legacy),
          legacy_json_(legacy ? legacy_device_info() : std::string()) {
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        socklen_t len = sizeof(addr);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        ::listen(listener_, 4);
        thread_ = std::thread([this] { Serve(); });
    }
    ~FakeRadar() {
        stop_ = true;
        // On Linux close() alone does not wake an accept() blocked in Serve().
#ifdef _WIN32
        ::shutdown(listener_, SD_BOTH);
#else
        ::shutdown(listener_, SHUT_RDWR);
#endif
        platform_close_socket(listener_);
        if (thread_.joinable()) thread_.join();
    }
    uint16_t port() const { return port_; }
    int connections() const { return connections_.load(); }
    int probes() const { return probes_.load(); }
    size_t requests() {
        std::lock_guard<std::mutex> lock(mutex_);
        return seen_.size();
    }

private:
    void Serve() {
        while (!stop_) {
            socket_t peer = ::accept(listener_, nullptr, nullptr);
            if (peer == kInvalidSocket) return;
            ++connections_;
            platform_set_recv_timeout(peer, 3000);
            bool first = true;
            while (!stop_) {
                uint8_t hdr[16];
                if (!recv_exact(peer, hdr, sizeof(hdr))) break;
                Request req;
                req.method = detail::unpack_be16(hdr + 2);
                req.client = detail::unpack_be16(hdr + 8);
                req.session = detail::unpack_be16(hdr + 10);
                std::vector<uint8_t> rest(detail::unpack_be32(hdr + 4) - 8);
                if (!recv_exact(peer, rest.data(), rest.size())) break;
                req.body_len = rest.size() - 20;
                req.body.assign(rest.begin() + 20, rest.end());
                const bool probe = first && req.method == 0x0010;
                first = false;
                if (probe) ++probes_;
                if (probe && !legacy_) {
                    Reply r;
                    r.json = R"({"return_code":0,"data":{"serial_number":"probe"}})";
                    const auto f = frame_for(req, r);
                    ::send(peer, reinterpret_cast<const char*>(f.data()),
                           static_cast<int>(f.size()), 0);
                    continue;
                }
                if (legacy_) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        seen_.push_back(req);
                    }
                    const auto f = legacy_frame_for(req, legacy_json_);
                    ::send(peer, reinterpret_cast<const char*>(f.data()),
                           static_cast<int>(f.size()), 0);
                    continue;
                }
                int index;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    seen_.push_back(req);
                    index = static_cast<int>(seen_.size()) - 1;
                }
                const Reply r = script_(index, req);
                if (r.silent) continue;
                const auto f = frame_for(req, r);
                ::send(peer, reinterpret_cast<const char*>(f.data()),
                       static_cast<int>(f.size()), 0);
            }
            platform_close_socket(peer);
        }
    }

    std::function<Reply(int, const Request&)> script_;
    bool legacy_ = false;
    std::string legacy_json_;
    socket_t listener_ = kInvalidSocket;
    uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> connections_{0};
    std::atomic<int> probes_{0};
    std::mutex mutex_;
    std::vector<Request> seen_;
    std::thread thread_;
};

const char* kSerialReply(int n) {
    return n == 0 ? R"({"return_code":0,"data":{"serial_number":"first"}})"
                  : R"({"return_code":0,"data":{"serial_number":"second"}})";
}

}  // namespace

// Tests run from static initializers (test_common.h); sockets come first.
static const bool g_sockets_ready = platform_init_sockets();

TEST(probe_finds_a_v110_sensor) {
    FakeRadar radar([](int, const Request&) { return Reply{}; });
    TcpClient tcp;
    CHECK(tcp.Generation() == AfiInterfaceGeneration::kUnknown);
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    CHECK(tcp.Generation() == AfiInterfaceGeneration::kV1_1);
    CHECK(tcp.IsConnected());
    CHECK(radar.probes() == 1 && radar.requests() == 0);
    PASS("probe_finds_a_v110_sensor");
}

TEST(legacy_sensor_gets_no_request_after_the_probe) {
    FakeRadar radar([](int, const Request&) { return Reply{}; }, true);
    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    CHECK(tcp.Generation() == AfiInterfaceGeneration::kV1_0);
    CHECK(!tcp.IsConnected());  // the one Config API slot is left free
    CHECK(radar.requests() == 1 && radar.probes() == 1);

    // The methods a v1.0.x sensor would run without arguments.
    ConfigClient cc(tcp);
    AfiOtaSwitchSlotResult slot;
    CHECK(cc.Restart() == kNotSupported);
    CHECK(cc.ResetAllSettings() == kNotSupported);
    CHECK(cc.FirmwareSwitchSlot("firmware", slot) == kNotSupported);
    CHECK(cc.FirmwareUpdateReboot() == kNotSupported);
    AfiDeviceInfo info;
    CHECK(cc.GetDeviceInfo(info) == kNotSupported);
    const AfiSensorResult last = cc.LastResult();
    CHECK(last.return_code == kRcNotSupported);
    CHECK(last.return_name == "NOT_SUPPORTED");
    CHECK(!last.error_message.empty());
    CHECK(radar.requests() == 1 && radar.connections() == 1);

    // An explicit Connect() probes again (a migrated sensor answers v1.1.0).
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    CHECK(radar.requests() == 2 && radar.connections() == 2);
    CHECK(tcp.Generation() == AfiInterfaceGeneration::kV1_0);
    PASS("legacy_sensor_gets_no_request_after_the_probe");
}

TEST(wrong_session_is_protocol_error_and_reconnects) {
    FakeRadar radar([](int i, const Request&) {
        Reply r;
        r.json = kSerialReply(i);
        if (i == 0) r.session_delta = 5;
        return r;
    });
    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    ConfigClient cc(tcp);
    AfiDeviceInfo info;
    CHECK(cc.GetDeviceInfo(info) == kProtocolError);
    CHECK(!tcp.IsConnected());
    CHECK(cc.GetDeviceInfo(info) == kOk);
    CHECK(info.serial_number == "second");
    CHECK(radar.connections() == 2);
    PASS("wrong_session_is_protocol_error_and_reconnects");
}

TEST(wrong_version_is_protocol_error) {
    FakeRadar radar([](int, const Request&) {
        Reply r;
        r.proto = 0x02;
        return r;
    });
    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    ConfigClient cc(tcp);
    AfiDeviceInfo info;
    CHECK(cc.GetDeviceInfo(info) == kProtocolError);
    PASS("wrong_version_is_protocol_error");
}

TEST(late_reply_is_never_read_as_the_next_answer) {
    FakeRadar radar([](int i, const Request&) {
        Reply r;
        r.json = kSerialReply(i);
        r.silent = (i == 0);
        return r;
    });
    TcpClient tcp;
    tcp.SetRequestTimeout(300);
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    ConfigClient cc(tcp);
    AfiDeviceInfo info;
    CHECK(cc.GetDeviceInfo(info) == kTimeout);
    CHECK(!tcp.IsConnected());
    CHECK(cc.GetDeviceInfo(info) == kOk);
    CHECK(info.serial_number == "second");
    PASS("late_reply_is_never_read_as_the_next_answer");
}

TEST(request_body_limit) {
    FakeRadar radar([](int, const Request&) { return Reply{}; });
    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    ConfigClient cc(tcp);
    std::vector<uint8_t> chunk(TcpClient::kMaxRequestBody + 1, 0);
    CHECK(cc.FirmwareDataChunk(chunk.data(), chunk.size()) == kInvalidParam);
    CHECK(radar.requests() == 0);
    CHECK(cc.FirmwareDataChunk(chunk.data(), TcpClient::kMaxRequestBody) == kOk);
    CHECK(radar.requests() == 1);
    PASS("request_body_limit");
}

TEST(radar_return_codes_are_distinct_errors) {
    struct Case { uint8_t rc; const char* name; int want; };
    static const Case cases[] = {
        {0x01, "INVALID_PARAM", kInvalidParam},
        {0x05, "NOT_SUPPORTED", kNotSupported},
        {0x07, "INTERNAL_ERROR", kSensorError},
        {0x08, "BUSY", kBusy},
        {0x0C, "SESSION_REQUIRED", kSessionRequired},
        {0x0D, "ACCESS_DENIED", kAccessDenied},
        {0x0E, "INVALID_STATE", kInvalidState},
    };
    FakeRadar radar([](int i, const Request&) {
        Reply r;
        r.rc = cases[i].rc;
        r.json = std::string(R"({"return_code":)") + std::to_string(cases[i].rc) +
                 R"(,"return_name":")" + cases[i].name +
                 R"(","data":{"error_message":"why"}})";
        return r;
    });
    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    ConfigClient cc(tcp);
    for (const auto& c : cases) {
        CHECK(cc.SetRotation(AfiRotation{}) == c.want);
        const AfiSensorResult last = cc.LastResult();
        CHECK(last.return_code == c.rc);
        CHECK(last.return_name == c.name);
        CHECK(last.error_message == "why");
        CHECK(last.e2e_ok);
    }
    CHECK(tcp.IsConnected());  // a refusal is an answer, not a broken stream
    PASS("radar_return_codes_are_distinct_errors");
}

TEST(disconnected_client_does_not_reconnect) {
    FakeRadar radar([](int, const Request&) { return Reply{}; });
    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    tcp.Disconnect();
    ConfigClient cc(tcp);
    AfiDeviceInfo info;
    CHECK(cc.GetDeviceInfo(info) == kDisconnected);
    CHECK(radar.connections() <= 1);  // the accept may still be in flight
    PASS("disconnected_client_does_not_reconnect");
}

// A radar that follows 11_Access_Mode: one-way Normal -> Diagnostic -> Programming.
static Reply mode_reply(int& mode, const Request& req) {
    Reply r;
    if (req.method == 0x00E0) {
        r.json = std::string(R"({"return_code":0,"data":{"access_mode":)") +
                 std::to_string(mode) + R"(,"access_mode_name":"x","elapsed_ms":5,"streams_halted":)" +
                 (mode == 2 ? "1" : "0") + "}}";
        return r;
    }
    const int want = req.body.find("\"access_mode\":2") != std::string::npos ? 2 : 1;
    if (want == mode || mode == 2) {
        r.rc = 0x0D;
        r.json = R"({"return_code":13,"return_name":"ACCESS_DENIED","data":{}})";
    } else if (want == 2 && mode == 0) {
        r.rc = 0x0C;
        r.json = R"({"return_code":12,"return_name":"SESSION_REQUIRED","data":{}})";
    } else {
        mode = want;
        r.json = std::string(R"({"return_code":0,"data":{"access_mode":)") +
                 std::to_string(mode) + R"(,"streams_halted":)" + (mode == 2 ? "1" : "0") + "}}";
    }
    return r;
}

TEST(enter_mode_walks_the_one_way_chain) {
    struct Case { int start; AfiAccessMode target; int want; int end; };
    static const Case cases[] = {
        {0, AfiAccessMode::kDiagnostic, kOk, 1},
        {0, AfiAccessMode::kProgramming, kOk, 2},
        {1, AfiAccessMode::kProgramming, kOk, 2},
        {1, AfiAccessMode::kDiagnostic, kOk, 1},
        {2, AfiAccessMode::kDiagnostic, kAccessDenied, 2},
    };
    for (const auto& c : cases) {
        int mode = c.start;
        FakeRadar radar([&mode](int, const Request& req) { return mode_reply(mode, req); });
        TcpClient tcp;
        CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
        ConfigClient cc(tcp);
        CHECK(cc.EnterMode(c.target) == c.want);
        CHECK(mode == c.end);
    }
    PASS("enter_mode_walks_the_one_way_chain");
}

TEST(set_mode_refusals_and_status) {
    int mode = 0;
    FakeRadar radar([&mode](int, const Request& req) { return mode_reply(mode, req); });
    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    ConfigClient cc(tcp);
    CHECK(cc.SetMode(AfiAccessMode::kNormal) == kInvalidParam);
    CHECK(radar.requests() == 0);
    CHECK(cc.SetMode(AfiAccessMode::kProgramming) == kSessionRequired);
    AfiModeStatus st;
    CHECK(cc.SetMode(AfiAccessMode::kDiagnostic, &st) == kOk);
    CHECK(st.access_mode == AfiAccessMode::kDiagnostic && !st.streams_halted);
    CHECK(cc.SetMode(AfiAccessMode::kDiagnostic) == kAccessDenied);
    CHECK(cc.SetMode(AfiAccessMode::kProgramming, &st) == kOk);
    CHECK(st.streams_halted);
    CHECK(cc.GetMode(st) == kOk);
    CHECK(st.access_mode == AfiAccessMode::kProgramming && st.elapsed_ms == 5);
    PASS("set_mode_refusals_and_status");
}

TEST(v110_fields_parse) {
    FakeRadar radar([](int, const Request& req) {
        Reply r;
        if (req.method == 0x0040) {
            r.json = R"({"return_code":0,"data":{"snr_threshold_db":5.50,)"
                     R"("cfar_table_first_frame":[5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5]}})";
        } else if (req.method == 0x0050) {
            r.json = R"({"return_code":0,"data":{"profile":0,"state":"SLAVE",)"
                     R"("grid_offset_ns":-120,"grid_state":"CONVERGING"}})";
        } else if (req.method == 0x0030) {
            r.json = R"({"return_code":0,"data":{"range_mode":6,"range_mode_name":"UDR"}})";
        } else {
            r.json = R"({"return_code":0,"data":{"current_range_mode":6,"restart_required":true}})";
        }
        return r;
    });
    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    ConfigClient cc(tcp);
    AfiDetectionThreshold th;
    CHECK(cc.GetDetectionThreshold(th) == kOk);
    CHECK(th.snr_dB == 5.5f);
    CHECK(!th.has_cfar_table_second_frame);
    AfiPtpConfig ptp;
    CHECK(cc.GetPtpConfig(ptp) == kOk);
    CHECK(ptp.grid_offset_ns == -120 && ptp.grid_state == "CONVERGING");
    CHECK(ptp.priority1 == 255);
    AfiRangeMode mode;
    CHECK(cc.GetRangeMode(mode) == kOk);
    CHECK(mode == AfiRangeMode::kUltraDualRange);
    CHECK(std::string(AfiRangeModeName(mode)) == "UDR");
    bool restart = false;
    CHECK(cc.SetRangeMode(AfiRangeMode::kUltraDualRange, &restart) == kOk && restart);
    CHECK(cc.SetRangeMode(static_cast<AfiRangeMode>(7)) == kInvalidParam);
    th.snr_dB = 7.25f;
    CHECK(cc.SetDetectionThreshold(th) == kOk);
    th.snr_dB = 9.5f;
    CHECK(cc.SetDetectionThreshold(th) == kInvalidParam);
    PASS("v110_fields_parse");
}

TEST(firmware_low_level) {
    std::vector<std::string> bodies;
    FakeRadar radar([&bodies](int, const Request& req) {
        Reply r;
        bodies.push_back(req.body);
        if (req.method == 0x0080) {
            r.json = R"({"return_code":0,"data":{"max_chunk_size":4096,"target_slot":"B"}})";
        } else if (req.method == 0x0081) {
            r.json = R"({"return_code":0,"data":{"received_bytes":256,"total_bytes":9000}})";
        }
        return r;
    });
    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", radar.port(), 2000) == kTcpOk);
    ConfigClient cc(tcp);
    AfiOtaStartRequest req;
    req.update_type = "firmware";
    req.component = "m7_2";
    req.total_size = 9000;
    req.crc32 = "1a2b3c4d";                       // bare hex: the radar would read decimal
    AfiOtaStartResponse start;
    CHECK(cc.FirmwareUpdateStart(req, start) == kOk);
    CHECK(bodies.back().find("\"crc32\":\"0x1a2b3c4d\"") != std::string::npos);
    CHECK(start.max_chunk_size == 4096 && start.target_slot == "B");
    req.crc32.clear();                            // empty: key omitted, not ""
    CHECK(cc.FirmwareUpdateStart(req, start) == kOk);
    CHECK(bodies.back().find("crc32") == std::string::npos);
    std::vector<uint8_t> chunk(1024, 0xAB);
    AfiOtaChunkResult cr;
    CHECK(cc.FirmwareDataChunk(chunk.data(), chunk.size(), &cr) == kOk);
    CHECK(cr.received_bytes == 256 && cr.total_bytes == 9000);
    PASS("firmware_low_level");
}

namespace {

/// Loopback listener on one port number for both TCP and UDP; counts the
/// CSII frames that arrive on each.
class CsiiSink {
public:
    CsiiSink() {
        tcp_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(tcp_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        socklen_t len = sizeof(addr);
        ::getsockname(tcp_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        ::listen(tcp_, 2);
        udp_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ::bind(udp_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        platform_set_recv_timeout(udp_, 100);
        tcp_thread_ = std::thread([this] {
            socket_t peer = ::accept(tcp_, nullptr, nullptr);
            if (peer == kInvalidSocket) return;
            platform_set_recv_timeout(peer, 3000);
            uint8_t frame[115];
            while (recv_exact(peer, frame, sizeof(frame))) {
                if (detail::unpack_be32(frame) == 0x60008001u) ++tcp_frames_;
            }
            platform_close_socket(peer);
        });
        udp_thread_ = std::thread([this] {
            uint8_t frame[256];
            while (!stop_) {
                const int n = ::recv(udp_, reinterpret_cast<char*>(frame), sizeof(frame), 0);
                if (n == 115 && detail::unpack_be32(frame) == 0x60008001u) ++udp_frames_;
            }
        });
    }
    ~CsiiSink() {
        stop_ = true;
#ifdef _WIN32
        ::shutdown(tcp_, SD_BOTH);
#else
        ::shutdown(tcp_, SHUT_RDWR);
#endif
        platform_close_socket(tcp_);
        tcp_thread_.join();
        udp_thread_.join();
        platform_close_socket(udp_);
    }
    uint16_t port() const { return port_; }
    int tcp_frames() const { return tcp_frames_.load(); }
    int udp_frames() const { return udp_frames_.load(); }

private:
    socket_t tcp_ = kInvalidSocket;
    socket_t udp_ = kInvalidSocket;
    uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> tcp_frames_{0};
    std::atomic<int> udp_frames_{0};
    std::thread tcp_thread_;
    std::thread udp_thread_;
};

bool wait_for(const std::function<bool()>& done) {
    for (int i = 0; i < 200 && !done(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

AfiSdkConfig csii_only_config(uint16_t config_port, uint16_t csii_port) {
    AfiSdkConfig config;
    config.sensor_ip = "127.0.0.1";
    config.config_port = config_port;
    config.csii_port = csii_port;
    config.enable_rdi = false;
    config.connect_timeout_ms = 2000;
    config.request_timeout_ms = 2000;
    return config;
}

}  // namespace

TEST(sdk_sends_csii_over_tcp_to_a_legacy_sensor) {
    FakeRadar radar([](int, const Request&) { return Reply{}; }, true);
    CsiiSink sink;
    AfiSdk sdk;
    CHECK(sdk.Init(csii_only_config(radar.port(), sink.port())) == kOk);
    CHECK(sdk.GetInterfaceGeneration() == AfiInterfaceGeneration::kV1_0);
    for (int i = 0; i < 3; ++i) CHECK(sdk.SendVehicleInput(AfiVehicleInput{}) == kOk);
    CHECK(wait_for([&] { return sink.tcp_frames() == 3; }));
    CHECK(sink.udp_frames() == 0);
    AfiDeviceInfo info;
    CHECK(sdk.GetDeviceInfo(info) == kNotSupported);
    CHECK(radar.requests() == 1);  // the probe
    sdk.Stop();
    PASS("sdk_sends_csii_over_tcp_to_a_legacy_sensor");
}

TEST(sdk_sends_csii_over_udp_to_a_v110_sensor) {
    FakeRadar radar([](int, const Request&) { return Reply{}; });
    CsiiSink sink;
    AfiSdk sdk;
    CHECK(sdk.Init(csii_only_config(radar.port(), sink.port())) == kOk);
    CHECK(sdk.GetInterfaceGeneration() == AfiInterfaceGeneration::kV1_1);
    for (int i = 0; i < 3; ++i) CHECK(sdk.SendVehicleInput(AfiVehicleInput{}) == kOk);
    CHECK(wait_for([&] { return sink.udp_frames() == 3; }));
    CHECK(sink.tcp_frames() == 0);
    sdk.Stop();
    PASS("sdk_sends_csii_over_udp_to_a_v110_sensor");
}

TEST(csii_transport_setting_overrides_the_generation) {
    FakeRadar radar([](int, const Request&) { return Reply{}; }, true);
    CsiiSink sink;
    AfiSdkConfig config = csii_only_config(radar.port(), sink.port());
    config.csii_transport = "udp";
    AfiSdk sdk;
    CHECK(sdk.Init(config) == kOk);
    CHECK(sdk.SendVehicleInput(AfiVehicleInput{}) == kOk);
    CHECK(wait_for([&] { return sink.udp_frames() == 1; }));
    CHECK(sink.tcp_frames() == 0);
    sdk.Stop();
    config.csii_transport = "sctp";
    AfiSdk bad;
    CHECK(bad.Init(config) == kInvalidParam);
    PASS("csii_transport_setting_overrides_the_generation");
}

TEST_MAIN("Config API Transport Tests")
