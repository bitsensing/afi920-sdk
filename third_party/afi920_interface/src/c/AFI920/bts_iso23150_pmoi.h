/**
 * @file bts_iso23150_pmoi.h
 * @brief Potentially Moving Object Interface (PMOI) serialization for ISO 23150 - AFI920
 */

#ifndef BTS_ISO23150_PMOI_H
#define BTS_ISO23150_PMOI_H

#include "../bts_iso23150.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *
 * PMOI Header (30B) = Common Header (24B) + PMOI List Header (6B:
 *   MotionType u8 + RecognisedPMOCapability u16 + RecognisedPMOStatus u8
 *   + NumberOfValidPMO u16).
 * PMOI Object Entity (116B) = Status (11) + Classification (11, fixed 5 slots)
 *   + Position (25) + Orientation (4) + BoundingBox (12) + Dynamics (49)
 *   + Radar-Specific (4).
 * Layout frozen against ISO 23150:2023 Table 9/10/11 (radar).
 * Authoritative spec: docs/04_Data_Stream/08_PMOI_Moving_Objects.md
 *============================================================================*/

#define BTS_PMOI_HEADER_SIZE            (30u)
#define BTS_PMOI_OBJECT_SIZE            (116u)
#define BTS_PMOI_MAX_OBJECTS            (255u)
#define BTS_PMOI_MAX_CLASSIFICATIONS    (5u)  /* fixed classification slots per object */
#define BTS_PMOI_MAX_PAYLOAD_SIZE       (BTS_PMOI_HEADER_SIZE + BTS_PMOI_OBJECT_SIZE * BTS_PMOI_MAX_OBJECTS)

/*============================================================================
 * Enums
 *============================================================================*/

/* MotionType (A.1.13) - interpretation of object motion signals */
typedef enum {
    BTS_PMOI_MT_RELATIVE            = 0,  /* relative motion in ego-vehicle frame */
    BTS_PMOI_MT_ABSOLUTE           = 1,  /* absolute motion in world frame */
} bts_pmoi_motion_type_t;

/* MeasurementStatusObjectLevel (A.2.9) */
typedef enum {
    BTS_PMOI_MS_MEASURED            = 0,
    BTS_PMOI_MS_NEW                 = 1,
    BTS_PMOI_MS_PREDICTED           = 2,
    BTS_PMOI_MS_PARTLY_MEASURED     = 3,
    BTS_PMOI_MS_INVALID             = 4,
    BTS_PMOI_MS_PREDICTED_OCCLUDED  = 5,
} bts_pmoi_measurement_status_t;

/* MovementStatus (A.2.32) */
typedef enum {
    BTS_PMOI_MOV_MOVING             = 0,
    BTS_PMOI_MOV_STOPPED_MOVING     = 1,
    BTS_PMOI_MOV_STATIONARY         = 2,
} bts_pmoi_movement_status_t;

/* PotentiallyMovingObjectClassificationType (A.2.12; 0-13 defined) */
typedef enum {
    BTS_PMOI_CT_CAR                 = 0,
    BTS_PMOI_CT_TRUCK               = 1,
    BTS_PMOI_CT_HEAVY_TRUCK         = 2,
    BTS_PMOI_CT_VAN                 = 3,
    BTS_PMOI_CT_BUS                 = 4,
    BTS_PMOI_CT_TRAILER             = 5,
    BTS_PMOI_CT_TRAIN               = 6,
    BTS_PMOI_CT_OTHER_VEHICLE       = 7,
    BTS_PMOI_CT_MOTORBIKE           = 8,
    BTS_PMOI_CT_ANIMAL              = 9,
    BTS_PMOI_CT_BICYCLE             = 10,
    BTS_PMOI_CT_TRICYCLE            = 11,
    BTS_PMOI_CT_PEDESTRIAN          = 12,
    BTS_PMOI_CT_WHEELCHAIR          = 13,
} bts_pmoi_classification_type_t;

/*============================================================================
 * PMOI Classification Entry (2 bytes)
 *============================================================================*/

typedef struct {
    uint8_t type;           /* bts_pmoi_classification_type_t */
    uint8_t confidence;     /* 0-100 % */
} bts_pmoi_classification_t;

/*============================================================================
 * PMOI Object Structure (116 bytes)
 *============================================================================*/

typedef struct {
    /* Status (11B) */
    float    existence_probability;         /* A.2.1  0-100 % */
    uint8_t  object_id;                     /* A.2.2 */
    uint8_t  object_grouping_id;            /* A.2.3 */
    float    age;                           /* A.2.4  seconds */
    uint8_t  measurement_status;            /* A.2.9  bts_pmoi_measurement_status_t */

    /* Classification (11B = count + fixed 5 slots) */
    uint8_t  num_classifications;           /* A.2.11 (0..BTS_PMOI_MAX_CLASSIFICATIONS) */
    bts_pmoi_classification_t classifications[BTS_PMOI_MAX_CLASSIFICATIONS]; /* A.2.12/13 */

    /* Position (25B) */
    uint8_t  reference_point;               /* A.2.14 bounding-box reference point */
    float    position_x;                    /* A.2.15 meters */
    float    position_y;
    float    position_z;
    float    position_error_x;              /* A.2.16 meters */
    float    position_error_y;
    float    position_error_z;

    /* Orientation (4B) */
    float    orientation_yaw;               /* A.2.17 radians */

    /* Bounding box (12B) - A.2.20 extent */
    float    bounding_box_length;           /* meters */
    float    bounding_box_width;            /* meters */
    float    bounding_box_height;           /* meters */

    /* Dynamics (49B) */
    float    velocity_x;                    /* A.2.24 m/s */
    float    velocity_y;
    float    velocity_z;
    float    velocity_error_x;              /* A.2.25 m/s */
    float    velocity_error_y;
    float    velocity_error_z;
    float    acceleration_x;                /* A.2.26 m/s^2 */
    float    acceleration_y;
    float    acceleration_z;
    float    acceleration_error_x;          /* A.2.27 m/s^2 */
    float    acceleration_error_y;
    float    acceleration_error_z;
    uint8_t  movement_status;               /* A.2.32 bts_pmoi_movement_status_t */

    /* Radar-Specific (4B) */
    float    entity_radar_cross_section;    /* A.2.71 dBsm */
} bts_pmoi_object_t;

/*============================================================================
 * PMOI Message Structure
 *============================================================================*/

typedef struct {
    /* Common Header */
    bts_interface_version_t interface_version;
    uint8_t  interface_id;
    uint8_t  num_serving_sensors;
    uint8_t  sensor_id;
    uint64_t timestamp;                     /* nanoseconds (prediction, A.1.5.1) */
    uint32_t message_counter;
    uint32_t interface_cycle_time;          /* nanoseconds */
    uint8_t  interface_cycle_time_variation;/* 0-100 % */
    bts_data_qualifier_t data_qualifier;

    /* PMOI List Header */
    uint8_t  motion_type;                   /* A.1.13 bts_pmoi_motion_type_t */
    uint16_t recognised_pmo_capability;     /* A.1.10.1 max recognisable objects */
    uint8_t  recognised_pmo_status;         /* A.1.11.1 processing status */
    uint16_t num_pmo;                       /* A.1.12.1 number of valid objects */

    /* Objects array (external allocation recommended for large data) */
    bts_pmoi_object_t* objects;
} bts_pmoi_message_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/** @brief Initialize PMOI message with default values */
void bts_pmoi_init(bts_pmoi_message_t* msg);

/** @brief Set interface version */
void bts_pmoi_set_version(bts_pmoi_message_t* msg, uint8_t major, uint8_t minor, uint8_t patch);

/** @brief Set sensor information */
void bts_pmoi_set_sensor_info(bts_pmoi_message_t* msg, uint8_t sensor_id, uint8_t num_serving);

/** @brief Set timestamp (nanoseconds) */
void bts_pmoi_set_timestamp(bts_pmoi_message_t* msg, uint64_t timestamp_ns);

/** @brief Set message counter */
void bts_pmoi_set_message_counter(bts_pmoi_message_t* msg, uint32_t counter);

/** @brief Set object recognition capability */
void bts_pmoi_set_capability(bts_pmoi_message_t* msg, uint16_t capability);

/**
 * @brief Set objects array
 * @note The objects pointer is stored, not copied. Caller must ensure lifetime.
 */
void bts_pmoi_set_objects(bts_pmoi_message_t* msg, bts_pmoi_object_t* objects, uint16_t count);

/** @brief Calculate serialized size */
size_t bts_pmoi_serialized_size(const bts_pmoi_message_t* msg);

/** @brief Serialize PMOI message to buffer. Returns bytes written or negative error. */
int bts_pmoi_serialize(const bts_pmoi_message_t* msg, uint8_t* buffer, size_t buffer_size);

/** @brief Initialize object with default values */
void bts_pmoi_object_init(bts_pmoi_object_t* obj);

#ifdef __cplusplus
}
#endif

#endif /* BTS_ISO23150_PMOI_H */
