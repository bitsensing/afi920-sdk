/**
 * @file bts_iso23150_spi.h
 * @brief Sensor Performance Interface (SPI) serialization for ISO 23150 - AFI920
 */

#ifndef BTS_ISO23150_SPI_H
#define BTS_ISO23150_SPI_H

#include "../bts_iso23150.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define BTS_SPI_HEADER_SIZE             (24u)
#define BTS_SPI_SENSOR_POSE_SIZE        (32u)
#define BTS_SPI_FOV_SEGMENT_SIZE        (17u)
#define BTS_SPI_OBJECT_TYPE_SIZE        (9u)
#define BTS_SPI_REF_TARGET_SIZE         (16u)

#define BTS_SPI_MAX_FOV_SEGMENTS        (16u)
#define BTS_SPI_MAX_OBJECT_TYPES        (16u)
#define BTS_SPI_MAX_REF_TARGETS         (16u)

/*============================================================================
 * Enums
 *============================================================================*/

typedef enum {
    BTS_SPI_VCST_REAR_AXLE              = 0,
    BTS_SPI_VCST_ROAD_LEVEL             = 1,
} bts_spi_vehicle_coordinate_system_t;

typedef enum {
    BTS_SPI_BS_NONE                     = 0,
    BTS_SPI_BS_PARTIAL_BLOCKAGE         = 1,
    BTS_SPI_BS_FULL_BLOCKAGE            = 2,
	BTS_SPI_BS_UNKNOWN					= 3,
} bts_spi_blockage_status_t;

typedef enum {
    BTS_SPI_ROT_CAR                     = 0,
    BTS_SPI_ROT_HEAVY_TRUCK             = 1,
    BTS_SPI_ROT_MOTORBIKE               = 2,
    BTS_SPI_ROT_BICYCLE                 = 3,
    BTS_SPI_ROT_PEDESTRIAN              = 4,
    BTS_SPI_ROT_POLE                    = 5,
    BTS_SPI_ROT_GUARD_RAIL              = 6,
    BTS_SPI_ROT_BUILDING                = 7,
    BTS_SPI_ROT_TRAFFIC_SIGN            = 8,
    BTS_SPI_ROT_TRAFFIC_LIGHT           = 9,
} bts_spi_recognised_object_type_t;

/*============================================================================
 * Data Structures
 *============================================================================*/

typedef struct {
    float origin_point_x;               /* meters */
    float origin_point_y;
    float origin_point_z;
    float orientation_yaw;              /* radians */
    float orientation_pitch;
    float orientation_roll;
    float orientation_error_yaw;        /* radians (optional) */
    float orientation_error_pitch;
} bts_spi_sensor_pose_t;

typedef struct {
    float azimuth_begin;                /* radians */
    float azimuth_end;
    float elevation_begin;              /* radians (optional) */
    float elevation_end;
    bts_spi_blockage_status_t blockage_status;
} bts_spi_fov_segment_t;

typedef struct {
    bts_spi_recognised_object_type_t object_type;
    float detection_range_begin;        /* meters */
    float detection_range_end;
} bts_spi_recognisable_object_type_t;

typedef struct {
    float radar_cross_section;          /* dBm^2 */
    float detection_range_begin;        /* meters */
    float detection_range_end;
    float signal_to_noise_ratio;        /* dB */
} bts_spi_reference_target_type_t;

/*============================================================================
 * SPI Message Structure
 *============================================================================*/

typedef struct {
    /* Header */
    bts_interface_version_t interface_version;
    uint8_t  interface_id;
    uint8_t  num_serving_sensors;
    uint8_t  sensor_id;
    uint64_t timestamp;                 /* nanoseconds */
    uint32_t message_counter;
    uint32_t interface_cycle_time;      /* nanoseconds */
    uint8_t  interface_cycle_time_variation;
    bts_data_qualifier_t data_qualifier;

    /* Vehicle Coordinate System */
    bts_spi_vehicle_coordinate_system_t vehicle_coordinate_system;

    /* Sensor Pose */
    bts_spi_sensor_pose_t sensor_pose;

    /* FOV Segments */
    uint8_t num_fov_segments;
    bts_spi_fov_segment_t fov_segments[BTS_SPI_MAX_FOV_SEGMENTS];

    /* Recognisable Object Types */
    uint8_t num_recognisable_object_types;
    bts_spi_recognisable_object_type_t recognisable_object_types[BTS_SPI_MAX_OBJECT_TYPES];

    /* Reference Target Types */
    uint8_t num_reference_target_types;
    bts_spi_reference_target_type_t reference_target_types[BTS_SPI_MAX_REF_TARGETS];
} bts_spi_message_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Initialize SPI message with default values
 */
void bts_spi_init(bts_spi_message_t* msg);

/**
 * @brief Set interface version
 */
void bts_spi_set_version(bts_spi_message_t* msg, uint8_t major, uint8_t minor, uint8_t patch);

/**
 * @brief Set sensor information
 */
void bts_spi_set_sensor_info(bts_spi_message_t* msg, uint8_t sensor_id, uint8_t num_serving);

/**
 * @brief Set timestamp
 */
void bts_spi_set_timestamp(bts_spi_message_t* msg, uint64_t timestamp_ns);

/**
 * @brief Set message counter
 */
void bts_spi_set_message_counter(bts_spi_message_t* msg, uint32_t counter);

/**
 * @brief Set sensor pose
 */
void bts_spi_set_sensor_pose(bts_spi_message_t* msg, const bts_spi_sensor_pose_t* pose);

/**
 * @brief Add FOV segment
 * @return Index of added segment, or negative error code
 */
int bts_spi_add_fov_segment(bts_spi_message_t* msg, const bts_spi_fov_segment_t* segment);

/**
 * @brief Add recognisable object type
 * @return Index of added type, or negative error code
 */
int bts_spi_add_recognisable_object_type(bts_spi_message_t* msg,
                                         const bts_spi_recognisable_object_type_t* obj_type);

/**
 * @brief Add reference target type
 * @return Index of added type, or negative error code
 */
int bts_spi_add_reference_target_type(bts_spi_message_t* msg,
                                      const bts_spi_reference_target_type_t* ref_target);

/**
 * @brief Calculate serialized size
 */
size_t bts_spi_serialized_size(const bts_spi_message_t* msg);

/**
 * @brief Serialize SPI message to buffer
 * @param msg Pointer to message structure
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes written, or negative error code
 */
int bts_spi_serialize(const bts_spi_message_t* msg, uint8_t* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* BTS_ISO23150_SPI_H */
