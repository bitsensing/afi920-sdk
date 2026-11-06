/**
 * @file test_wire_payload.cpp
 * @brief Parser-independent callback must survive domain parser drift
 */
#include "afi/afi_sdk.h"
#include "bts_iso23150_e2e.h"
#include "protocol/someip.h"
#include "transport/platform.h"
#include "util/endian_util.h"
#include "legacy_vectors.h"
#include "test_common.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace afi;

static uint16_t test_port(uint16_t offset = 0) {
#ifdef _WIN32
    return static_cast<uint16_t>(41000 + (GetCurrentProcessId() % 900) + offset);
#else
    return static_cast<uint16_t>(41000 + (::getpid() % 900) + offset);
#endif
}

static std::vector<uint8_t> make_future_shi_frame() {
    // Current SHII fits comfortably inside the old 2 KiB typed receive buffer.
    // A larger future schema must still reach the payload callback byte-exact.
    std::vector<uint8_t> iso(4097, 0);
    iso[0] = 9;  // deliberately future interface major
    iso[3] = 0x0D;
    iso[4] = 1;
    iso[5] = 7;
    const uint64_t sensor_ts = 1234567890123ull;
    for (int i = 0; i < 8; ++i) {
        iso[6 + i] = static_cast<uint8_t>(sensor_ts >> (8 * i));
    }
    const uint32_t counter = 77;
    for (int i = 0; i < 4; ++i) {
        iso[14 + i] = static_cast<uint8_t>(counter >> (8 * i));
    }
    for (size_t i = 32; i < iso.size(); ++i) {
        iso[i] = static_cast<uint8_t>((i * 19 + 0xA5) & 0xFF);
    }

    std::vector<uint8_t> frame(kSomeipHeaderSize + kE2eHeaderSize + iso.size(), 0);
    detail::pack_be32(frame.data(), 0x60008003u);
    detail::pack_be32(frame.data() + 4,
                      static_cast<uint32_t>(8 + kE2eHeaderSize + iso.size()));
    detail::pack_be32(frame.data() + 8, 0x00010001u);
    frame[12] = kSomeipProtocolVersion;
    frame[13] = kSomeipInterfaceVersion;
    frame[14] = kSomeipMsgTypeNotify;
    frame[15] = kSomeipReturnOk;

    uint8_t* e2e = frame.data() + kSomeipHeaderSize;
    bts_e2e_build_header(e2e, BTS_E2E_DATA_ID_SHII, 12,
                         static_cast<uint32_t>(iso.size()));
    std::memcpy(e2e + kE2eHeaderSize, iso.data(), iso.size());
    bts_e2e_apply_crc(e2e, e2e + kE2eHeaderSize, iso.size());
    return frame;
}

static std::vector<uint8_t> make_future_protected_payload(
        uint32_t data_id, uint32_t e2e_counter, size_t iso_size,
        uint64_t sensor_ts, uint32_t message_counter) {
    std::vector<uint8_t> out(kE2eHeaderSize + iso_size, 0);
    uint8_t* iso = out.data() + kE2eHeaderSize;
    iso[0] = 99;  // deliberately outside the typed parser's current schema
    iso[3] = 0x0D;
    iso[4] = 1;
    iso[5] = 7;
    for (int i = 0; i < 8; ++i) {
        iso[6 + i] = static_cast<uint8_t>(sensor_ts >> (8 * i));
    }
    for (int i = 0; i < 4; ++i) {
        iso[14 + i] = static_cast<uint8_t>(message_counter >> (8 * i));
    }
    for (size_t i = 18; i < iso_size; ++i) {
        iso[i] = static_cast<uint8_t>((i * 37 + data_id) & 0xFF);
    }
    bts_e2e_build_header(out.data(), data_id, e2e_counter,
                         static_cast<uint32_t>(iso_size));
    bts_e2e_apply_crc(out.data(), iso, iso_size);
    return out;
}

static bool send_tp_payload(uint16_t port, uint16_t event_id,
                            uint16_t session_id,
                            const std::vector<uint8_t>& payload) {
    socket_t sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender == kInvalidSocket) return false;
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool ok = true;
    for (size_t offset = 0; offset < payload.size();
         offset += kSomeipTpChunkSize) {
        const size_t chunk = (std::min)(kSomeipTpChunkSize,
                                        payload.size() - offset);
        const bool more = offset + chunk < payload.size();
        std::vector<uint8_t> frame(
            kSomeipHeaderSize + kSomeipTpHeaderSize + chunk, 0);
        detail::pack_be32(frame.data(),
                          (static_cast<uint32_t>(kSomeipServiceId) << 16) |
                          event_id);
        detail::pack_be32(frame.data() + 4,
                          static_cast<uint32_t>(8 + kSomeipTpHeaderSize + chunk));
        detail::pack_be32(frame.data() + 8,
                          0x00010000u | session_id);
        frame[12] = kSomeipProtocolVersion;
        frame[13] = kSomeipInterfaceVersion;
        frame[14] = kSomeipMsgTypeNotify | kSomeipMsgTypeTpFlag;
        frame[15] = kSomeipReturnOk;
        const uint32_t tp_word =
            static_cast<uint32_t>((offset / 16) << 4) | (more ? 1u : 0u);
        detail::pack_be32(frame.data() + kSomeipHeaderSize, tp_word);
        std::memcpy(frame.data() + kSomeipHeaderSize + kSomeipTpHeaderSize,
                    payload.data() + offset, chunk);
        const int sent = ::sendto(
            sender, reinterpret_cast<const char*>(frame.data()),
            static_cast<int>(frame.size()), 0,
            reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
        if (sent != static_cast<int>(frame.size())) {
            ok = false;
            break;
        }
        // Keep large future-schema tests deterministic even when the host caps
        // SO_RCVBUF below the SDK's requested value.
        if (((offset / kSomeipTpChunkSize) & 31u) == 31u) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    platform_close_socket(sender);
    return ok;
}


TEST(callback_precedes_domain_parser) {
    AfiSdkConfig config;
    config.sensor_ip = "127.0.0.1";  // loopback senders
    config.enable_config_api = false;
    config.enable_rdi = false;
    config.enable_shi = true;
    config.enable_spi = false;
    config.shi_transport = "udp";
    config.shi_port = test_port();
    config.validate_e2e = true;
    config.e2e_strict = true;

    std::atomic<bool> received{false};
    std::atomic<uint32_t> typed_count{0};
    AfiWirePayloadFrame captured;

    AfiSdk sdk;
    sdk.RegWirePayloadCallback([&](const AfiWirePayloadFrame& frame) {
        captured = frame;
        received.store(true);
    });
    sdk.RegRecvCallback([&](const AfiShiMessage&) {
        typed_count.fetch_add(1);
    });

    CHECK(sdk.Init(config) == kOk);
    CHECK(sdk.Start() == kOk);

    const auto frame = make_future_shi_frame();
    socket_t sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(sender != kInvalidSocket);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(config.shi_port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int sent = ::sendto(sender,
                              reinterpret_cast<const char*>(frame.data()),
                              static_cast<int>(frame.size()), 0,
                              reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    platform_close_socket(sender);
    CHECK(sent == static_cast<int>(frame.size()));

    for (int i = 0; i < 100 && !received.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(sdk.Stop() == kOk);

    CHECK(received.load());
    CHECK(typed_count.load() == 0);
    CHECK(captured.stream == AfiStreamKind::kHealth);
    CHECK(captured.sensor_timestamp_ns == 1234567890123ull);
    CHECK(captured.message_counter == 77);
    CHECK(captured.e2e_counter == 12);
    CHECK(captured.e2e_checked);
    CHECK(captured.e2e_valid);
    CHECK(captured.payload.size() == frame.size() - kSomeipHeaderSize);
    CHECK(std::memcmp(captured.payload.data(),
                      frame.data() + kSomeipHeaderSize,
                      captured.payload.size()) == 0);
    PASS("callback_precedes_domain_parser");
}

TEST(tp_reassembly_preserves_rdi_payload) {
    AfiSdkConfig config;
    config.sensor_ip = "127.0.0.1";  // loopback senders
    config.enable_config_api = false;
    config.enable_rdi = true;
    config.enable_shi = false;
    config.enable_spi = false;
    config.rdi_transport = "udp";
    config.rdi_port = test_port(10);
    config.validate_e2e = true;
    config.e2e_strict = true;

    // The payload intentionally exceeds the compiled typed-schema maximum.
    // TP reassembly is a transport concern and must not use those maxima as a
    // capture limit.
    const auto rdi_payload = make_future_protected_payload(
        BTS_E2E_DATA_ID_RDI, 101, BTS_RDI_MAX_PAYLOAD_SIZE + 64u,
        1111111111111ull, 501);
    AfiWirePayloadFrame captured;
    std::atomic<bool> received{false};
    std::atomic<uint32_t> typed_rdi{0};

    AfiSdk sdk;
    sdk.RegWirePayloadCallback([&](const AfiWirePayloadFrame& frame) {
        if (frame.stream == AfiStreamKind::kDetection) {
            captured = frame;
            received.store(true);
        }
    });
    sdk.RegRecvCallback([&](const AfiRdiFrame&) { typed_rdi.fetch_add(1); });

    CHECK(sdk.Init(config) == kOk);
    CHECK(sdk.Start() == kOk);
    CHECK(send_tp_payload(config.rdi_port, kEventRdi, 71, rdi_payload));
    for (int i = 0; i < 200 && !received.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(sdk.Stop() == kOk);

    CHECK(received.load());
    CHECK(typed_rdi.load() == 0);
    CHECK(captured.payload == rdi_payload);
    CHECK(captured.sensor_timestamp_ns == 1111111111111ull);
    CHECK(captured.message_counter == 501);
    CHECK(captured.e2e_counter == 101);
    CHECK(captured.e2e_valid);
    PASS("tp_reassembly_preserves_rdi_payload");
}

TEST(e2e_failure_is_reported_not_dropped) {
    AfiSdkConfig config;
    config.sensor_ip = "127.0.0.1";  // loopback senders
    config.enable_config_api = false;
    config.enable_rdi = false;
    config.enable_shi = true;
    config.enable_spi = false;
    config.shi_transport = "udp";
    config.shi_port = test_port(20);
    config.validate_e2e = true;  // e2e_strict stays at its default (false)

    auto frame = make_future_shi_frame();
    frame[kSomeipHeaderSize + kE2eHeaderSize + 40] ^= 0x5A;  // corrupt body

    std::atomic<bool> received{false};
    AfiWirePayloadFrame captured;
    AfiSdk sdk;
    sdk.RegWirePayloadCallback([&](const AfiWirePayloadFrame& value) {
        captured = value;
        received.store(true);
    });
    CHECK(sdk.Init(config) == kOk);
    CHECK(sdk.Start() == kOk);

    socket_t sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(sender != kInvalidSocket);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(config.shi_port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::sendto(sender, reinterpret_cast<const char*>(frame.data()),
             static_cast<int>(frame.size()), 0,
             reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    platform_close_socket(sender);

    for (int i = 0; i < 100 && !received.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(sdk.Stop() == kOk);

    CHECK(received.load());
    CHECK(captured.e2e_checked);
    CHECK(!captured.e2e_valid);
    CHECK(captured.e2e_counter == 12);
    CHECK(captured.payload.size() == frame.size() - kSomeipHeaderSize);
    PASS("e2e_failure_is_reported_not_dropped");
}

// One unsegmented stream frame: SOME/IP + E2E + ISO payload.
static std::vector<uint8_t> make_stream_frame(uint32_t message_id, uint32_t data_id,
                                              uint32_t e2e_counter,
                                              const std::vector<uint8_t>& iso,
                                              bool corrupt) {
    std::vector<uint8_t> frame(kSomeipHeaderSize + kE2eHeaderSize + iso.size(), 0);
    detail::pack_be32(frame.data(), message_id);
    detail::pack_be32(frame.data() + 4,
                      static_cast<uint32_t>(8 + kE2eHeaderSize + iso.size()));
    detail::pack_be32(frame.data() + 8, 0x00010001u);
    frame[12] = kSomeipProtocolVersion;
    frame[13] = kSomeipInterfaceVersion;
    frame[14] = kSomeipMsgTypeNotify;
    frame[15] = kSomeipReturnOk;
    uint8_t* e2e = frame.data() + kSomeipHeaderSize;
    bts_e2e_build_header(e2e, data_id, e2e_counter, static_cast<uint32_t>(iso.size()));
    std::memcpy(e2e + kE2eHeaderSize, iso.data(), iso.size());
    bts_e2e_apply_crc(e2e, e2e + kE2eHeaderSize, iso.size());
    if (corrupt) e2e[kE2eHeaderSize + 7] ^= 0x5A;  // timestamp byte: still parses
    return frame;
}

static std::vector<uint8_t> iso_header(uint8_t interface_id, uint32_t message_counter) {
    std::vector<uint8_t> h(24, 0);
    h[0] = 1; h[1] = 1; h[2] = 0;
    h[3] = interface_id;
    h[4] = 1;
    h[5] = 1;
    for (int i = 0; i < 4; ++i) h[14 + i] = static_cast<uint8_t>(message_counter >> (8 * i));
    return h;
}

static void send_udp(uint16_t port, const std::vector<uint8_t>& frame) {
    socket_t sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::sendto(sender, reinterpret_cast<const char*>(frame.data()),
             static_cast<int>(frame.size()), 0,
             reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    platform_close_socket(sender);
}

TEST(typed_callbacks_carry_the_e2e_verdict_and_loss_skips_failed_frames) {
    AfiSdkConfig config;
    config.sensor_ip = "127.0.0.1";  // loopback senders
    config.enable_config_api = false;
    config.enable_rdi = true;
    config.enable_shi = true;
    config.enable_spi = false;
    config.rdi_transport = "udp";
    config.shi_transport = "udp";
    config.rdi_port = test_port(40);
    config.shi_port = test_port(41);
    config.validate_e2e = true;  // e2e_strict off: failed frames are delivered

    // SHII: valid shape with one entry per list (shi.ksy).
    std::vector<uint8_t> shi = iso_header(0x0D, 5);
    const uint8_t shi_body[] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0};
    shi.insert(shi.end(), shi_body, shi_body + sizeof(shi_body));
    // RDI: header only, zero detections.
    std::vector<uint8_t> rdi = iso_header(0x08, 9);
    rdi.resize(36, 0);

    std::mutex mu;
    std::vector<AfiStreamMeta> shi_meta;
    std::vector<std::pair<uint32_t, bool>> rdi_seen;
    std::atomic<uint32_t> lost{0};
    AfiSdk sdk;
    sdk.RegRecvCallback([&](const AfiShiMessage&, const AfiStreamMeta& m) {
        std::lock_guard<std::mutex> lock(mu);
        shi_meta.push_back(m);
    });
    sdk.RegRecvCallback([&](const AfiRdiFrame& f) {
        std::lock_guard<std::mutex> lock(mu);
        rdi_seen.emplace_back(f.e2e_counter, f.e2e_valid);
    });
    sdk.RegRecvCallback([&](uint32_t, uint32_t l) { lost.store(l); });
    CHECK(sdk.Init(config) == kOk);
    CHECK(sdk.Start() == kOk);

    send_udp(config.shi_port, make_stream_frame(0x60008003u, BTS_E2E_DATA_ID_SHII, 3, shi, false));
    send_udp(config.shi_port, make_stream_frame(0x60008003u, BTS_E2E_DATA_ID_SHII, 4, shi, true));
    // RDI counters 1, (corrupt 50), 2: the corrupt Counter must not open a gap.
    send_udp(config.rdi_port, make_stream_frame(0x60008002u, BTS_E2E_DATA_ID_RDI, 1, rdi, false));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    send_udp(config.rdi_port, make_stream_frame(0x60008002u, BTS_E2E_DATA_ID_RDI, 50, rdi, true));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    send_udp(config.rdi_port, make_stream_frame(0x60008002u, BTS_E2E_DATA_ID_RDI, 2, rdi, false));

    for (int i = 0; i < 200; ++i) {
        {
            std::lock_guard<std::mutex> lock(mu);
            if (shi_meta.size() >= 2 && rdi_seen.size() >= 3) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(sdk.Stop() == kOk);

    std::lock_guard<std::mutex> lock(mu);
    CHECK(shi_meta.size() == 2);
    CHECK(shi_meta[0].e2e_checked && shi_meta[0].e2e_valid && shi_meta[0].e2e_counter == 3);
    CHECK(!shi_meta[1].e2e_valid && shi_meta[1].e2e_counter == 4);
    CHECK(shi_meta[0].recv_timestamp_ns != 0);
    CHECK(rdi_seen.size() == 3);
    {
        AfiRdiFrame original;
        original.e2e_counter = 7;
        original.e2e_checked = true;
        original.e2e_valid = true;
        AfiRdiFrame copied(original);
        AfiRdiFrame moved(std::move(copied));
        AfiRdiFrame assigned;
        assigned = moved;
        CHECK(assigned.e2e_counter == 7 && assigned.e2e_checked && assigned.e2e_valid);
    }
    CHECK(rdi_seen[0] == std::make_pair(1u, true));
    CHECK(rdi_seen[1] == std::make_pair(50u, false));
    CHECK(rdi_seen[2] == std::make_pair(2u, true));
    CHECK(lost.load() == 0);
    PASS("typed_callbacks_carry_the_e2e_verdict_and_loss_skips_failed_frames");
}

TEST(legacy_v1_0_frames_are_delivered_as_valid) {
    AfiSdkConfig config;
    config.sensor_ip = "127.0.0.1";  // loopback senders
    config.enable_config_api = false;
    config.enable_rdi = true;
    config.enable_shi = true;
    config.enable_spi = true;
    config.stream_transport = "udp";
    config.rdi_port = test_port(50);
    config.shi_port = test_port(51);
    config.spi_port = test_port(52);
    config.validate_e2e = true;

    std::mutex mu;
    std::vector<AfiRdiFrame> rdi;
    std::vector<AfiStreamMeta> shi, spi;
    std::vector<AfiInterfaceGeneration> wire;
    std::atomic<uint32_t> lost{0};
    AfiSdk sdk;
    sdk.RegRecvCallback([&](const AfiRdiFrame& f) {
        std::lock_guard<std::mutex> lock(mu);
        rdi.push_back(f);
    });
    sdk.RegRecvCallback([&](const AfiShiMessage&, const AfiStreamMeta& m) {
        std::lock_guard<std::mutex> lock(mu);
        shi.push_back(m);
    });
    sdk.RegRecvCallback([&](const AfiSpiMessage&, const AfiStreamMeta& m) {
        std::lock_guard<std::mutex> lock(mu);
        spi.push_back(m);
    });
    sdk.RegWirePayloadCallback([&](const AfiWirePayloadFrame& f) {
        std::lock_guard<std::mutex> lock(mu);
        if (f.e2e_valid) wire.push_back(f.generation);
    });
    sdk.RegRecvCallback([&](uint32_t, uint32_t l) { lost.store(l); });
    CHECK(sdk.Init(config) == kOk);
    CHECK(sdk.GetInterfaceGeneration() == AfiInterfaceGeneration::kUnknown);
    CHECK(sdk.Start() == kOk);

    const auto rdi_frames = legacy_frames("rdi_3frames.bin");
    const auto shi_frames = legacy_frames("shii_3frames.bin");
    const auto spi_frames = legacy_frames("spi_3frames.bin");
    CHECK(rdi_frames.size() == 3 && shi_frames.size() == 3 && spi_frames.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        send_udp(config.rdi_port, rdi_frames[i]);
        send_udp(config.shi_port, shi_frames[i]);
        send_udp(config.spi_port, spi_frames[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    for (int i = 0; i < 200; ++i) {
        {
            std::lock_guard<std::mutex> lock(mu);
            if (rdi.size() >= 3 && shi.size() >= 3 && spi.size() >= 3) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(sdk.Stop() == kOk);

    std::lock_guard<std::mutex> lock(mu);
    CHECK(rdi.size() == 3 && shi.size() == 3 && spi.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(rdi[i].e2e_valid && rdi[i].e2e_counter == i + 1);
        CHECK(rdi[i].generation == AfiInterfaceGeneration::kV1_0);
        CHECK(rdi[i].message.interface_version.minor == 0);
        CHECK(shi[i].e2e_valid && shi[i].generation == AfiInterfaceGeneration::kV1_0);
        CHECK(spi[i].e2e_valid && spi[i].generation == AfiInterfaceGeneration::kV1_0);
    }
    CHECK(wire.size() == 9);
    for (AfiInterfaceGeneration g : wire) CHECK(g == AfiInterfaceGeneration::kV1_0);
    CHECK(lost.load() == 0);
    // With the Config API off, the generation comes from the stream frames.
    CHECK(sdk.GetInterfaceGeneration() == AfiInterfaceGeneration::kV1_0);
    PASS("legacy_v1_0_frames_are_delivered_as_valid");
}

TEST(udp_streams_drop_other_sources) {
    AfiSdkConfig config;
    config.sensor_ip = "127.0.0.2";  // the frames below come from 127.0.0.1
    config.enable_config_api = false;
    config.enable_rdi = false;
    config.enable_shi = true;
    config.enable_spi = false;
    config.shi_transport = "udp";
    config.shi_port = test_port(45);
    config.validate_e2e = true;

    std::vector<uint8_t> shi = iso_header(0x0D, 5);
    const uint8_t shi_body[] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0};
    shi.insert(shi.end(), shi_body, shi_body + sizeof(shi_body));

    std::atomic<int> wire{0};
    AfiSdk sdk;
    sdk.RegWirePayloadCallback([&](const AfiWirePayloadFrame&) { wire.fetch_add(1); });
    CHECK(sdk.Init(config) == kOk);
    CHECK(sdk.Start() == kOk);
    send_udp(config.shi_port, make_stream_frame(0x60008003u, BTS_E2E_DATA_ID_SHII, 3, shi, false));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(sdk.Stop() == kOk);
    CHECK(wire.load() == 0);
    PASS("udp_streams_drop_other_sources");
}


TEST_MAIN("Wire Payload Callback Tests")
