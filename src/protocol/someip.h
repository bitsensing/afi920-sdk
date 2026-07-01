/**
 * @file someip.h
 * @brief SOME/IP header serialization/deserialization
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstdint>
#include <cstddef>

namespace afi {

// ─── SOME/IP Constants ───

static constexpr uint16_t kSomeipServiceId        = 0x6000;
static constexpr size_t   kSomeipHeaderSize        = 16;
static constexpr size_t   kSomeipTpHeaderSize      = 4;
static constexpr size_t   kSomeipTpChunkSize       = 1392;
static constexpr uint8_t  kSomeipMsgTypeRequest    = 0x00;
static constexpr uint8_t  kSomeipMsgTypeNotify     = 0x02;
static constexpr uint8_t  kSomeipMsgTypeResponse   = 0x80;
static constexpr uint8_t  kSomeipMsgTypeError      = 0x81;
static constexpr uint8_t  kSomeipMsgTypeTpFlag     = 0x20;
static constexpr uint8_t  kSomeipReturnOk          = 0x00;

// ─── Event IDs ───

static constexpr uint16_t kEventCsii    = 0x8001;  // Host → Radar (TCP only)
static constexpr uint16_t kEventRdi     = 0x8002;
static constexpr uint16_t kEventShii    = 0x8003;
static constexpr uint16_t kEventSpi     = 0x8004;

// ─── E2E Profile 7 (RDI/SHII/SPI/CSII payloads) ───

static constexpr size_t   kE2eHeaderSize = 20;

// ─── Method IDs (Config API, protocol v3.0 registry) ───

enum class MethodId : uint16_t {
    // Category 1: Device Info & Status (0x0010~0x0016)
    kGetDeviceInfo         = 0x0010,
    kGetSensorStatus       = 0x0011,
    kGetDiagnosticInfo     = 0x0012,
    kClearFaultLog         = 0x0013,
    kRestart               = 0x0014,
    kResetAllSettings      = 0x0015,
    kGetCommandHistoryInfo = 0x0016,

    // Category 2: Network Configuration (0x0020~0x0021)
    kGetNetworkConfig      = 0x0020,
    kSetNetworkConfig      = 0x0021,

    // Category 3: Range Mode (0x0030~0x0031)
    kGetRangeMode          = 0x0030,
    kSetRangeMode          = 0x0031,

    // Category 4: Detection Configuration (0x0040~0x0045)
    kGetDetectionThreshold = 0x0040,
    kSetDetectionThreshold = 0x0041,
    kGetDetectionFilters   = 0x0042,
    kSetDetectionFilters   = 0x0043,
    kGetDetectionDensity   = 0x0044,
    kSetDetectionDensity   = 0x0045,

    // Category 5: Time Synchronization (0x0050~0x0051)
    kGetPtpConfig          = 0x0050,
    kSetPtpConfig          = 0x0051,

    // Category 7: Mounting Info (0x0070~0x0075)
    kGetMountingPosition   = 0x0070,
    kSetMountingPosition   = 0x0071,
    kGetRotation           = 0x0072,
    kSetRotation           = 0x0073,
    kGetSensorPosition     = 0x0074,
    kSetSensorPosition     = 0x0075,

    // Category 9: Config Persistence (0x0090~0x0092)
    kSaveConfig            = 0x0090,
    kGetConfigStatus       = 0x0091,
    kResetConfigDefaults   = 0x0092,

    // Discovery (UDP 30520, separate channel from Config API)
    kDiscoverRequest       = 0x00C0,
    kDiscoverResponse      = 0x00C1,
};

// ─── Transport Mode ───

enum class SomeipTransport : uint8_t {
    kUdp = 0,  // TP auto-applied for payload > 1392B
    kTcp = 1,  // Always single SOME/IP frame (no TP)
};

// ─── Parsed SOME/IP Header ───

struct SomeipHeader {
    uint16_t service_id;
    uint16_t method_id;
    uint32_t length;
    uint16_t client_id;
    uint16_t session_id;
    uint8_t  protocol_version;
    uint8_t  interface_version;
    uint8_t  message_type;
    uint8_t  return_code;
};

/**
 * @brief Deserialize SOME/IP header from buffer
 * @param buf Input buffer (at least 16 bytes)
 * @param len Buffer length
 * @param[out] out Parsed header
 * @return 0 on success, negative on error
 */
int someip_deserialize(const uint8_t* buf, size_t len, SomeipHeader& out);

/**
 * @brief Check if message type has TP flag set
 */
inline bool someip_is_tp(uint8_t message_type) {
    return (message_type & kSomeipMsgTypeTpFlag) != 0;
}

/**
 * @brief Get payload size from SOME/IP header
 * @param hdr Parsed header
 * @param has_tp Whether TP header is present
 * @return Payload size in bytes (excluding SOME/IP and TP headers)
 */
inline uint32_t someip_payload_size(const SomeipHeader& hdr, bool has_tp) {
    uint32_t overhead = 8 + (has_tp ? kSomeipTpHeaderSize : 0);
    return (hdr.length > overhead) ? (hdr.length - overhead) : 0;
}

// ─── TP Header ───

struct SomeipTpHeader {
    uint32_t offset_bytes;
    bool     more_segments;
};

/**
 * @brief Deserialize SOME/IP-TP header
 * @param buf 4 bytes after SOME/IP header
 * @param[out] out Parsed TP header
 */
void someip_tp_deserialize(const uint8_t* buf, SomeipTpHeader& out);

} // namespace afi
