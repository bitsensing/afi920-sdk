/**
 * @file bts_iso23150_rdi.h
 * @brief Radar Detection Interface (RDI) serialization for ISO 23150 - AFI920
 */

#ifndef BTS_ISO23150_RDI_H
#define BTS_ISO23150_RDI_H

#include "../bts_iso23150.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *
 * Detection Header (36B) = Common Header (24B) + Ambiguity Domain (8B,
 *   RadialVelocity begin/end) + Detection List Info (4B, capability + count).
 * Detection Entity (51B) = Status (9) + Information (10) + Position (24)
 *   + Dynamics (8).
 * Authoritative spec: docs/04_Data_Stream/03_RDI_Radar_Detections.md
 *============================================================================*/

#define BTS_RDI_HEADER_SIZE             (36u)
#define BTS_RDI_DETECTION_SIZE          (51u)
#define BTS_RDI_MAX_DETECTIONS          (8192u)
#define BTS_RDI_MAX_PAYLOAD_SIZE        (BTS_RDI_HEADER_SIZE + BTS_RDI_DETECTION_SIZE * BTS_RDI_MAX_DETECTIONS)

/*============================================================================
 * RDI Detection Structure (51 bytes)
 *============================================================================*/

typedef struct {
    /* Status (ISO23150) */
    uint8_t  existence_probability;         /* 0-100 % */
    uint16_t detection_id;                  /* 0xFFFF if unused */
    uint16_t object_id_reference;           /* associated Object ID (reserve) */
    float    timestamp_difference;          /* seconds */

    /* Information (ISO23150) */
    float    radar_cross_section;           /* dBsm */
    float    signal_to_noise_ratio;         /* dB */
    uint16_t ambiguity_grouping_id;         /* 0xFFFF if unused */

    /* Position (ISO23150) */
    float    position_radial_distance;      /* meters */
    float    position_azimuth;              /* radians */
    float    position_elevation;            /* radians */
    float    position_radial_distance_error;
    float    position_azimuth_error;
    float    position_elevation_error;

    /* Dynamics (ISO23150) */
    float    relative_velocity_radial;      /* m/s */
    float    relative_velocity_radial_error;
} bts_rdi_detection_t;

/*============================================================================
 * RDI Ambiguity Domain Structure (8 bytes)
 *============================================================================*/

typedef struct {
    float radial_velocity_begin;    /* m/s */
    float radial_velocity_end;
} bts_rdi_ambiguity_domain_t;

/*============================================================================
 * RDI Message Structure
 *============================================================================*/

typedef struct {
    /* Header */
    bts_interface_version_t interface_version;
    uint8_t  interface_id;
    uint8_t  num_serving_sensors;
    uint8_t  sensor_id;
    uint64_t timestamp;                     /* nanoseconds */
    uint32_t message_counter;
    uint32_t interface_cycle_time;          /* nanoseconds */
    uint8_t  interface_cycle_time_variation;/* 0-100 % */
    bts_data_qualifier_t data_qualifier;

    /* Ambiguity domain (RadialVelocity only) */
    bts_rdi_ambiguity_domain_t ambiguity_domain;

    /* Detections info */
    uint16_t recognised_detections_capability;
    uint16_t num_detections;

    /* Detections array (external allocation recommended for large data) */
    bts_rdi_detection_t* detections;
} bts_rdi_message_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Initialize RDI message with default values
 * @param msg Pointer to message structure
 */
void bts_rdi_init(bts_rdi_message_t* msg);

/**
 * @brief Set interface version
 */
void bts_rdi_set_version(bts_rdi_message_t* msg, uint8_t major, uint8_t minor, uint8_t patch);

/**
 * @brief Set sensor information
 */
void bts_rdi_set_sensor_info(bts_rdi_message_t* msg, uint8_t sensor_id, uint8_t num_serving);

/**
 * @brief Set timestamp
 */
void bts_rdi_set_timestamp(bts_rdi_message_t* msg, uint64_t timestamp_ns);

/**
 * @brief Set message counter
 */
void bts_rdi_set_message_counter(bts_rdi_message_t* msg, uint32_t counter);

/**
 * @brief Set ambiguity domain (RadialVelocity begin/end)
 */
void bts_rdi_set_ambiguity_domain(bts_rdi_message_t* msg, const bts_rdi_ambiguity_domain_t* domain);

/**
 * @brief Set detection capability
 */
void bts_rdi_set_detection_capability(bts_rdi_message_t* msg, uint16_t capability);

/**
 * @brief Set detections array
 * @param msg Pointer to message structure
 * @param detections Pointer to detections array
 * @param count Number of detections
 * @note The detections pointer is stored, not copied. Caller must ensure lifetime.
 */
void bts_rdi_set_detections(bts_rdi_message_t* msg, bts_rdi_detection_t* detections, uint16_t count);

/**
 * @brief Calculate serialized size
 * @param msg Pointer to message structure
 * @return Size in bytes
 */
size_t bts_rdi_serialized_size(const bts_rdi_message_t* msg);

/**
 * @brief Serialize RDI message to buffer
 * @param msg Pointer to message structure
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes written, or negative error code
 */
int bts_rdi_serialize(const bts_rdi_message_t* msg, uint8_t* buffer, size_t buffer_size);

/**
 * @brief Initialize detection with default values
 */
void bts_rdi_detection_init(bts_rdi_detection_t* det);

#ifdef __cplusplus
}
#endif

#endif /* BTS_ISO23150_RDI_H */
