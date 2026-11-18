/**
 * @file interface_version.h
 * @brief Interface generation from the interface version a radar reports
 *
 * Interface v1.2.0 adds the `if_version` key to Discovery and GetDeviceInfo
 * and fills the ISO23150 Header InterfaceVersionID of RDI, SHII and SPI with the
 * same version. The E2E header only separates v1.0.x from later generations.
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "afi/afi_types.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace afi {

/// Generation for an `if_version` value ("1.2.0"). The key exists from
/// v1.2.0 on, so a value that does not parse is still at least kV1_2.
inline AfiInterfaceGeneration GenerationFromIfVersion(const std::string& v) {
    if (v.empty()) return AfiInterfaceGeneration::kUnknown;
    char* end = nullptr;
    const long major = std::strtol(v.c_str(), &end, 10);
    long minor = -1;
    if (end && *end == '.') minor = std::strtol(end + 1, nullptr, 10);
    if (major == 1 && minor == 0) return AfiInterfaceGeneration::kV1_0;
    if (major == 1 && minor == 1) return AfiInterfaceGeneration::kV1_1;
    return AfiInterfaceGeneration::kV1_2;
}

/// Raise kV1_1 (the E2E verdict) to kV1_2 when the ISO23150 Header at
/// `common` reports interface 1.2 or later.
inline AfiInterfaceGeneration RefineGeneration(AfiInterfaceGeneration g,
                                               const uint8_t* common,
                                               size_t len) {
    if (g != AfiInterfaceGeneration::kV1_1 || !common || len < 2) return g;
    return (common[0] == 1 && common[1] >= 2) ? AfiInterfaceGeneration::kV1_2 : g;
}

} // namespace afi
