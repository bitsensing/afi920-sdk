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

    /// Port for CSII vehicle input (Host → Radar, TCP only)
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

    /// Verify the E2E Profile 7 CRC-64 on received data streams
    bool validate_e2e = true;

    /// Drop frames whose E2E CRC does not match (otherwise warn and deliver)
    bool e2e_strict = false;

    /// Socket receive buffer size (SO_RCVBUF) for both TCP and UDP
    uint32_t socket_buffer_size = 16 * 1024 * 1024;

    /// TCP connect timeout in milliseconds
    int connect_timeout_ms = 5000;

    /// Config API request timeout in milliseconds
    int request_timeout_ms = 5000;
};

} // namespace afi
