/**
 * @file discovery.h
 * @brief AFI920 sensor discovery via UDP broadcast
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace afi {

class Discovery {
public:
    /**
     * @brief Discover AFI920 sensors on the network
     *
     * Sends SOME/IP Discovery request (0x00C0) via UDP broadcast
     * and collects responses (0x00C1) within timeout.
     *
     * @param timeout_ms Timeout in milliseconds
     * @param port Discovery port (default 30520)
     * @param bind_ip Local IP to bind for broadcast (empty = INADDR_ANY)
     * @return Vector of discovered sensors
     */
    static std::vector<AfiSensorInfo> Scan(int timeout_ms = 3000,
                                            uint16_t port = 30520,
                                            const std::string& bind_ip = "");

private:
    static int ParseResponse(const uint8_t* data, size_t len, AfiSensorInfo& out);
};

} // namespace afi
