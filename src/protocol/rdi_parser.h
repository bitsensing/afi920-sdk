/**
 * @file rdi_parser.h
 * @brief RDI (Radar Detection Interface) payload parser
 *
 * Deserializes ISO 23150 RDI binary payload into bts_rdi_message_t.
 * Header: 61 bytes + Detections: 94 bytes each (little-endian).
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include <cstdint>
#include <cstddef>
#include <vector>

namespace afi {

class RdiParser {
public:
    /**
     * @brief Parse RDI payload (after SOME/IP header removal & TP reassembly)
     * @param payload Raw RDI payload bytes (little-endian)
     * @param len Payload length
     * @param[out] frame Output frame (detections_storage will be populated)
     * @return 0 on success, negative on error
     */
    int Parse(const uint8_t* payload, size_t len, AfiRdiFrame& frame);

private:
    static int ParseDetection(const uint8_t* data, bts_rdi_detection_t& det);
};

} // namespace afi
