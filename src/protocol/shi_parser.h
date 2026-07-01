/**
 * @file shi_parser.h
 * @brief SHII (Sensor Health Information Interface) payload parser
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include <cstdint>
#include <cstddef>

namespace afi {

class ShiParser {
public:
    /**
     * @brief Parse SHII payload (62 bytes, little-endian)
     * @param payload Raw SHII payload bytes
     * @param len Payload length
     * @param[out] msg Output message
     * @return 0 on success, negative on error
     */
    int Parse(const uint8_t* payload, size_t len, AfiShiMessage& msg);
};

} // namespace afi
