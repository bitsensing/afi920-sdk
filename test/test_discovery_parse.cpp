/**
 * @file test_discovery_parse.cpp
 * @brief Unit test: Discovery response parsing and the interface generation
 *
 * driver_hash exists only in interface v1.0.x responses, the m72_* keys only
 * in v1.1.0 ones. The v1.0.x response is the one a firmware 3.0.0 sensor sent.
 */
#include "discovery/discovery.h"
#include "legacy_vectors.h"
#include "test_common.h"

#include <cstring>
#include <string>

using namespace afi;

static int parse_json(const std::string& json, AfiSensorInfo& out) {
    return Discovery::ParseResponse(reinterpret_cast<const uint8_t*>(json.data()),
                                    json.size(), out);
}

TEST(legacy_v1_0_response) {
    const std::vector<uint8_t> rsp = legacy_file("discovery_rsp.bin");
    CHECK(rsp.size() > 16);
    AfiSensorInfo info;
    CHECK(Discovery::ParseResponse(rsp.data() + 16, rsp.size() - 16, info) == 0);
    CHECK(info.generation == AfiInterfaceGeneration::kV1_0);
    CHECK(info.software_version == "3.0.0");
    CHECK(info.detection_protocol == "tcp" && info.detection_port == 30509);
    CHECK(info.ip == "192.168.10.210");
    PASS("legacy_v1_0_response");
}

TEST(v110_response_has_m72_keys) {
    AfiSensorInfo info;
    CHECK(parse_json(R"({"return_code":0,"data":{"software_version":"0.0.0",)"
                     R"("fw_hash":"f6c38c30","m72_ptp_state":"SLAVE"}})", info) == 0);
    CHECK(info.generation == AfiInterfaceGeneration::kV1_1);
    PASS("v110_response_has_m72_keys");
}

TEST(generation_unknown_without_either_key) {
    AfiSensorInfo info;
    CHECK(parse_json(R"({"return_code":0,"data":{"software_version":"0.0.0"}})", info) == 0);
    CHECK(info.generation == AfiInterfaceGeneration::kUnknown);
    PASS("generation_unknown_without_either_key");
}

TEST_MAIN("Discovery Parse Tests")
