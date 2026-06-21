/**
 * @file bts_iso23150_e2e.h
 * @brief AUTOSAR E2E Profile 7 header (20B) for ISO 23150 data streams
 *
 * Authoritative spec: docs/04_Data_Stream/10_E2E_Protection.md
 * AUTOSAR PRS_E2EProtocol R22-11 6.10 (Profile 7).
 *
 * Scope: RDI / SHII / SPI / CSII only (NOT Raw Data, NOT Config API).
 * The 20B E2E header is inserted between the SOME/IP header and the
 * ISO23150 payload. All four fields are Big-endian (profile-mandated),
 * unlike the LE ISO23150 payload.
 *
 * Phase 1 (current): sender computes the CRC-64/XZ over the protected
 *   region (E2E header offset 8..end = Length+Counter+DataID + payload)
 *   and writes it Big-endian into the 8-byte CRC field. Counter is
 *   incremented per stream by the caller.
 *   - Phase 2 (future): receiver verification + state machine (E2E_SM).
 *
 * CRC: AUTOSAR Crc_CalculateCRC64 == CRC-64/XZ
 *   poly 0x42F0E1EBA9EA3693 (reflected 0xC96C5795D7870F42),
 *   init/xorout 0xFFFFFFFFFFFFFFFF, refin/refout true.
 *   KAT: crc("123456789") = 0x995DC9BBDF1939FA.
 *   Reference impls: src/csharp/.../E2eHeader.cs, tests/python/lib/e2e.py.
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
#define BTS_E2E_OFF_CRC                 (0u)   /* 8B (CRC-64) */
#define BTS_E2E_OFF_LENGTH              (8u)   /* 4B: protected data length */
#define BTS_E2E_OFF_COUNTER             (12u)  /* 4B: per-stream sequence counter */
#define BTS_E2E_OFF_DATA_ID             (16u)  /* 4B: E2E Data ID (= MessageID32) */

/* CRC-64/XZ parameters (AUTOSAR Crc_CalculateCRC64). */
#define BTS_E2E_CRC64_INIT              (0xFFFFFFFFFFFFFFFFull)
#define BTS_E2E_CRC64_XOROUT            (0xFFFFFFFFFFFFFFFFull)

/* E2E Data ID = stream MessageID32 (Service<<16 | Event), masquerade guard.
 * Registry: docs/04_Data_Stream/10_E2E_Protection.md 5. */
#define BTS_E2E_DATA_ID_CSII            (0x60008001u)
#define BTS_E2E_DATA_ID_RDI             (0x60008002u)
#define BTS_E2E_DATA_ID_SHII            (0x60008003u)
#define BTS_E2E_DATA_ID_SPI             (0x60008004u)

/*============================================================================
 * CRC-64/XZ (table-driven, reflected)
 *============================================================================*/

/**
 * @brief Incrementally update a reflected CRC-64/XZ over n bytes.
 *
 * Caller seeds crc with BTS_E2E_CRC64_INIT on the first call and applies the
 * final XOR (^ BTS_E2E_CRC64_XOROUT) after the last chunk. Splitting the
 * protected region across calls (E2E header tail, then payload) yields the
 * same result as one contiguous buffer  used for RDI's scatter-gather frame.
 *
 * The 256-entry table is a function-local static const (compile-time,
 * deterministic, no dynamic allocation; instantiated only in TUs that call
 * this function).
 */
static inline uint64_t bts_e2e_crc64_update(uint64_t crc,
                                            const uint8_t* p,
                                            size_t n)
{
    static const uint64_t table[256] = {
        0x0000000000000000ULL, 0xB32E4CBE03A75F6FULL, 0xF4843657A840A05BULL, 0x47AA7AE9ABE7FF34ULL,
        0x7BD0C384FF8F5E33ULL, 0xC8FE8F3AFC28015CULL, 0x8F54F5D357CFFE68ULL, 0x3C7AB96D5468A107ULL,
        0xF7A18709FF1EBC66ULL, 0x448FCBB7FCB9E309ULL, 0x0325B15E575E1C3DULL, 0xB00BFDE054F94352ULL,
        0x8C71448D0091E255ULL, 0x3F5F08330336BD3AULL, 0x78F572DAA8D1420EULL, 0xCBDB3E64AB761D61ULL,
        0x7D9BA13851336649ULL, 0xCEB5ED8652943926ULL, 0x891F976FF973C612ULL, 0x3A31DBD1FAD4997DULL,
        0x064B62BCAEBC387AULL, 0xB5652E02AD1B6715ULL, 0xF2CF54EB06FC9821ULL, 0x41E11855055BC74EULL,
        0x8A3A2631AE2DDA2FULL, 0x39146A8FAD8A8540ULL, 0x7EBE1066066D7A74ULL, 0xCD905CD805CA251BULL,
        0xF1EAE5B551A2841CULL, 0x42C4A90B5205DB73ULL, 0x056ED3E2F9E22447ULL, 0xB6409F5CFA457B28ULL,
        0xFB374270A266CC92ULL, 0x48190ECEA1C193FDULL, 0x0FB374270A266CC9ULL, 0xBC9D3899098133A6ULL,
        0x80E781F45DE992A1ULL, 0x33C9CD4A5E4ECDCEULL, 0x7463B7A3F5A932FAULL, 0xC74DFB1DF60E6D95ULL,
        0x0C96C5795D7870F4ULL, 0xBFB889C75EDF2F9BULL, 0xF812F32EF538D0AFULL, 0x4B3CBF90F69F8FC0ULL,
        0x774606FDA2F72EC7ULL, 0xC4684A43A15071A8ULL, 0x83C230AA0AB78E9CULL, 0x30EC7C140910D1F3ULL,
        0x86ACE348F355AADBULL, 0x3582AFF6F0F2F5B4ULL, 0x7228D51F5B150A80ULL, 0xC10699A158B255EFULL,
        0xFD7C20CC0CDAF4E8ULL, 0x4E526C720F7DAB87ULL, 0x09F8169BA49A54B3ULL, 0xBAD65A25A73D0BDCULL,
        0x710D64410C4B16BDULL, 0xC22328FF0FEC49D2ULL, 0x85895216A40BB6E6ULL, 0x36A71EA8A7ACE989ULL,
        0x0ADDA7C5F3C4488EULL, 0xB9F3EB7BF06317E1ULL, 0xFE5991925B84E8D5ULL, 0x4D77DD2C5823B7BAULL,
        0x64B62BCAEBC387A1ULL, 0xD7986774E864D8CEULL, 0x90321D9D438327FAULL, 0x231C512340247895ULL,
        0x1F66E84E144CD992ULL, 0xAC48A4F017EB86FDULL, 0xEBE2DE19BC0C79C9ULL, 0x58CC92A7BFAB26A6ULL,
        0x9317ACC314DD3BC7ULL, 0x2039E07D177A64A8ULL, 0x67939A94BC9D9B9CULL, 0xD4BDD62ABF3AC4F3ULL,
        0xE8C76F47EB5265F4ULL, 0x5BE923F9E8F53A9BULL, 0x1C4359104312C5AFULL, 0xAF6D15AE40B59AC0ULL,
        0x192D8AF2BAF0E1E8ULL, 0xAA03C64CB957BE87ULL, 0xEDA9BCA512B041B3ULL, 0x5E87F01B11171EDCULL,
        0x62FD4976457FBFDBULL, 0xD1D305C846D8E0B4ULL, 0x96797F21ED3F1F80ULL, 0x2557339FEE9840EFULL,
        0xEE8C0DFB45EE5D8EULL, 0x5DA24145464902E1ULL, 0x1A083BACEDAEFDD5ULL, 0xA9267712EE09A2BAULL,
        0x955CCE7FBA6103BDULL, 0x267282C1B9C65CD2ULL, 0x61D8F8281221A3E6ULL, 0xD2F6B4961186FC89ULL,
        0x9F8169BA49A54B33ULL, 0x2CAF25044A02145CULL, 0x6B055FEDE1E5EB68ULL, 0xD82B1353E242B407ULL,
        0xE451AA3EB62A1500ULL, 0x577FE680B58D4A6FULL, 0x10D59C691E6AB55BULL, 0xA3FBD0D71DCDEA34ULL,
        0x6820EEB3B6BBF755ULL, 0xDB0EA20DB51CA83AULL, 0x9CA4D8E41EFB570EULL, 0x2F8A945A1D5C0861ULL,
        0x13F02D374934A966ULL, 0xA0DE61894A93F609ULL, 0xE7741B60E174093DULL, 0x545A57DEE2D35652ULL,
        0xE21AC88218962D7AULL, 0x5134843C1B317215ULL, 0x169EFED5B0D68D21ULL, 0xA5B0B26BB371D24EULL,
        0x99CA0B06E7197349ULL, 0x2AE447B8E4BE2C26ULL, 0x6D4E3D514F59D312ULL, 0xDE6071EF4CFE8C7DULL,
        0x15BB4F8BE788911CULL, 0xA6950335E42FCE73ULL, 0xE13F79DC4FC83147ULL, 0x521135624C6F6E28ULL,
        0x6E6B8C0F1807CF2FULL, 0xDD45C0B11BA09040ULL, 0x9AEFBA58B0476F74ULL, 0x29C1F6E6B3E0301BULL,
        0xC96C5795D7870F42ULL, 0x7A421B2BD420502DULL, 0x3DE861C27FC7AF19ULL, 0x8EC62D7C7C60F076ULL,
        0xB2BC941128085171ULL, 0x0192D8AF2BAF0E1EULL, 0x4638A2468048F12AULL, 0xF516EEF883EFAE45ULL,
        0x3ECDD09C2899B324ULL, 0x8DE39C222B3EEC4BULL, 0xCA49E6CB80D9137FULL, 0x7967AA75837E4C10ULL,
        0x451D1318D716ED17ULL, 0xF6335FA6D4B1B278ULL, 0xB199254F7F564D4CULL, 0x02B769F17CF11223ULL,
        0xB4F7F6AD86B4690BULL, 0x07D9BA1385133664ULL, 0x4073C0FA2EF4C950ULL, 0xF35D8C442D53963FULL,
        0xCF273529793B3738ULL, 0x7C0979977A9C6857ULL, 0x3BA3037ED17B9763ULL, 0x888D4FC0D2DCC80CULL,
        0x435671A479AAD56DULL, 0xF0783D1A7A0D8A02ULL, 0xB7D247F3D1EA7536ULL, 0x04FC0B4DD24D2A59ULL,
        0x3886B22086258B5EULL, 0x8BA8FE9E8582D431ULL, 0xCC0284772E652B05ULL, 0x7F2CC8C92DC2746AULL,
        0x325B15E575E1C3D0ULL, 0x8175595B76469CBFULL, 0xC6DF23B2DDA1638BULL, 0x75F16F0CDE063CE4ULL,
        0x498BD6618A6E9DE3ULL, 0xFAA59ADF89C9C28CULL, 0xBD0FE036222E3DB8ULL, 0x0E21AC88218962D7ULL,
        0xC5FA92EC8AFF7FB6ULL, 0x76D4DE52895820D9ULL, 0x317EA4BB22BFDFEDULL, 0x8250E80521188082ULL,
        0xBE2A516875702185ULL, 0x0D041DD676D77EEAULL, 0x4AAE673FDD3081DEULL, 0xF9802B81DE97DEB1ULL,
        0x4FC0B4DD24D2A599ULL, 0xFCEEF8632775FAF6ULL, 0xBB44828A8C9205C2ULL, 0x086ACE348F355AADULL,
        0x34107759DB5DFBAAULL, 0x873E3BE7D8FAA4C5ULL, 0xC094410E731D5BF1ULL, 0x73BA0DB070BA049EULL,
        0xB86133D4DBCC19FFULL, 0x0B4F7F6AD86B4690ULL, 0x4CE50583738CB9A4ULL, 0xFFCB493D702BE6CBULL,
        0xC3B1F050244347CCULL, 0x709FBCEE27E418A3ULL, 0x3735C6078C03E797ULL, 0x841B8AB98FA4B8F8ULL,
        0xADDA7C5F3C4488E3ULL, 0x1EF430E13FE3D78CULL, 0x595E4A08940428B8ULL, 0xEA7006B697A377D7ULL,
        0xD60ABFDBC3CBD6D0ULL, 0x6524F365C06C89BFULL, 0x228E898C6B8B768BULL, 0x91A0C532682C29E4ULL,
        0x5A7BFB56C35A3485ULL, 0xE955B7E8C0FD6BEAULL, 0xAEFFCD016B1A94DEULL, 0x1DD181BF68BDCBB1ULL,
        0x21AB38D23CD56AB6ULL, 0x9285746C3F7235D9ULL, 0xD52F0E859495CAEDULL, 0x6601423B97329582ULL,
        0xD041DD676D77EEAAULL, 0x636F91D96ED0B1C5ULL, 0x24C5EB30C5374EF1ULL, 0x97EBA78EC690119EULL,
        0xAB911EE392F8B099ULL, 0x18BF525D915FEFF6ULL, 0x5F1528B43AB810C2ULL, 0xEC3B640A391F4FADULL,
        0x27E05A6E926952CCULL, 0x94CE16D091CE0DA3ULL, 0xD3646C393A29F297ULL, 0x604A2087398EADF8ULL,
        0x5C3099EA6DE60CFFULL, 0xEF1ED5546E415390ULL, 0xA8B4AFBDC5A6ACA4ULL, 0x1B9AE303C601F3CBULL,
        0x56ED3E2F9E224471ULL, 0xE5C372919D851B1EULL, 0xA26908783662E42AULL, 0x114744C635C5BB45ULL,
        0x2D3DFDAB61AD1A42ULL, 0x9E13B115620A452DULL, 0xD9B9CBFCC9EDBA19ULL, 0x6A978742CA4AE576ULL,
        0xA14CB926613CF817ULL, 0x1262F598629BA778ULL, 0x55C88F71C97C584CULL, 0xE6E6C3CFCADB0723ULL,
        0xDA9C7AA29EB3A624ULL, 0x69B2361C9D14F94BULL, 0x2E184CF536F3067FULL, 0x9D36004B35545910ULL,
        0x2B769F17CF112238ULL, 0x9858D3A9CCB67D57ULL, 0xDFF2A94067518263ULL, 0x6CDCE5FE64F6DD0CULL,
        0x50A65C93309E7C0BULL, 0xE388102D33392364ULL, 0xA4226AC498DEDC50ULL, 0x170C267A9B79833FULL,
        0xDCD7181E300F9E5EULL, 0x6FF954A033A8C131ULL, 0x28532E49984F3E05ULL, 0x9B7D62F79BE8616AULL,
        0xA707DB9ACF80C06DULL, 0x14299724CC279F02ULL, 0x5383EDCD67C06036ULL, 0xE0ADA17364673F59ULL,
    };
    size_t i;
    for (i = 0; i < n; i++) {
        crc = (crc >> 8) ^ table[(uint8_t)(crc ^ p[i])];
    }
    return crc;
}

/* Write a 64-bit value Big-endian (E2E CRC field). Local helper: bts_iso23150.h
 * provides be16/be32 (and le64) but no be64. */
static inline void bts_e2e_pack_be64(uint8_t* p, uint64_t v)
{
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);  p[7] = (uint8_t)(v);
}

/*============================================================================
 * API
 *============================================================================*/

/**
 * @brief Build a 20-byte E2E Profile 7 header (Big-endian).
 *
 * Writes Length/Counter/Data ID (Big-endian) and zeroes the CRC field. The CRC
 * is filled afterwards by bts_e2e_apply_crc_contig() / bts_e2e_apply_crc_split()
 * once the protected payload bytes that follow are in place (the CRC spans the
 * header tail + payload, which do not exist yet at build time).
 *
 * Header-only static inline so no extra .c needs to be added to consuming
 * build systems (S32DS auto-generated makefiles / Kaitai test CMake).
 *
 * @param out       Output buffer, must be at least BTS_E2E_HEADER_SIZE bytes.
 * @param data_id   E2E Data ID (use BTS_E2E_DATA_ID_*).
 * @param counter   Per-stream E2E sequence counter (caller increments +1/msg).
 * @param data_len  Length of the protected ISO23150 payload that follows.
 * @return BTS_E2E_HEADER_SIZE on success, or negative error code.
 */
static inline int bts_e2e_build_header(uint8_t* out,
                                       uint32_t data_id,
                                       uint32_t counter,
                                       uint32_t data_len)
{
    if (out == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }
    /* CRC (offset 0..7): zeroed here, filled by bts_e2e_apply_crc_*(). */
    memset(&out[BTS_E2E_OFF_CRC], 0, 8);
    /* Length / Counter / Data ID  Big-endian (Profile 7 mandated). */
    bts_pack_be32(&out[BTS_E2E_OFF_LENGTH],  data_len);
    bts_pack_be32(&out[BTS_E2E_OFF_COUNTER], counter);
    bts_pack_be32(&out[BTS_E2E_OFF_DATA_ID], data_id);
    return (int)BTS_E2E_HEADER_SIZE;
}

/**
 * @brief Compute and write the E2E CRC for a contiguous frame (header+payload).
 *
 * For streams whose 20B E2E header and ISO23150 payload live in one buffer
 * (SHII, SPI). Computes CRC-64/XZ over frame[8 .. total_len) and writes it
 * Big-endian into frame[0..7]. Call after bts_e2e_build_header() once the
 * payload has been serialized into frame + BTS_E2E_HEADER_SIZE.
 *
 * @param frame      Buffer holding the 20B E2E header followed by the payload.
 * @param total_len  BTS_E2E_HEADER_SIZE + payload length.
 */
static inline void bts_e2e_apply_crc_contig(uint8_t* frame, size_t total_len)
{
    uint64_t crc;
    if (frame == NULL || total_len <= BTS_E2E_OFF_LENGTH) {
        return;
    }
    crc = bts_e2e_crc64_update(BTS_E2E_CRC64_INIT,
                               &frame[BTS_E2E_OFF_LENGTH],
                               total_len - BTS_E2E_OFF_LENGTH);
    crc ^= BTS_E2E_CRC64_XOROUT;
    bts_e2e_pack_be64(&frame[BTS_E2E_OFF_CRC], crc);
}

/**
 * @brief Compute and write the E2E CRC for a split (scatter-gather) frame.
 *
 * For RDI, whose 20B E2E header and (large) ISO23150 payload are separate
 * buffers sent via iovec. Computes CRC-64/XZ over the header tail
 * (hdr[8..19], i.e. Length+Counter+DataID) then the payload, and writes the
 * result Big-endian into hdr[0..7]. Equivalent to the contiguous case.
 *
 * @param hdr          20B E2E header buffer (built by bts_e2e_build_header()).
 * @param payload      Serialized ISO23150 payload that follows on the wire.
 * @param payload_len  Length of payload in bytes.
 */
static inline void bts_e2e_apply_crc_split(uint8_t* hdr,
                                           const uint8_t* payload,
                                           size_t payload_len)
{
    uint64_t crc;
    if (hdr == NULL) {
        return;
    }
    crc = bts_e2e_crc64_update(BTS_E2E_CRC64_INIT,
                               &hdr[BTS_E2E_OFF_LENGTH],
                               (size_t)(BTS_E2E_HEADER_SIZE - BTS_E2E_OFF_LENGTH));
    if (payload != NULL && payload_len > 0) {
        crc = bts_e2e_crc64_update(crc, payload, payload_len);
    }
    crc ^= BTS_E2E_CRC64_XOROUT;
    bts_e2e_pack_be64(&hdr[BTS_E2E_OFF_CRC], crc);
}

#ifdef __cplusplus
}
#endif

#endif /* BTS_ISO23150_E2E_H */
