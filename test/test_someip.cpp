/**
 * @file test_someip.cpp
 * @brief Unit test: SOME/IP header parsing + interface v1.1.0 ID registry
 */
#include "protocol/someip.h"
#include "afi/afi_sdk.h"
#include "util/endian_util.h"
#include "test_common.h"
#include <cstring>

using namespace afi;

TEST(deserialize_valid_header) {
    uint8_t buf[16];
    detail::pack_be32(buf + 0, 0x60008002);  // service 0x6000, RDI event
    detail::pack_be32(buf + 4, 108);          // length
    detail::pack_be32(buf + 8, 0x00010042);   // client 1, session 0x42
    buf[12] = 0x01;
    buf[13] = 0x01;
    buf[14] = 0x02;  // NOTIFICATION
    buf[15] = 0x00;

    SomeipHeader hdr;
    CHECK(someip_deserialize(buf, 16, hdr) == 0);
    CHECK(hdr.service_id == 0x6000);
    CHECK(hdr.method_id == 0x8002);
    CHECK(hdr.length == 108);
    CHECK(hdr.client_id == 0x0001);
    CHECK(hdr.session_id == 0x0042);
    CHECK(hdr.message_type == 0x02);
    PASS("deserialize_valid_header");
}

TEST(short_buffer_rejected) {
    uint8_t buf[15] = {};
    SomeipHeader hdr;
    CHECK(someip_deserialize(buf, sizeof(buf), hdr) < 0);
    PASS("short_buffer_rejected");
}

TEST(tp_flag_detection) {
    CHECK(someip_is_tp(0x22));      // NOTIFICATION | TP
    CHECK(!someip_is_tp(0x02));
    CHECK(!someip_is_tp(0x80));
    PASS("tp_flag_detection");
}

TEST(tp_header_reserved_bits) {
    uint8_t valid[4] = {0x00, 0x00, 0x01, 0x01};  // offset=256B, more=true
    SomeipTpHeader tp{};
    someip_tp_deserialize(valid, tp);
    CHECK(tp.valid);
    CHECK(tp.offset_bytes == 256u);
    CHECK(tp.more_segments);

    uint8_t invalid[4] = {0x00, 0x00, 0x00, 0x02};
    someip_tp_deserialize(invalid, tp);
    CHECK(!tp.valid);
    PASS("tp_header_reserved_bits");
}

TEST(payload_size_calculation) {
    SomeipHeader hdr{};
    hdr.length = 8 + 100;
    CHECK(someip_payload_size(hdr, false) == 100);
    CHECK(someip_payload_size(hdr, true) == 96);  // minus 4B TP header
    hdr.length = 4;
    CHECK(someip_payload_size(hdr, false) == 0);
    PASS("payload_size_calculation");
}

TEST(event_ids_match_registry) {
    CHECK(kEventCsii == 0x8001);
    CHECK(kEventRdi  == 0x8002);
    CHECK(kEventShii == 0x8003);
    CHECK(kEventSpi  == 0x8004);
    CHECK(kEventPmoi == 0x8005);
    CHECK(kSomeipProtocolVersion == 0x01);
    CHECK(kSomeipInterfaceVersion == 0x01);
    CHECK(kE2eHeaderSize == 20u);
    PASS("event_ids_match_registry");
}

// Interface v1.1.0 Method ID registry (01_API_Overview §4). Reserved IDs
// (0x0013 always NOT_SUPPORTED, 0x0044/45, 0x0060/61, 0x0090, 0x00B0-B6)
// have no SDK entry.
TEST(method_ids_match_v110_registry) {
    CHECK(static_cast<uint16_t>(MethodId::kGetDeviceInfo) == 0x0010);
    CHECK(static_cast<uint16_t>(MethodId::kGetSensorStatus) == 0x0011);
    CHECK(static_cast<uint16_t>(MethodId::kGetDiagnosticInfo) == 0x0012);
    CHECK(static_cast<uint16_t>(MethodId::kRestart) == 0x0014);
    CHECK(static_cast<uint16_t>(MethodId::kResetAllSettings) == 0x0015);
    CHECK(static_cast<uint16_t>(MethodId::kGetCommandHistoryInfo) == 0x0016);

    CHECK(static_cast<uint16_t>(MethodId::kGetNetworkConfig) == 0x0020);
    CHECK(static_cast<uint16_t>(MethodId::kSetNetworkConfig) == 0x0021);

    CHECK(static_cast<uint16_t>(MethodId::kGetRangeMode) == 0x0030);
    CHECK(static_cast<uint16_t>(MethodId::kSetRangeMode) == 0x0031);

    CHECK(static_cast<uint16_t>(MethodId::kGetDetectionThreshold) == 0x0040);
    CHECK(static_cast<uint16_t>(MethodId::kSetDetectionThreshold) == 0x0041);
    CHECK(static_cast<uint16_t>(MethodId::kGetDetectionFilters) == 0x0042);
    CHECK(static_cast<uint16_t>(MethodId::kSetDetectionFilters) == 0x0043);

    CHECK(static_cast<uint16_t>(MethodId::kGetPtpConfig) == 0x0050);
    CHECK(static_cast<uint16_t>(MethodId::kSetPtpConfig) == 0x0051);

    CHECK(static_cast<uint16_t>(MethodId::kGetMountingPosition) == 0x0070);
    CHECK(static_cast<uint16_t>(MethodId::kSetSensorPosition) == 0x0075);

    CHECK(static_cast<uint16_t>(MethodId::kFirmwareUpdateStart) == 0x0080);
    CHECK(static_cast<uint16_t>(MethodId::kFirmwareDataChunk) == 0x0081);
    CHECK(static_cast<uint16_t>(MethodId::kFirmwareUpdateCommit) == 0x0082);
    CHECK(static_cast<uint16_t>(MethodId::kFirmwareUpdateStatus) == 0x0083);
    CHECK(static_cast<uint16_t>(MethodId::kFirmwareUpdateAbort) == 0x0084);
    CHECK(static_cast<uint16_t>(MethodId::kFirmwareSwitchSlot) == 0x0085);
    CHECK(static_cast<uint16_t>(MethodId::kFirmwareUpdateReboot) == 0x0086);
    CHECK(static_cast<uint16_t>(MethodId::kGetBootInfo) == 0x0087);

    CHECK(static_cast<uint16_t>(MethodId::kGetConfigStatus) == 0x0091);
    CHECK(static_cast<uint16_t>(MethodId::kResetConfigDefaults) == 0x0092);

    CHECK(static_cast<uint16_t>(MethodId::kDiscoverRequest) == 0x00C0);
    CHECK(static_cast<uint16_t>(MethodId::kDiscoverResponse) == 0x00C1);

    CHECK(static_cast<uint16_t>(MethodId::kGetMode) == 0x00E0);
    CHECK(static_cast<uint16_t>(MethodId::kSetMode) == 0x00E1);
    PASS("method_ids_match_v110_registry");
}

TEST_MAIN("SOME/IP Tests (interface v1.1.0)")
