/**
 * @file bts_iso23150_csii.h
 * @brief Common Sensor Input Interface (CSII) deserialization for ISO 23150 - AFI920
 *
 * Host -> Radar direction. The radar consumes vehicle state, sensor operation
 * command, and environmental time published by the host every 100ms.
 *
 * Wire format: 79 bytes fixed (NumberOfValidServingSensors = 1 by convention).
 * Endianness: little-endian payload (ARM native).
 */

#ifndef BTS_ISO23150_CSII_H
#define BTS_ISO23150_CSII_H

#include "../bts_iso23150.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define BTS_CSII_SIZE                       (79u)
#define BTS_CSII_NUM_VALID_SERVING_SENSORS  (1u)

/*============================================================================
 * Enums
 *============================================================================*/

typedef enum {
    BTS_CSII_SOMC_INITIALIZING          = 0,
    BTS_CSII_SOMC_OFF                   = 1,
    BTS_CSII_SOMC_DORMANCY              = 2,
    BTS_CSII_SOMC_LOW_POWER_MODE        = 3,
    BTS_CSII_SOMC_NORMAL_MODE           = 16,
    BTS_CSII_SOMC_NORMAL_MODE_SR        = 17,
    BTS_CSII_SOMC_NORMAL_MODE_LR        = 18,
    BTS_CSII_SOMC_NORMAL_MODE_SRLR      = 19,
    BTS_CSII_SOMC_NORMAL_MODE_USR       = 20,
    BTS_CSII_SOMC_START_CALIBRATION     = 32,
    BTS_CSII_SOMC_START_CLEANING        = 33,
} bts_csii_somc_t;

typedef enum {
    BTS_CSII_VCS_REAR_AXLE              = 0,
    BTS_CSII_VCS_ROAD_LEVEL             = 1,
} bts_csii_vehicle_coord_system_t;

typedef enum {
    BTS_CSII_GP_NEUTRAL                 = 0,
    BTS_CSII_GP_PARKING                 = 1,
    BTS_CSII_GP_FORWARD                 = 2,
    BTS_CSII_GP_REVERSE                 = 3,
    BTS_CSII_GP_LOW_GEAR                = 4,
    BTS_CSII_GP_2ND_GEAR                = 5,
    BTS_CSII_GP_3RD_GEAR                = 6,
    BTS_CSII_GP_4TH_GEAR                = 7,
    BTS_CSII_GP_5TH_GEAR                = 8,
    BTS_CSII_GP_6TH_GEAR                = 9,
} bts_csii_gear_position_t;

/*============================================================================
 * CSII Message Structure (matches schema/AFI920/csii.ksy)
 *============================================================================*/

typedef struct {
    /* --- Common Interface Header (24B, N=1) --- */
    bts_interface_version_t interface_version;          /* 3B */
    uint8_t  interface_id;                              /* 1B  — 0x0E IID_CommonSensorInput */
    uint8_t  num_valid_serving_sensors;                 /* 1B  — fixed 1 */
    uint8_t  sensor_id;                                 /* 1B */
    uint64_t timestamp_measurement;                     /* 8B  — PTP nanoseconds */
    uint32_t message_counter;                           /* 4B */
    uint32_t interface_cycle_time;                      /* 4B  — ns */
    uint8_t  interface_cycle_time_variation;            /* 1B  — % */
    bts_data_qualifier_t data_qualifier;                /* 1B */

    /* --- Sensor Operation Command (1B) --- */
    bts_csii_somc_t somc;                               /* 1B */

    /* --- Environmental Info: Time (8B) --- */
    double   global_timestamp_utc;                      /* 8B  — UTC seconds */

    /* --- Vehicle State (46B) --- */
    bts_csii_vehicle_coord_system_t vehicle_coord_system; /* 1B */
    float    velocity_x_mps;                            /* 4B */
    float    velocity_y_mps;                            /* 4B */
    float    velocity_z_mps;                            /* 4B */
    float    rotation_rate_yaw_rps;                     /* 4B */
    float    rotation_rate_pitch_rps;                   /* 4B */
    float    rotation_rate_roll_rps;                    /* 4B */
    float    wheel_flp_rps;                             /* 4B */
    float    wheel_frp_rps;                             /* 4B */
    float    wheel_rlp_rps;                             /* 4B */
    float    wheel_rrp_rps;                             /* 4B */
    float    steering_angle_rad;                        /* 4B */
    bts_csii_gear_position_t gear_position;             /* 1B */
} bts_csii_message_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Initialize CSII message with default values
 *  - interface_version = {1,0,0}
 *  - interface_id = 0x0E
 *  - num_valid_serving_sensors = 1
 *  - data_qualifier = DQ_Normal
 *  - somc = NormalMode
 *  - all numeric fields zeroed
 */
void bts_csii_init(bts_csii_message_t* msg);

/**
 * @brief Get serialized size (always BTS_CSII_SIZE = 79)
 */
size_t bts_csii_serialized_size(const bts_csii_message_t* msg);

/**
 * @brief Serialize CSII message to buffer (host side — publisher)
 *
 * @param msg         Source message
 * @param buffer      Output buffer
 * @param buffer_size Buffer capacity (must be >= 79)
 * @return Bytes written on success, negative bts_iso23150_error_t on failure
 */
int bts_csii_serialize(const bts_csii_message_t* msg,
                       uint8_t* buffer,
                       size_t buffer_size);

/**
 * @brief Deserialize CSII message from buffer (radar side — subscriber)
 *
 * Validates:
 *  - buffer size == 79B exactly
 *  - interface_id == 0x0E (IID_CommonSensorInput)
 *  - num_valid_serving_sensors == 1
 *
 * @param buffer      Wire payload (CSII portion, after the 16B SOME/IP header)
 * @param buffer_size Buffer size (must be == 79)
 * @param msg         Output message
 * @return BTS_ISO23150_OK on success, negative bts_iso23150_error_t on failure
 */
int bts_csii_deserialize(const uint8_t* buffer,
                         size_t buffer_size,
                         bts_csii_message_t* msg);

#ifdef __cplusplus
}
#endif

#endif /* BTS_ISO23150_CSII_H */
