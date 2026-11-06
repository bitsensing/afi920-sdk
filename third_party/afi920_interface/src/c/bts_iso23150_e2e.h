/**
 * @file bts_iso23150_e2e.h
 * @brief E2E communication protection header (20B, CRC-32)
 *
 * Authoritative spec: docs/04_Data_Stream/10_E2E_Protection.md
 *
 * Scope: RDI / SHII / SPI / CSII / Config API.
 * Excluded: Discovery (UDP 30520) and System Log (UDP 30515, no SOME/IP
 * framing, so there is no place to carry the header).
 *
 * The 20B header sits between the SOME/IP header and the body. All fields are
 * Big-endian, unlike the LE ISO23150 payload. The field layout follows AUTOSAR
 * E2E Profile 7, but the CRC is CRC-32 rather than Profile 7's CRC-64/XZ, so
 * this is NOT an AUTOSAR-conformant profile - 10_E2E_Protection.md is the spec.
 *
 *   offset  size  field
 *     0      4    CRC high   = 0x00000000 (kept so a CRC-64 widening does not
 *                              move any field)
 *     4      4    CRC-32     over the body only
 *     8      4    Length     body byte count
 *    12      4    Counter    sequence counter (rules: System Spec 7.3, 8.2)
 *    16      4    Data ID    masquerade guard
 *
 * CRC: IEEE 802.3 CRC-32 (zlib, Ethernet FCS). poly 0x04C11DB7 (reflected
 *   0xEDB88320), reflected input and output, init 0xFFFFFFFF, final XOR
 *   0xFFFFFFFF. KAT: crc("123456789") = 0xCBF43926. Any zlib/binascii crc32
 *   reproduces it. The S32R45 DMA_CRC computes it with CTL[POLY_SEL] = 0000b
 *   and GEC[SWAP_BIT] (input bit reflection); the sender reflects the output
 *   (RBIT) and applies the final XOR in software.
 *
 * Protected region = the body only. The header's own 20B are not covered,
 * because the hardware path processes one contiguous address range. Length,
 * Counter and Data ID are checked by value consistency instead (Phase 2).
 *
 * Phase 1 (current): the sender computes the CRC over the body and increments
 *   the counter. Receivers do not verify yet.
 *   - Phase 2 (future): receiver verification + state machine (E2E_SM).
 */

#ifndef BTS_ISO23150_E2E_H
#define BTS_ISO23150_E2E_H

#include <string.h>
#include "bts_iso23150.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define BTS_E2E_HEADER_SIZE             (20u)

/* E2E header field offsets (relative to E2E header start, Big-endian) */
#define BTS_E2E_OFF_CRC_HIGH            (0u)   /* 4B: always 0 */
#define BTS_E2E_OFF_CRC                 (4u)   /* 4B: CRC-32 over the body */
#define BTS_E2E_OFF_LENGTH              (8u)   /* 4B: body byte count */
#define BTS_E2E_OFF_COUNTER             (12u)  /* 4B: sequence counter */
#define BTS_E2E_OFF_DATA_ID             (16u)  /* 4B: E2E Data ID */

/* CRC-32 parameters (IEEE 802.3). The update API takes and returns the final
 * value (zlib crc32() convention), so a chain starts from 0. */
#define BTS_E2E_CRC32_POLY              (0xEDB88320u)  /* 0x04C11DB7 reflected */
#define BTS_E2E_CRC32_INIT              (0x00000000u)
#define BTS_E2E_CRC32_KAT               (0xCBF43926u)  /* crc("123456789") */

/* E2E Data ID = Service ID 0x6000 in the high half, the channel's own
 * identifier in the low half. Registry: 10_E2E_Protection.md 5. */
#define BTS_E2E_DATA_ID(sub16)          (0x60000000u | ((uint32_t)(sub16) & 0xFFFFu))

#define BTS_E2E_DATA_ID_CSII            (0x60008001u)
#define BTS_E2E_DATA_ID_RDI             (0x60008002u)
#define BTS_E2E_DATA_ID_SHII            (0x60008003u)
#define BTS_E2E_DATA_ID_SPI             (0x60008004u)

/*============================================================================
 * CRC-32 (nibble table, LSB-first)
 *============================================================================*/

/**
 * @brief Incrementally update the CRC over n bytes.
 *
 * zlib crc32() convention: crc is the final value of the bytes so far, and the
 * first call passes BTS_E2E_CRC32_INIT (0). Chained calls give the same value
 * as one call over the concatenation, with no separate finalize step.
 *
 * The table is 16 entries (64B) rather than the usual 256 entries (1KB): M7_2
 * runs from SRAM and that kilobyte is a real share of its budget, so each byte
 * costs two lookups instead of one. Bodies here are kilobytes, not megabytes.
 *
 * @note On a -O0 translation unit this loop is slow enough to matter (164
 *   cycles/byte measured on M7_2). Callers in such projects wrap it in a leaf
 *   function carrying __attribute__((optimize("O2"))).
 */
static inline uint32_t bts_e2e_crc32_update(uint32_t crc,
                                            const uint8_t* p,
                                            size_t n)
{
    static const uint32_t nib[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
        0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
    };
    size_t i;

    if (p == NULL) {
        return crc;
    }
    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc ^= (uint32_t)p[i];
        crc = (crc >> 4) ^ nib[crc & 0x0Fu];
        crc = (crc >> 4) ^ nib[crc & 0x0Fu];
    }
    return ~crc;
}

/** One-shot CRC over a single buffer. */
static inline uint32_t bts_e2e_crc32(const uint8_t* p, size_t n)
{
    return bts_e2e_crc32_update(BTS_E2E_CRC32_INIT, p, n);
}

/*============================================================================
 * API
 *============================================================================*/

/**
 * @brief Build a 20-byte E2E header (Big-endian).
 *
 * Writes Length/Counter/Data ID and zeroes both CRC words. The CRC is filled
 * afterwards by bts_e2e_apply_crc() once the body bytes exist.
 *
 * Header-only static inline so no extra .c needs to be added to consuming
 * build systems (S32DS auto-generated makefiles).
 *
 * @param out       Output buffer, must be at least BTS_E2E_HEADER_SIZE bytes.
 * @param data_id   E2E Data ID (BTS_E2E_DATA_ID_* or BTS_E2E_DATA_ID()).
 * @param counter   E2E sequence counter. The caller owns its rule (System Spec 7.3, 8.2).
 * @param body_len  Length of the body that follows the header.
 * @return BTS_E2E_HEADER_SIZE on success, or negative error code.
 */
static inline int bts_e2e_build_header(uint8_t* out,
                                       uint32_t data_id,
                                       uint32_t counter,
                                       uint32_t body_len)
{
    if (out == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }
    /* CRC (offset 0..7): zeroed here, filled by bts_e2e_apply_crc(). */
    memset(&out[BTS_E2E_OFF_CRC_HIGH], 0, 8);
    bts_pack_be32(&out[BTS_E2E_OFF_LENGTH],  body_len);
    bts_pack_be32(&out[BTS_E2E_OFF_COUNTER], counter);
    bts_pack_be32(&out[BTS_E2E_OFF_DATA_ID], data_id);
    return (int)BTS_E2E_HEADER_SIZE;
}

/**
 * @brief Compute the CRC over the body and write it into the header.
 *
 * Works for both layouts: a contiguous frame passes body = hdr +
 * BTS_E2E_HEADER_SIZE, a scatter-gather frame passes its separate payload
 * buffer. The protected region is the body alone either way, so the two cases
 * are the same call.
 *
 * @param hdr       20B E2E header built by bts_e2e_build_header().
 * @param body      Body bytes that follow the header on the wire.
 * @param body_len  Length of body in bytes.
 */
static inline void bts_e2e_apply_crc(uint8_t* hdr,
                                     const uint8_t* body,
                                     size_t body_len)
{
    if (hdr == NULL) {
        return;
    }
    bts_pack_be32(&hdr[BTS_E2E_OFF_CRC_HIGH], 0u);
    bts_pack_be32(&hdr[BTS_E2E_OFF_CRC], bts_e2e_crc32(body, body_len));
}

#ifdef __cplusplus
}
#endif

#endif /* BTS_ISO23150_E2E_H */
