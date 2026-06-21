/**
 * @file bts_iso23150_spi.c
 * @brief Sensor Performance Interface (SPI) serialization implementation - AFI920
 */

#include <string.h>
#include "bts_iso23150_spi.h"

/*============================================================================
 * Header Field Offsets
 *============================================================================*/

#define OFF_SPI_VERSION_MAJOR           0
#define OFF_SPI_VERSION_MINOR           1
#define OFF_SPI_VERSION_PATCH           2
#define OFF_SPI_INTERFACE_ID            3
#define OFF_SPI_NUM_SERVING             4
#define OFF_SPI_SENSOR_ID               5
#define OFF_SPI_TIMESTAMP               6
#define OFF_SPI_MSG_COUNTER             14
#define OFF_SPI_CYCLE_TIME              18
#define OFF_SPI_CYCLE_VARIATION         22
#define OFF_SPI_DATA_QUALIFIER          23

/* After header (24 bytes) */
#define OFF_SPI_VEHICLE_COORD_SYS       24

/* Sensor pose (48 bytes starting at offset 25) */
#define OFF_SPI_POSE_START              25

/*============================================================================
 * Implementation
 *============================================================================*/

void bts_spi_init(bts_spi_message_t* msg)
{
    if (msg == NULL) return;

    memset(msg, 0, sizeof(*msg));

    msg->interface_version.major = 1;
    msg->interface_version.minor = 0;
    msg->interface_version.patch = 0;
    msg->interface_id = BTS_IID_SENSOR_PERFORMANCE;
    msg->num_serving_sensors = 1;
    msg->sensor_id = 0;
    msg->interface_cycle_time = 100000000; /* 100ms in ns */
    msg->interface_cycle_time_variation = 100;
    msg->data_qualifier = BTS_DQ_NORMAL;

    msg->vehicle_coordinate_system = BTS_SPI_VCST_REAR_AXLE;

    /* Default sensor pose (origin at sensor position) */
    msg->sensor_pose.origin_point_x = 0.0f;
    msg->sensor_pose.origin_point_y = 0.0f;
    msg->sensor_pose.origin_point_z = 0.0f;
    msg->sensor_pose.orientation_yaw = 0.0f;
    msg->sensor_pose.orientation_pitch = 0.0f;
    msg->sensor_pose.orientation_roll = 0.0f;
}

void bts_spi_set_version(bts_spi_message_t* msg, uint8_t major, uint8_t minor, uint8_t patch)
{
    if (msg == NULL) return;
    msg->interface_version.major = major;
    msg->interface_version.minor = minor;
    msg->interface_version.patch = patch;
}

void bts_spi_set_sensor_info(bts_spi_message_t* msg, uint8_t sensor_id, uint8_t num_serving)
{
    if (msg == NULL) return;
    msg->sensor_id = sensor_id;
    msg->num_serving_sensors = num_serving;
}

void bts_spi_set_timestamp(bts_spi_message_t* msg, uint64_t timestamp_ns)
{
    if (msg == NULL) return;
    msg->timestamp = timestamp_ns;
}

void bts_spi_set_message_counter(bts_spi_message_t* msg, uint32_t counter)
{
    if (msg == NULL) return;
    msg->message_counter = counter;
}

void bts_spi_set_sensor_pose(bts_spi_message_t* msg, const bts_spi_sensor_pose_t* pose)
{
    if (msg == NULL || pose == NULL) return;
    msg->sensor_pose = *pose;
}

int bts_spi_add_fov_segment(bts_spi_message_t* msg, const bts_spi_fov_segment_t* segment)
{
    if (msg == NULL || segment == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    if (msg->num_fov_segments >= BTS_SPI_MAX_FOV_SEGMENTS) {
        return BTS_ISO23150_ERR_INDEX_OUT_OF_RANGE;
    }

    msg->fov_segments[msg->num_fov_segments] = *segment;
    return msg->num_fov_segments++;
}

int bts_spi_add_recognisable_object_type(bts_spi_message_t* msg,
                                         const bts_spi_recognisable_object_type_t* obj_type)
{
    if (msg == NULL || obj_type == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    if (msg->num_recognisable_object_types >= BTS_SPI_MAX_OBJECT_TYPES) {
        return BTS_ISO23150_ERR_INDEX_OUT_OF_RANGE;
    }

    msg->recognisable_object_types[msg->num_recognisable_object_types] = *obj_type;
    return msg->num_recognisable_object_types++;
}

int bts_spi_add_reference_target_type(bts_spi_message_t* msg,
                                      const bts_spi_reference_target_type_t* ref_target)
{
    if (msg == NULL || ref_target == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    if (msg->num_reference_target_types >= BTS_SPI_MAX_REF_TARGETS) {
        return BTS_ISO23150_ERR_INDEX_OUT_OF_RANGE;
    }

    msg->reference_target_types[msg->num_reference_target_types] = *ref_target;
    return msg->num_reference_target_types++;
}

size_t bts_spi_serialized_size(const bts_spi_message_t* msg)
{
    if (msg == NULL) return 0;

    size_t size = BTS_SPI_HEADER_SIZE;               /* 24 bytes */
    size += 1;                                        /* Vehicle coordinate system */
    size += BTS_SPI_SENSOR_POSE_SIZE;                /* 48 bytes */
    size += 1;                                        /* num_fov_segments */
    size += (size_t)msg->num_fov_segments * BTS_SPI_FOV_SEGMENT_SIZE;
    size += 1;                                        /* num_recognisable_object_types */
    size += (size_t)msg->num_recognisable_object_types * BTS_SPI_OBJECT_TYPE_SIZE;
    size += 1;                                        /* num_reference_target_types */
    size += (size_t)msg->num_reference_target_types * BTS_SPI_REF_TARGET_SIZE;

    return size;
}

static size_t serialize_sensor_pose(const bts_spi_sensor_pose_t* pose, uint8_t* buf)
{
    size_t off = 0;

    bts_pack_f32(&buf[off], pose->origin_point_x); off += 4;
    bts_pack_f32(&buf[off], pose->origin_point_y); off += 4;
    bts_pack_f32(&buf[off], pose->origin_point_z); off += 4;
    bts_pack_f32(&buf[off], pose->orientation_yaw); off += 4;
    bts_pack_f32(&buf[off], pose->orientation_pitch); off += 4;
    bts_pack_f32(&buf[off], pose->orientation_roll); off += 4;
    bts_pack_f32(&buf[off], pose->orientation_error_yaw); off += 4;
    bts_pack_f32(&buf[off], pose->orientation_error_pitch); off += 4;

    return off; /* Should be 32 */
}

static size_t serialize_fov_segment(const bts_spi_fov_segment_t* seg, uint8_t* buf)
{
    size_t off = 0;

    bts_pack_f32(&buf[off], seg->azimuth_begin); off += 4;
    bts_pack_f32(&buf[off], seg->azimuth_end); off += 4;
    bts_pack_f32(&buf[off], seg->elevation_begin); off += 4;
    bts_pack_f32(&buf[off], seg->elevation_end); off += 4;
    buf[off++] = (uint8_t)seg->blockage_status;

    return off; /* Should be 17 */
}

static size_t serialize_object_type(const bts_spi_recognisable_object_type_t* obj, uint8_t* buf)
{
    size_t off = 0;

    buf[off++] = (uint8_t)obj->object_type;
    bts_pack_f32(&buf[off], obj->detection_range_begin); off += 4;
    bts_pack_f32(&buf[off], obj->detection_range_end); off += 4;

    return off; /* Should be 9 */
}

static size_t serialize_ref_target(const bts_spi_reference_target_type_t* ref, uint8_t* buf)
{
    size_t off = 0;

    bts_pack_f32(&buf[off], ref->radar_cross_section); off += 4;
    bts_pack_f32(&buf[off], ref->detection_range_begin); off += 4;
    bts_pack_f32(&buf[off], ref->detection_range_end); off += 4;
    bts_pack_f32(&buf[off], ref->signal_to_noise_ratio); off += 4;

    return off; /* Should be 16 */
}

int bts_spi_serialize(const bts_spi_message_t* msg, uint8_t* buffer, size_t buffer_size)
{
    if (msg == NULL || buffer == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    size_t required_size = bts_spi_serialized_size(msg);
    if (buffer_size < required_size) {
        return BTS_ISO23150_ERR_BUFFER_TOO_SMALL;
    }

    /* Clear buffer */
    memset(buffer, 0, required_size);

    size_t offset = 0;

    /* Header */
    buffer[OFF_SPI_VERSION_MAJOR] = msg->interface_version.major;
    buffer[OFF_SPI_VERSION_MINOR] = msg->interface_version.minor;
    buffer[OFF_SPI_VERSION_PATCH] = msg->interface_version.patch;
    buffer[OFF_SPI_INTERFACE_ID] = msg->interface_id;
    buffer[OFF_SPI_NUM_SERVING] = msg->num_serving_sensors;
    buffer[OFF_SPI_SENSOR_ID] = msg->sensor_id;
    bts_pack_u64(&buffer[OFF_SPI_TIMESTAMP], msg->timestamp);
    bts_pack_u32(&buffer[OFF_SPI_MSG_COUNTER], msg->message_counter);
    bts_pack_u32(&buffer[OFF_SPI_CYCLE_TIME], msg->interface_cycle_time);
    buffer[OFF_SPI_CYCLE_VARIATION] = msg->interface_cycle_time_variation;
    buffer[OFF_SPI_DATA_QUALIFIER] = (uint8_t)msg->data_qualifier;

    /* Vehicle coordinate system */
    buffer[OFF_SPI_VEHICLE_COORD_SYS] = (uint8_t)msg->vehicle_coordinate_system;

    /* Sensor pose */
    offset = OFF_SPI_POSE_START;
    offset += serialize_sensor_pose(&msg->sensor_pose, &buffer[offset]);

    /* FOV segments */
    buffer[offset++] = msg->num_fov_segments;
    for (uint8_t i = 0; i < msg->num_fov_segments; i++) {
        offset += serialize_fov_segment(&msg->fov_segments[i], &buffer[offset]);
    }

    /* Recognisable object types */
    buffer[offset++] = msg->num_recognisable_object_types;
    for (uint8_t i = 0; i < msg->num_recognisable_object_types; i++) {
        offset += serialize_object_type(&msg->recognisable_object_types[i], &buffer[offset]);
    }

    /* Reference target types */
    buffer[offset++] = msg->num_reference_target_types;
    for (uint8_t i = 0; i < msg->num_reference_target_types; i++) {
        offset += serialize_ref_target(&msg->reference_target_types[i], &buffer[offset]);
    }

    return (int)offset;
}
