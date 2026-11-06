/**
 * @file pmoi_parser.h
 * @brief PMOI (Potentially Moving Object Interface) payload parser
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"

#include <cstddef>
#include <cstdint>

namespace afi {

class PmoiParser {
public:
    /** Parse a little-endian PMOI ISO 23150 payload after E2E removal. */
    int Parse(const uint8_t* payload, size_t len, AfiPmoiFrame& frame);

private:
    static int ParseObject(const uint8_t* data, bts_pmoi_object_t& object);
};

} // namespace afi
