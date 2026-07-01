/**
 * @file csii_builder.h
 * @brief CSII (Common Sensor Input) frame builder — Host → Radar
 *
 * Builds the complete 115-byte wire frame:
 *   SOME/IP header (16B, BE) + E2E Profile 7 header (20B, BE, CRC-64/XZ)
 *   + ISO 23150 CSII payload (79B, LE)
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include <cstdint>
#include <cstddef>

namespace afi {

/// Total CSII frame size on the wire (TCP single segment)
static constexpr size_t kCsiiFrameSize = 16 + 20 + BTS_CSII_SIZE;  // 115

/**
 * @brief Serialize one CSII message into a complete wire frame
 *
 * @param input        Vehicle dynamics from the host application
 * @param counter      Message counter (used for both the ISO message_counter
 *                     and the E2E counter; increment by 1 per message)
 * @param session_id   SOME/IP session id (increment per message)
 * @param timestamp_ns Measurement timestamp in nanoseconds
 * @param[out] out     Output buffer, at least kCsiiFrameSize bytes
 * @return Number of bytes written (kCsiiFrameSize), or negative on error
 */
int csii_build_frame(const AfiVehicleInput& input,
                     uint32_t counter,
                     uint16_t session_id,
                     uint64_t timestamp_ns,
                     uint8_t* out);

} // namespace afi
