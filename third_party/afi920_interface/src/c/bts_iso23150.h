/**
 * @file bts_iso23150.h
 * @brief Common definitions for ISO 23150 serialization library
 */

#ifndef BTS_ISO23150_H
#define BTS_ISO23150_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>  /* memcpy for f32/f64 (un)pack */

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Configuration
 *============================================================================*/

/* Payload endianness (default: little-endian) */
#if defined(BTS_ISO23150_BIG_ENDIAN) && defined(BTS_ISO23150_LITTLE_ENDIAN)
#error "Define only one: BTS_ISO23150_BIG_ENDIAN or BTS_ISO23150_LITTLE_ENDIAN"
#endif

#if !defined(BTS_ISO23150_BIG_ENDIAN) && !defined(BTS_ISO23150_LITTLE_ENDIAN)
#define BTS_ISO23150_LITTLE_ENDIAN
#endif

/*============================================================================
 * Error Codes
 *============================================================================*/

typedef enum {
    BTS_ISO23150_OK                    = 0,
    BTS_ISO23150_ERR_NULL_PARAM        = -1,
    BTS_ISO23150_ERR_BUFFER_TOO_SMALL  = -2,
    BTS_ISO23150_ERR_INVALID_PARAM     = -3,
    BTS_ISO23150_ERR_SERIALIZE_FAILED  = -4,
    BTS_ISO23150_ERR_DESERIALIZE_FAILED= -5,
    BTS_ISO23150_ERR_INDEX_OUT_OF_RANGE= -6,
} bts_iso23150_error_t;

/*============================================================================
 * SOME/IP Constants
 *============================================================================*/

#define BTS_SOMEIP_SERVICE_ID           (0x6000u)

/* Event IDs */
#define BTS_SOMEIP_EVENT_SII            (0x8001u)  /* CSII (reserved) */
#define BTS_SOMEIP_EVENT_RDI            (0x8002u)
#define BTS_SOMEIP_EVENT_SHII           (0x8003u)
#define BTS_SOMEIP_EVENT_SPI            (0x8004u)

/* Protocol constants */
#define BTS_SOMEIP_PROTOCOL_VERSION     (0x01u)
#define BTS_SOMEIP_INTERFACE_VERSION    (0x01u)
#define BTS_SOMEIP_MSGTYPE_NOTIFICATION (0x02u)
#define BTS_SOMEIP_MSGTYPE_TP_FLAG      (0x20u)
#define BTS_SOMEIP_RETURN_OK            (0x00u)
#define BTS_SOMEIP_CLIENT_ID            (0x0001u)

/* Size constants */
#define BTS_SOMEIP_HEADER_SIZE          (16u)
#define BTS_SOMEIP_TP_HEADER_SIZE       (4u)
#define BTS_SOMEIP_TP_CHUNK_SIZE        (1392u)
#define BTS_SOMEIP_TP_OFFSET_UNIT       (16u)

/*============================================================================
 * Interface IDs
 *============================================================================*/

typedef enum {
    BTS_IID_UNKNOWN                     = 0x00,
    BTS_IID_OTHER                       = 0x01,
    BTS_IID_POTENTIALLY_MOVING_OBJECTS  = 0x02,
    BTS_IID_ROAD_OBJECT                 = 0x03,
    BTS_IID_STATIC_OBJECT               = 0x04,
    BTS_IID_FREE_SPACE_AREA_OBJECT      = 0x05,
    BTS_IID_CAMERA_FEATURE              = 0x06,
    BTS_IID_ULTRASONIC_FEATURE          = 0x07,
    BTS_IID_RADAR_DETECTION             = 0x08,
    BTS_IID_LIDAR_DETECTION             = 0x09,
    BTS_IID_CAMERA_DETECTION            = 0x0A,
    BTS_IID_ULTRASONIC_DETECTION        = 0x0B,
    BTS_IID_SENSOR_PERFORMANCE          = 0x0C,
    BTS_IID_SENSOR_HEALTH_INFORMATION   = 0x0D,
    BTS_IID_COMMON_SENSOR_INPUT         = 0x0E,
} bts_interface_id_t;

/*============================================================================
 * Data Qualifier Enum
 *============================================================================*/

typedef enum {
    BTS_DQ_NORMAL                       = 0,
    BTS_DQ_NOT_AVAILABLE                = 1,
    BTS_DQ_REDUCE_IN_COVERAGE           = 2,
    BTS_DQ_REDUCE_IN_PERFORMANCE        = 3,
    BTS_DQ_REDUCE_COVERAGE_AND_PERF     = 4,
    BTS_DQ_TEST_MODE                    = 5,
    BTS_DQ_INVALID                      = 6,
} bts_data_qualifier_t;

/*============================================================================
 * Common Types
 *============================================================================*/

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} bts_interface_version_t;

/*============================================================================
 * Pack/Unpack Helpers (internal use)
 *============================================================================*/

/* Big-endian packing (for SOME/IP header) */
static inline void bts_pack_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

static inline void bts_pack_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

/* Little-endian packing (for payload) */
static inline void bts_pack_le16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void bts_pack_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline void bts_pack_le64(uint8_t* p, uint64_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
    p[4] = (uint8_t)((v >> 32) & 0xFF);
    p[5] = (uint8_t)((v >> 40) & 0xFF);
    p[6] = (uint8_t)((v >> 48) & 0xFF);
    p[7] = (uint8_t)((v >> 56) & 0xFF);
}

static inline void bts_pack_f32(uint8_t* p, float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
#ifdef BTS_ISO23150_BIG_ENDIAN
    bts_pack_be32(p, u);
#else
    bts_pack_le32(p, u);
#endif
}

/* Payload packing (endianness-aware) */
static inline void bts_pack_u16(uint8_t* p, uint16_t v) {
#ifdef BTS_ISO23150_BIG_ENDIAN
    bts_pack_be16(p, v);
#else
    bts_pack_le16(p, v);
#endif
}

static inline void bts_pack_u32(uint8_t* p, uint32_t v) {
#ifdef BTS_ISO23150_BIG_ENDIAN
    bts_pack_be32(p, v);
#else
    bts_pack_le32(p, v);
#endif
}

static inline void bts_pack_u64(uint8_t* p, uint64_t v) {
#ifdef BTS_ISO23150_BIG_ENDIAN
    /* Big-endian 64-bit */
    p[0] = (uint8_t)((v >> 56) & 0xFF);
    p[1] = (uint8_t)((v >> 48) & 0xFF);
    p[2] = (uint8_t)((v >> 40) & 0xFF);
    p[3] = (uint8_t)((v >> 32) & 0xFF);
    p[4] = (uint8_t)((v >> 24) & 0xFF);
    p[5] = (uint8_t)((v >> 16) & 0xFF);
    p[6] = (uint8_t)((v >> 8) & 0xFF);
    p[7] = (uint8_t)(v & 0xFF);
#else
    bts_pack_le64(p, v);
#endif
}

/*============================================================================
 * Unpack Helpers (for deserialization — CSII inbound and future receivers)
 *============================================================================*/

/* Little-endian unpacking (for payload) */
static inline uint16_t bts_unpack_le16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t bts_unpack_le32(const uint8_t* p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static inline uint64_t bts_unpack_le64(const uint8_t* p) {
    return (uint64_t)p[0]
         | ((uint64_t)p[1] << 8)
         | ((uint64_t)p[2] << 16)
         | ((uint64_t)p[3] << 24)
         | ((uint64_t)p[4] << 32)
         | ((uint64_t)p[5] << 40)
         | ((uint64_t)p[6] << 48)
         | ((uint64_t)p[7] << 56);
}

/* Big-endian unpacking (for SOME/IP header) */
static inline uint16_t bts_unpack_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t bts_unpack_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         | (uint32_t)p[3];
}

/* Payload-endianness-aware (matches bts_pack_u16/32/64) */
static inline uint16_t bts_unpack_u16(const uint8_t* p) {
#ifdef BTS_ISO23150_BIG_ENDIAN
    return bts_unpack_be16(p);
#else
    return bts_unpack_le16(p);
#endif
}

static inline uint32_t bts_unpack_u32(const uint8_t* p) {
#ifdef BTS_ISO23150_BIG_ENDIAN
    return bts_unpack_be32(p);
#else
    return bts_unpack_le32(p);
#endif
}

static inline uint64_t bts_unpack_u64(const uint8_t* p) {
#ifdef BTS_ISO23150_BIG_ENDIAN
    /* Big-endian 64-bit */
    return ((uint64_t)p[0] << 56)
         | ((uint64_t)p[1] << 48)
         | ((uint64_t)p[2] << 40)
         | ((uint64_t)p[3] << 32)
         | ((uint64_t)p[4] << 24)
         | ((uint64_t)p[5] << 16)
         | ((uint64_t)p[6] << 8)
         | (uint64_t)p[7];
#else
    return bts_unpack_le64(p);
#endif
}

static inline float bts_unpack_f32(const uint8_t* p) {
    uint32_t u;
    float f;
#ifdef BTS_ISO23150_BIG_ENDIAN
    u = bts_unpack_be32(p);
#else
    u = bts_unpack_le32(p);
#endif
    memcpy(&f, &u, sizeof(f));
    return f;
}

static inline double bts_unpack_f64(const uint8_t* p) {
    uint64_t u;
    double d;
#ifdef BTS_ISO23150_BIG_ENDIAN
    u = ((uint64_t)p[0] << 56)
      | ((uint64_t)p[1] << 48)
      | ((uint64_t)p[2] << 40)
      | ((uint64_t)p[3] << 32)
      | ((uint64_t)p[4] << 24)
      | ((uint64_t)p[5] << 16)
      | ((uint64_t)p[6] << 8)
      | (uint64_t)p[7];
#else
    u = bts_unpack_le64(p);
#endif
    memcpy(&d, &u, sizeof(d));
    return d;
}

#ifdef __cplusplus
}
#endif

#endif /* BTS_ISO23150_H */
