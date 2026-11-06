/**
 * @file bts_iso23150_pmoi.c
 * @brief Potentially Moving Object Interface (PMOI) serialization implementation - AFI920
 */

#include <string.h>
#include "bts_iso23150_pmoi.h"

/*============================================================================
 * Header Field Offsets (30 bytes)
 *============================================================================*/

#define OFF_PMOI_VERSION_MAJOR      0
#define OFF_PMOI_VERSION_MINOR      1
#define OFF_PMOI_VERSION_PATCH      2
#define OFF_PMOI_INTERFACE_ID       3
#define OFF_PMOI_NUM_SERVING        4
#define OFF_PMOI_SENSOR_ID          5
#define OFF_PMOI_TIMESTAMP          6
#define OFF_PMOI_MSG_COUNTER        14
#define OFF_PMOI_CYCLE_TIME         18
#define OFF_PMOI_CYCLE_VARIATION    22
#define OFF_PMOI_DATA_QUALIFIER     23
#define OFF_PMOI_MOTION_TYPE        24
#define OFF_PMOI_PMO_CAPABILITY     25
#define OFF_PMOI_PMO_STATUS         27
#define OFF_PMOI_NUM_PMO            28

/*============================================================================
 * Object Field Offsets (116 bytes, relative to object start)
 *============================================================================*/

/* Status (11B) */
#define OFF_OBJ_EXIST_PROB          0
#define OFF_OBJ_OBJECT_ID           4
#define OFF_OBJ_GROUPING_ID         5
#define OFF_OBJ_AGE                 6
#define OFF_OBJ_MEAS_STATUS         10
/* Classification (11B) */
#define OFF_OBJ_NUM_CLASS           11
#define OFF_OBJ_CLASS_BASE          12  /* 5 x {type u1, confidence u1} */
/* Position (25B) */
#define OFF_OBJ_REF_POINT           22
#define OFF_OBJ_POS_X               23
#define OFF_OBJ_POS_Y               27
#define OFF_OBJ_POS_Z               31
#define OFF_OBJ_POS_ERR_X           35
#define OFF_OBJ_POS_ERR_Y           39
#define OFF_OBJ_POS_ERR_Z           43
/* Orientation (4B) */
#define OFF_OBJ_ORIENT_YAW          47
/* Bounding box (12B) */
#define OFF_OBJ_BBOX_LENGTH         51
#define OFF_OBJ_BBOX_WIDTH          55
#define OFF_OBJ_BBOX_HEIGHT         59
/* Dynamics (49B) */
#define OFF_OBJ_VEL_X               63
#define OFF_OBJ_VEL_Y               67
#define OFF_OBJ_VEL_Z               71
#define OFF_OBJ_VEL_ERR_X           75
#define OFF_OBJ_VEL_ERR_Y           79
#define OFF_OBJ_VEL_ERR_Z           83
#define OFF_OBJ_ACC_X               87
#define OFF_OBJ_ACC_Y               91
#define OFF_OBJ_ACC_Z               95
#define OFF_OBJ_ACC_ERR_X           99
#define OFF_OBJ_ACC_ERR_Y           103
#define OFF_OBJ_ACC_ERR_Z           107
#define OFF_OBJ_MOVEMENT_STATUS     111
/* Radar-Specific (4B) */
#define OFF_OBJ_RCS                 112

/*============================================================================
 * Implementation
 *============================================================================*/

void bts_pmoi_init(bts_pmoi_message_t* msg)
{
    if (msg == NULL) return;

    memset(msg, 0, sizeof(*msg));

    msg->interface_version.major = 1;
    msg->interface_version.minor = 0;
    msg->interface_version.patch = 0;
    msg->interface_id = BTS_IID_POTENTIALLY_MOVING_OBJECTS;
    msg->num_serving_sensors = 1;
    msg->sensor_id = 0;
    msg->interface_cycle_time = 100000000; /* 100ms in ns */
    msg->interface_cycle_time_variation = 100;
    msg->data_qualifier = BTS_DQ_NORMAL;

    msg->motion_type = (uint8_t)BTS_PMOI_MT_RELATIVE;  /* radar relative motion (assumption) */
    msg->recognised_pmo_capability = BTS_PMOI_MAX_OBJECTS;
    msg->recognised_pmo_status = 0;
    msg->num_pmo = 0;
    msg->objects = NULL;
}

void bts_pmoi_set_version(bts_pmoi_message_t* msg, uint8_t major, uint8_t minor, uint8_t patch)
{
    if (msg == NULL) return;
    msg->interface_version.major = major;
    msg->interface_version.minor = minor;
    msg->interface_version.patch = patch;
}

void bts_pmoi_set_sensor_info(bts_pmoi_message_t* msg, uint8_t sensor_id, uint8_t num_serving)
{
    if (msg == NULL) return;
    msg->sensor_id = sensor_id;
    msg->num_serving_sensors = num_serving;
}

void bts_pmoi_set_timestamp(bts_pmoi_message_t* msg, uint64_t timestamp_ns)
{
    if (msg == NULL) return;
    msg->timestamp = timestamp_ns;
}

void bts_pmoi_set_message_counter(bts_pmoi_message_t* msg, uint32_t counter)
{
    if (msg == NULL) return;
    msg->message_counter = counter;
}

void bts_pmoi_set_capability(bts_pmoi_message_t* msg, uint16_t capability)
{
    if (msg == NULL) return;
    msg->recognised_pmo_capability = capability;
}

void bts_pmoi_set_objects(bts_pmoi_message_t* msg, bts_pmoi_object_t* objects, uint16_t count)
{
    if (msg == NULL) return;
    msg->objects = objects;
    msg->num_pmo = (count <= BTS_PMOI_MAX_OBJECTS) ? count : BTS_PMOI_MAX_OBJECTS;
}

size_t bts_pmoi_serialized_size(const bts_pmoi_message_t* msg)
{
    if (msg == NULL) return 0;
    return BTS_PMOI_HEADER_SIZE + (size_t)msg->num_pmo * BTS_PMOI_OBJECT_SIZE;
}

void bts_pmoi_object_init(bts_pmoi_object_t* obj)
{
    if (obj == NULL) return;
    memset(obj, 0, sizeof(*obj));
}

static int serialize_object(const bts_pmoi_object_t* obj, uint8_t* buf)
{
    uint32_t i;

    /* Status */
    bts_pack_f32(&buf[OFF_OBJ_EXIST_PROB], obj->existence_probability);
    buf[OFF_OBJ_OBJECT_ID]   = obj->object_id;
    buf[OFF_OBJ_GROUPING_ID] = obj->object_grouping_id;
    bts_pack_f32(&buf[OFF_OBJ_AGE], obj->age);
    buf[OFF_OBJ_MEAS_STATUS] = obj->measurement_status;

    /* Classification (fixed 5 slots; unused slots stay zero from serialize clear) */
    buf[OFF_OBJ_NUM_CLASS] = obj->num_classifications;
    for (i = 0; i < BTS_PMOI_MAX_CLASSIFICATIONS; i++) {
        buf[OFF_OBJ_CLASS_BASE + i * 2u]      = obj->classifications[i].type;
        buf[OFF_OBJ_CLASS_BASE + i * 2u + 1u] = obj->classifications[i].confidence;
    }

    /* Position */
    buf[OFF_OBJ_REF_POINT] = obj->reference_point;
    bts_pack_f32(&buf[OFF_OBJ_POS_X], obj->position_x);
    bts_pack_f32(&buf[OFF_OBJ_POS_Y], obj->position_y);
    bts_pack_f32(&buf[OFF_OBJ_POS_Z], obj->position_z);
    bts_pack_f32(&buf[OFF_OBJ_POS_ERR_X], obj->position_error_x);
    bts_pack_f32(&buf[OFF_OBJ_POS_ERR_Y], obj->position_error_y);
    bts_pack_f32(&buf[OFF_OBJ_POS_ERR_Z], obj->position_error_z);

    /* Orientation */
    bts_pack_f32(&buf[OFF_OBJ_ORIENT_YAW], obj->orientation_yaw);

    /* Bounding box (A.2.20 extent) */
    bts_pack_f32(&buf[OFF_OBJ_BBOX_LENGTH], obj->bounding_box_length);
    bts_pack_f32(&buf[OFF_OBJ_BBOX_WIDTH],  obj->bounding_box_width);
    bts_pack_f32(&buf[OFF_OBJ_BBOX_HEIGHT], obj->bounding_box_height);

    /* Dynamics */
    bts_pack_f32(&buf[OFF_OBJ_VEL_X], obj->velocity_x);
    bts_pack_f32(&buf[OFF_OBJ_VEL_Y], obj->velocity_y);
    bts_pack_f32(&buf[OFF_OBJ_VEL_Z], obj->velocity_z);
    bts_pack_f32(&buf[OFF_OBJ_VEL_ERR_X], obj->velocity_error_x);
    bts_pack_f32(&buf[OFF_OBJ_VEL_ERR_Y], obj->velocity_error_y);
    bts_pack_f32(&buf[OFF_OBJ_VEL_ERR_Z], obj->velocity_error_z);
    bts_pack_f32(&buf[OFF_OBJ_ACC_X], obj->acceleration_x);
    bts_pack_f32(&buf[OFF_OBJ_ACC_Y], obj->acceleration_y);
    bts_pack_f32(&buf[OFF_OBJ_ACC_Z], obj->acceleration_z);
    bts_pack_f32(&buf[OFF_OBJ_ACC_ERR_X], obj->acceleration_error_x);
    bts_pack_f32(&buf[OFF_OBJ_ACC_ERR_Y], obj->acceleration_error_y);
    bts_pack_f32(&buf[OFF_OBJ_ACC_ERR_Z], obj->acceleration_error_z);
    buf[OFF_OBJ_MOVEMENT_STATUS] = obj->movement_status;

    /* Radar-Specific */
    bts_pack_f32(&buf[OFF_OBJ_RCS], obj->entity_radar_cross_section);

    return BTS_PMOI_OBJECT_SIZE;
}

int bts_pmoi_serialize(const bts_pmoi_message_t* msg, uint8_t* buffer, size_t buffer_size)
{
    size_t required_size;
    uint16_t i;

    if (msg == NULL || buffer == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    required_size = bts_pmoi_serialized_size(msg);
    if (buffer_size < required_size) {
        return BTS_ISO23150_ERR_BUFFER_TOO_SMALL;
    }

    /* Clear buffer (zero-fills reserved/unused classification slots) */
    memset(buffer, 0, required_size);

    /* Header */
    buffer[OFF_PMOI_VERSION_MAJOR] = msg->interface_version.major;
    buffer[OFF_PMOI_VERSION_MINOR] = msg->interface_version.minor;
    buffer[OFF_PMOI_VERSION_PATCH] = msg->interface_version.patch;
    buffer[OFF_PMOI_INTERFACE_ID]  = msg->interface_id;
    buffer[OFF_PMOI_NUM_SERVING]   = msg->num_serving_sensors;
    buffer[OFF_PMOI_SENSOR_ID]     = msg->sensor_id;
    bts_pack_u64(&buffer[OFF_PMOI_TIMESTAMP], msg->timestamp);
    bts_pack_u32(&buffer[OFF_PMOI_MSG_COUNTER], msg->message_counter);
    bts_pack_u32(&buffer[OFF_PMOI_CYCLE_TIME], msg->interface_cycle_time);
    buffer[OFF_PMOI_CYCLE_VARIATION] = msg->interface_cycle_time_variation;
    buffer[OFF_PMOI_DATA_QUALIFIER]  = (uint8_t)msg->data_qualifier;

    /* PMOI List Header */
    buffer[OFF_PMOI_MOTION_TYPE] = msg->motion_type;
    bts_pack_u16(&buffer[OFF_PMOI_PMO_CAPABILITY], msg->recognised_pmo_capability);
    buffer[OFF_PMOI_PMO_STATUS] = msg->recognised_pmo_status;
    bts_pack_u16(&buffer[OFF_PMOI_NUM_PMO], msg->num_pmo);

    /* Objects */
    if (msg->objects != NULL) {
        for (i = 0; i < msg->num_pmo; i++) {
            uint8_t* obj_buf = &buffer[BTS_PMOI_HEADER_SIZE + (size_t)i * BTS_PMOI_OBJECT_SIZE];
            serialize_object(&msg->objects[i], obj_buf);
        }
    }

    return (int)required_size;
}
