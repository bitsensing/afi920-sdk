/**
 * @file afi_config.h
 * @brief AFI920 SDK configuration
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstdint>
#include <string>

namespace afi {

/**
 * @brief SDK configuration for a single sensor connection
 *
 * For multi-sensor usage, create one AfiSdk instance per sensor
 * with different config (sensor_ip, ports).
 */
struct AfiSdkConfig {
    /// Sensor IP address
    std::string sensor_ip = "192.168.10.150";

    /// TCP port for SOME/IP Config API
    uint16_t config_port = 30500;

    /// Port for RDI (Radar Detection Interface)
    uint16_t rdi_port = 30509;

    /// Port for SHII (Sensor Health Information Interface)
    uint16_t shi_port = 30510;

    /// Port for SPI (Sensor Performance Interface)
    uint16_t spi_port = 30511;

    /// Port for CSII vehicle input (Host → Radar; UDP on v1.1.0, TCP on v1.0.x)
    uint16_t csii_port = 30501;

    /// Default data stream transport protocol ("tcp" or "udp", default is TCP)
    std::string stream_transport = "tcp";

    /// Enable RDI data stream reception
    bool enable_rdi = true;

    /// Enable SHII data stream reception
    bool enable_shi = false;

    /// Enable SPI data stream reception
    bool enable_spi = false;

    /// Auto-connect Config API (TCP) on Init()
    bool enable_config_api = true;

    /// Verify the E2E CRC-32 on received data streams. A failed check (CRC,
    /// zero CRC, Length, Data ID) is logged as a warning and the frame is
    /// still delivered.
    bool validate_e2e = true;

    /// Drop frames that fail the E2E check (default: warn and deliver)
    bool e2e_strict = false;

    /// Socket receive buffer size (SO_RCVBUF) for both TCP and UDP
    uint32_t socket_buffer_size = 16 * 1024 * 1024;

    /// TCP connect timeout in milliseconds
    int connect_timeout_ms = 5000;

    /// Config API request timeout in milliseconds
    int request_timeout_ms = 5000;

    // v1.0.2 additions are appended to preserve the order and offsets of the
    // original public members for existing aggregate-style callers.

    /// Port for PMOI (Potentially Moving Object Interface)
    uint16_t pmoi_port = 30512;

    /// Optional per-stream transport overrides (empty = stream_transport)
    std::string rdi_transport;
    std::string shi_transport;
    std::string spi_transport;
    std::string pmoi_transport;

    /// Enable PMOI data stream reception. Interface v1.1.0 reserves PMOI and
    /// the radar does not send it; set this only when Discovery reports the
    /// endpoint (AfiSensorInfo::has_objects), taking pmoi_port from
    /// objects_port. An unavailable PMOI endpoint is logged and disables only
    /// PMOI; Init() still succeeds for the other streams.
    bool enable_pmoi = false;

    /// CSII transport: empty picks it from the sensor's interface generation
    /// (UDP on v1.1.0, TCP on v1.0.x, UDP while unknown); "udp" or "tcp"
    /// forces one. The generation is unknown only while the Config API is
    /// off and no stream frame has arrived.
    std::string csii_transport;
};

} // namespace afi
