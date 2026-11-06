/**
 * @file test_config_e2e.cpp
 * @brief Config API request/response E2E framing over a loopback TCP peer
 *
 * Request and response both carry the 20B E2E header after the SOME/IP
 * header; Data ID is 0x6000 << 16 | Method ID (Config API Overview §1,
 * 10_E2E_Protection.md §8.2). A failed response check is a warning only.
 */
#include "config_api/config_client.h"
#include "protocol/e2e.h"
#include "transport/platform.h"
#include "transport/tcp_client.h"
#include "util/endian_util.h"
#include "bts_iso23150_e2e.h"
#include "test_common.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace afi;

static uint16_t test_port() {
#ifdef _WIN32
    return static_cast<uint16_t>(43000 + (GetCurrentProcessId() % 900));
#else
    return static_cast<uint16_t>(43000 + (::getpid() % 900));
#endif
}

static bool recv_exact(socket_t s, uint8_t* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        const int r = ::recv(s, reinterpret_cast<char*>(buf + got),
                             static_cast<int>(n - got), 0);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

static bool send_all(socket_t s, const std::vector<uint8_t>& buf) {
    size_t sent = 0;
    while (sent < buf.size()) {
        const int r = ::send(s, reinterpret_cast<const char*>(buf.data() + sent),
                             static_cast<int>(buf.size() - sent), 0);
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

struct RequestSeen {
    uint32_t message_id = 0;
    uint32_t someip_length = 0;
    uint16_t session = 0;
    E2eStatus e2e = E2eStatus::kTooShort;
    E2eHeader hdr;
    std::string body;
};

static std::vector<uint8_t> make_response(uint32_t message_id, uint16_t session,
                                          uint32_t counter, const std::string& json,
                                          bool corrupt_crc) {
    std::vector<uint8_t> frame(16 + 20 + json.size(), 0);
    detail::pack_be32(frame.data(), message_id);
    detail::pack_be32(frame.data() + 4, static_cast<uint32_t>(8 + 20 + json.size()));
    detail::pack_be32(frame.data() + 8, 0x00010000u | session);
    frame[12] = 0x01;
    frame[13] = 0x01;
    frame[14] = 0x80;
    frame[15] = 0x00;
    std::memcpy(frame.data() + 36, json.data(), json.size());
    e2e_write_header(frame.data() + 16, message_id, counter,
                     frame.data() + 36, json.size());
    if (corrupt_crc) frame[16 + 7] ^= 0xFF;
    return frame;
}

TEST(config_request_and_response_carry_e2e) {
    CHECK(platform_init_sockets());
    const uint16_t port = test_port();
    socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(listener != kInvalidSocket);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    CHECK(::listen(listener, 1) == 0);

    const std::string reply =
        R"({"return_code":0,"return_name":"SUCCESS","data":{"serial_number":"SN-1",)"
        R"("model_name":"AFI920","fw_hash":"9f99a7cd"}})";
    RequestSeen seen[2];
    std::atomic<bool> server_ok{false};

    std::thread server([&]() {
        socket_t peer = ::accept(listener, nullptr, nullptr);
        if (peer == kInvalidSocket) return;
        bool ok = true;
        // Request 0 is the GetDeviceInfo probe that opens the connection.
        RequestSeen probe;
        for (int i = -1; i < 2 && ok; ++i) {
            uint8_t hdr[16];
            ok = recv_exact(peer, hdr, sizeof(hdr));
            if (!ok) break;
            RequestSeen& r = i < 0 ? probe : seen[i];
            r.message_id = detail::unpack_be32(hdr);
            r.someip_length = detail::unpack_be32(hdr + 4);
            r.session = detail::unpack_be16(hdr + 10);
            std::vector<uint8_t> rest(r.someip_length - 8);
            ok = recv_exact(peer, rest.data(), rest.size());
            if (!ok) break;
            r.e2e = e2e_validate(rest.data(), rest.size(), r.hdr, r.message_id);
            if (rest.size() >= 20) r.body.assign(rest.begin() + 20, rest.end());
            // Second response carries a broken CRC: the client must still
            // return the body.
            ok = send_all(peer, make_response(r.message_id, r.session,
                                              static_cast<uint32_t>(i + 2),
                                              reply, i == 1));
        }
        server_ok.store(ok);
        platform_close_socket(peer);
    });

    TcpClient tcp;
    CHECK(tcp.Connect("127.0.0.1", port, 2000) == 0);
    ConfigClient client(tcp);
    AfiDeviceInfo info;
    const int rc1 = client.GetDeviceInfo(info);
    AfiDeviceInfo info2;
    const int rc2 = client.GetDeviceInfo(info2);
    tcp.Disconnect();
    server.join();
    platform_close_socket(listener);
    platform_cleanup_sockets();

    CHECK(server_ok.load());
    CHECK(rc1 == kOk);
    CHECK(info.serial_number == "SN-1");
    CHECK(info.fw_hash == "9f99a7cd");
    CHECK(rc2 == kOk);
    CHECK(info2.model_name == "AFI920");

    // Request framing: Length = 8 + 20 + JSON, body at frame offset 36.
    CHECK(seen[0].message_id == 0x60000010u);
    CHECK(seen[0].someip_length == 8 + 20 + seen[0].body.size());
    CHECK(seen[0].e2e == E2eStatus::kOk);
    CHECK(seen[0].hdr.data_id == 0x60000010u);
    CHECK(seen[0].hdr.length == seen[0].body.size());
    CHECK(seen[0].body == "{}");
    CHECK(seen[1].e2e == E2eStatus::kOk);
    CHECK(seen[1].hdr.counter == seen[0].hdr.counter + 1);
    CHECK(seen[1].session == static_cast<uint16_t>(seen[0].session + 1));
    PASS("config_request_and_response_carry_e2e");
}

TEST_MAIN("Config API E2E Tests")
