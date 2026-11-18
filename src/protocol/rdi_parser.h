/**
 * @file rdi_parser.h
 * @brief RDI (Radar Detection Interface) payload parser
 *
 * Deserializes ISO 23150 RDI binary payload into bts_rdi_message_t.
 * Header: 36 bytes + Detections: 51 bytes each (little-endian).
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

/// List Layout of an interface v1.2.0 RDI header message (offset 36, 4B)
struct RdiListLayout {
    uint16_t entities_per_chunk = 0;
    uint16_t chunk_count = 0;
};

class RdiParser {
public:
    /**
     * @brief Parse a single-message RDI payload (interface v1.0.x and v1.1.0)
     * @param payload RDI payload after SOME/IP, TP reassembly and E2E (little-endian)
     * @param len Payload length; must hold exactly the counted detections
     * @param[out] frame Output frame (detections_storage will be populated)
     * @return 0 on success, negative on error
     */
    int Parse(const uint8_t* payload, size_t len, AfiRdiFrame& frame);

    /**
     * @brief Parse an interface v1.2.0 header message body (40B)
     * @param body Detection Header (36B) + List Layout (4B)
     * @param len Body length; must be 40
     * @param[out] msg Header fields; detections stays null
     * @param[out] layout List Layout
     * @return 0 on success, negative on error
     */
    static int ParseHeaderMessage(const uint8_t* body, size_t len,
                                  bts_rdi_message_t& msg, RdiListLayout& layout);

    /// Decode the 36B Detection Header (ISO23150 Header, Ambiguity Domain,
    /// Detection List Info) without checking the detection count.
    static void ParseDetectionHeader(const uint8_t* p, bts_rdi_message_t& msg);

    /// Decode one 51B detection.
    static void ParseDetection(const uint8_t* data, bts_rdi_detection_t& det);
};

} // namespace afi
