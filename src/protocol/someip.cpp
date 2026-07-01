// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "protocol/someip.h"
#include "util/endian_util.h"

namespace afi {

int someip_deserialize(const uint8_t* buf, size_t len, SomeipHeader& out) {
    if (!buf || len < kSomeipHeaderSize) return -1;

    uint32_t message_id = detail::unpack_be32(buf + 0);
    out.service_id = static_cast<uint16_t>((message_id >> 16) & 0xFFFF);
    out.method_id  = static_cast<uint16_t>(message_id & 0xFFFF);
    out.length     = detail::unpack_be32(buf + 4);

    uint32_t request_id = detail::unpack_be32(buf + 8);
    out.client_id  = static_cast<uint16_t>((request_id >> 16) & 0xFFFF);
    out.session_id = static_cast<uint16_t>(request_id & 0xFFFF);

    out.protocol_version  = buf[12];
    out.interface_version = buf[13];
    out.message_type      = buf[14];
    out.return_code       = buf[15];

    return 0;
}

void someip_tp_deserialize(const uint8_t* buf, SomeipTpHeader& out) {
    uint32_t raw = detail::unpack_be32(buf);
    // Bits 31:4 = offset in 16-byte units, bit 0 = more_segments
    out.offset_bytes  = (raw >> 4) * 16;
    out.more_segments = (raw & 0x01) != 0;
}

} // namespace afi
