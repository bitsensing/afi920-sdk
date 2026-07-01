/**
 * @file bts_iso23150_csii.c
 * @brief Common Sensor Input Interface (CSII) (de)serialization - AFI920
 */

#include "bts_iso23150_csii.h"
#include <string.h>

/*============================================================================
 * Defaults
 *============================================================================*/

void bts_csii_init(bts_csii_message_t* msg)
{
    if (msg == NULL) {
        return;
    }
    memset(msg, 0, sizeof(*msg));

    msg->interface_version.major = 1;
    msg->interface_version.minor = 0;
    msg->interface_version.patch = 0;
    msg->interface_id = (uint8_t)BTS_IID_COMMON_SENSOR_INPUT;
    msg->num_valid_serving_sensors = BTS_CSII_NUM_VALID_SERVING_SENSORS;
    msg->sensor_id = 0;
    msg->timestamp_measurement = 0;
    msg->message_counter = 0;
    msg->interface_cycle_time = 100000000u;     /* 100 ms */
    msg->interface_cycle_time_variation = 0;
    msg->data_qualifier = BTS_DQ_NORMAL;

    msg->somc = BTS_CSII_SOMC_NORMAL_MODE;
    msg->global_timestamp_utc = 0.0;

    msg->vehicle_coord_system = BTS_CSII_VCS_REAR_AXLE;
    msg->gear_position = BTS_CSII_GP_NEUTRAL;
}

size_t bts_csii_serialized_size(const bts_csii_message_t* msg)
{
    (void)msg;
    return BTS_CSII_SIZE;
}

/*============================================================================
 * Serialization (host publisher side)
 *============================================================================*/

int bts_csii_serialize(const bts_csii_message_t* msg,
                       uint8_t* buffer,
                       size_t buffer_size)
{
    uint8_t* p;

    if (msg == NULL || buffer == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }
    if (buffer_size < BTS_CSII_SIZE) {
        return BTS_ISO23150_ERR_BUFFER_TOO_SMALL;
    }

    p = buffer;

    /* CIH (24B) */
    *p++ = msg->interface_version.major;
    *p++ = msg->interface_version.minor;
    *p++ = msg->interface_version.patch;
    *p++ = msg->interface_id;
    *p++ = msg->num_valid_serving_sensors;
    *p++ = msg->sensor_id;
    bts_pack_u64(p, msg->timestamp_measurement);    p += 8;
    bts_pack_u32(p, msg->message_counter);          p += 4;
    bts_pack_u32(p, msg->interface_cycle_time);     p += 4;
    *p++ = msg->interface_cycle_time_variation;
    *p++ = (uint8_t)msg->data_qualifier;

    /* SOC (1B) */
    *p++ = (uint8_t)msg->somc;

    /* Environmental Info: Time (8B) */
    {
        uint64_t u;
        memcpy(&u, &msg->global_timestamp_utc, sizeof(u));
        bts_pack_u64(p, u);
        p += 8;
    }

    /* Vehicle State (46B) */
    *p++ = (uint8_t)msg->vehicle_coord_system;
    bts_pack_f32(p, msg->velocity_x_mps);           p += 4;
    bts_pack_f32(p, msg->velocity_y_mps);           p += 4;
    bts_pack_f32(p, msg->velocity_z_mps);           p += 4;
    bts_pack_f32(p, msg->rotation_rate_yaw_rps);    p += 4;
    bts_pack_f32(p, msg->rotation_rate_pitch_rps);  p += 4;
    bts_pack_f32(p, msg->rotation_rate_roll_rps);   p += 4;
    bts_pack_f32(p, msg->wheel_flp_rps);            p += 4;
    bts_pack_f32(p, msg->wheel_frp_rps);            p += 4;
    bts_pack_f32(p, msg->wheel_rlp_rps);            p += 4;
    bts_pack_f32(p, msg->wheel_rrp_rps);            p += 4;
    bts_pack_f32(p, msg->steering_angle_rad);       p += 4;
    *p++ = (uint8_t)msg->gear_position;

    return (int)(p - buffer);   /* 79 */
}

/*============================================================================
 * Deserialization (radar subscriber side)
 *============================================================================*/

int bts_csii_deserialize(const uint8_t* buffer,
                         size_t buffer_size,
                         bts_csii_message_t* msg)
{
    const uint8_t* p;

    if (buffer == NULL || msg == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }
    if (buffer_size != BTS_CSII_SIZE) {
        return BTS_ISO23150_ERR_DESERIALIZE_FAILED;
    }

    p = buffer;

    /* CIH (24B) */
    msg->interface_version.major = *p++;
    msg->interface_version.minor = *p++;
    msg->interface_version.patch = *p++;
    msg->interface_id            = *p++;
    msg->num_valid_serving_sensors = *p++;
    msg->sensor_id               = *p++;
    msg->timestamp_measurement   = bts_unpack_u64(p); p += 8;
    msg->message_counter         = bts_unpack_u32(p); p += 4;
    msg->interface_cycle_time    = bts_unpack_u32(p); p += 4;
    msg->interface_cycle_time_variation = *p++;
    msg->data_qualifier          = (bts_data_qualifier_t)(*p++);

    /* Validation: interface_id and num_serving must match expectation */
    if (msg->interface_id != (uint8_t)BTS_IID_COMMON_SENSOR_INPUT) {
        return BTS_ISO23150_ERR_INVALID_PARAM;
    }
    if (msg->num_valid_serving_sensors != BTS_CSII_NUM_VALID_SERVING_SENSORS) {
        return BTS_ISO23150_ERR_INVALID_PARAM;
    }

    /* SOC (1B) */
    msg->somc = (bts_csii_somc_t)(*p++);

    /* Environmental Info: Time (8B) */
    {
        uint64_t u = bts_unpack_u64(p);
        memcpy(&msg->global_timestamp_utc, &u, sizeof(double));
        p += 8;
    }

    /* Vehicle State (46B) */
    msg->vehicle_coord_system     = (bts_csii_vehicle_coord_system_t)(*p++);
    msg->velocity_x_mps           = bts_unpack_f32(p); p += 4;
    msg->velocity_y_mps           = bts_unpack_f32(p); p += 4;
    msg->velocity_z_mps           = bts_unpack_f32(p); p += 4;
    msg->rotation_rate_yaw_rps    = bts_unpack_f32(p); p += 4;
    msg->rotation_rate_pitch_rps  = bts_unpack_f32(p); p += 4;
    msg->rotation_rate_roll_rps   = bts_unpack_f32(p); p += 4;
    msg->wheel_flp_rps            = bts_unpack_f32(p); p += 4;
    msg->wheel_frp_rps            = bts_unpack_f32(p); p += 4;
    msg->wheel_rlp_rps            = bts_unpack_f32(p); p += 4;
    msg->wheel_rrp_rps            = bts_unpack_f32(p); p += 4;
    msg->steering_angle_rad       = bts_unpack_f32(p); p += 4;
    msg->gear_position            = (bts_csii_gear_position_t)(*p++);

    /* Sanity: consumed exactly BTS_CSII_SIZE bytes */
    if ((size_t)(p - buffer) != BTS_CSII_SIZE) {
        return BTS_ISO23150_ERR_DESERIALIZE_FAILED;
    }

    return BTS_ISO23150_OK;
}
