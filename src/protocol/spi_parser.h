/**
 * @file spi_parser.h
 * @brief SPI (Sensor Performance Interface) payload parser
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include <cstdint>
#include <cstddef>

namespace afi {

class SpiParser {
public:
    /**
     * @brief Parse SPI payload (variable length, little-endian)
     * @param payload Raw SPI payload bytes
     * @param len Payload length
     * @param[out] msg Output message
     * @return 0 on success, negative on error
     */
    int Parse(const uint8_t* payload, size_t len, AfiSpiMessage& msg);
};

} // namespace afi
