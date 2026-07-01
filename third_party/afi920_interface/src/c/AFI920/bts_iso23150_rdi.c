/**
 * @file bts_iso23150_rdi.c
 * @brief Radar Detection Interface (RDI) serialization implementation - AFI920
 */

#include <string.h>
#include "bts_iso23150_rdi.h"

/*============================================================================
 * Header Field Offsets (36 bytes)
 *============================================================================*/

#define OFF_RDI_VERSION_MAJOR       0
#define OFF_RDI_VERSION_MINOR       1
#define OFF_RDI_VERSION_PATCH       2
#define OFF_RDI_INTERFACE_ID        3
#define OFF_RDI_NUM_SERVING         4
#define OFF_RDI_SENSOR_ID           5
#define OFF_RDI_TIMESTAMP           6
#define OFF_RDI_MSG_COUNTER         14
#define OFF_RDI_CYCLE_TIME          18
#define OFF_RDI_CYCLE_VARIATION     22
#define OFF_RDI_DATA_QUALIFIER      23
#define OFF_RDI_VEL_AMB_BEGIN       24
#define OFF_RDI_VEL_AMB_END         28
#define OFF_RDI_DET_CAPABILITY      32
#define OFF_RDI_NUM_DETECTIONS      34

/*============================================================================
 * Detection Field Offsets (51 bytes, relative to detection start)
 *============================================================================*/

#define OFF_DET_EXIST_PROB          0
#define OFF_DET_ID                  1
#define OFF_DET_OBJ_ID_REF          3
#define OFF_DET_TIMESTAMP_DIFF      5
#define OFF_DET_RCS                 9
#define OFF_DET_SNR                 13
#define OFF_DET_AMB_GROUP_ID        17
#define OFF_DET_POS_RADIAL          19
#define OFF_DET_POS_AZ              23
#define OFF_DET_POS_EL              27
#define OFF_DET_POS_RADIAL_ERR      31
#define OFF_DET_POS_AZ_ERR          35
#define OFF_DET_POS_EL_ERR          39
#define OFF_DET_VEL_RADIAL          43
#define OFF_DET_VEL_RADIAL_ERR      47

/*============================================================================
 * Implementation
 *============================================================================*/

void bts_rdi_init(bts_rdi_message_t* msg)
{
    if (msg == NULL) return;

    memset(msg, 0, sizeof(*msg));

    msg->interface_version.major = 1;
    msg->interface_version.minor = 0;
    msg->interface_version.patch = 0;
    msg->interface_id = BTS_IID_RADAR_DETECTION;
    msg->num_serving_sensors = 1;
    msg->sensor_id = 0;
    msg->interface_cycle_time = 100000000; /* 100ms in ns */
    msg->interface_cycle_time_variation = 100;
    msg->data_qualifier = BTS_DQ_NORMAL;

    /* Default ambiguity domain (RadialVelocity) */
    msg->ambiguity_domain.radial_velocity_begin = -88.89f;
    msg->ambiguity_domain.radial_velocity_end = 55.56f;

    msg->recognised_detections_capability = BTS_RDI_MAX_DETECTIONS;
    msg->num_detections = 0;
    msg->detections = NULL;
}

void bts_rdi_set_version(bts_rdi_message_t* msg, uint8_t major, uint8_t minor, uint8_t patch)
{
    if (msg == NULL) return;
    msg->interface_version.major = major;
    msg->interface_version.minor = minor;
    msg->interface_version.patch = patch;
}

void bts_rdi_set_sensor_info(bts_rdi_message_t* msg, uint8_t sensor_id, uint8_t num_serving)
{
    if (msg == NULL) return;
    msg->sensor_id = sensor_id;
    msg->num_serving_sensors = num_serving;
}

void bts_rdi_set_timestamp(bts_rdi_message_t* msg, uint64_t timestamp_ns)
{
    if (msg == NULL) return;
    msg->timestamp = timestamp_ns;
}

void bts_rdi_set_message_counter(bts_rdi_message_t* msg, uint32_t counter)
{
    if (msg == NULL) return;
    msg->message_counter = counter;
}

void bts_rdi_set_ambiguity_domain(bts_rdi_message_t* msg, const bts_rdi_ambiguity_domain_t* domain)
{
    if (msg == NULL || domain == NULL) return;
    msg->ambiguity_domain = *domain;
}

void bts_rdi_set_detection_capability(bts_rdi_message_t* msg, uint16_t capability)
{
    if (msg == NULL) return;
    msg->recognised_detections_capability = capability;
}

void bts_rdi_set_detections(bts_rdi_message_t* msg, bts_rdi_detection_t* detections, uint16_t count)
{
    if (msg == NULL) return;
    msg->detections = detections;
    msg->num_detections = (count <= BTS_RDI_MAX_DETECTIONS) ? count : BTS_RDI_MAX_DETECTIONS;
}

size_t bts_rdi_serialized_size(const bts_rdi_message_t* msg)
{
    if (msg == NULL) return 0;
    return BTS_RDI_HEADER_SIZE + (size_t)msg->num_detections * BTS_RDI_DETECTION_SIZE;
}

void bts_rdi_detection_init(bts_rdi_detection_t* det)
{
    if (det == NULL) return;
    memset(det, 0, sizeof(*det));
}

static int serialize_detection(const bts_rdi_detection_t* det, uint8_t* buf)
{
    /* Status */
    buf[OFF_DET_EXIST_PROB] = det->existence_probability;
    bts_pack_u16(&buf[OFF_DET_ID], det->detection_id);
    bts_pack_u16(&buf[OFF_DET_OBJ_ID_REF], det->object_id_reference);
    bts_pack_f32(&buf[OFF_DET_TIMESTAMP_DIFF], det->timestamp_difference);

    /* Information */
    bts_pack_f32(&buf[OFF_DET_RCS], det->radar_cross_section);
    bts_pack_f32(&buf[OFF_DET_SNR], det->signal_to_noise_ratio);
    bts_pack_u16(&buf[OFF_DET_AMB_GROUP_ID], det->ambiguity_grouping_id);

    /* Position */
    bts_pack_f32(&buf[OFF_DET_POS_RADIAL], det->position_radial_distance);
    bts_pack_f32(&buf[OFF_DET_POS_AZ], det->position_azimuth);
    bts_pack_f32(&buf[OFF_DET_POS_EL], det->position_elevation);
    bts_pack_f32(&buf[OFF_DET_POS_RADIAL_ERR], det->position_radial_distance_error);
    bts_pack_f32(&buf[OFF_DET_POS_AZ_ERR], det->position_azimuth_error);
    bts_pack_f32(&buf[OFF_DET_POS_EL_ERR], det->position_elevation_error);

    /* Dynamics */
    bts_pack_f32(&buf[OFF_DET_VEL_RADIAL], det->relative_velocity_radial);
    bts_pack_f32(&buf[OFF_DET_VEL_RADIAL_ERR], det->relative_velocity_radial_error);

    return BTS_RDI_DETECTION_SIZE;
}

int bts_rdi_serialize(const bts_rdi_message_t* msg, uint8_t* buffer, size_t buffer_size)
{
    if (msg == NULL || buffer == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    size_t required_size = bts_rdi_serialized_size(msg);
    if (buffer_size < required_size) {
        return BTS_ISO23150_ERR_BUFFER_TOO_SMALL;
    }

    /* Clear buffer */
    memset(buffer, 0, required_size);

    /* Header */
    buffer[OFF_RDI_VERSION_MAJOR] = msg->interface_version.major;
    buffer[OFF_RDI_VERSION_MINOR] = msg->interface_version.minor;
    buffer[OFF_RDI_VERSION_PATCH] = msg->interface_version.patch;
    buffer[OFF_RDI_INTERFACE_ID] = msg->interface_id;
    buffer[OFF_RDI_NUM_SERVING] = msg->num_serving_sensors;
    buffer[OFF_RDI_SENSOR_ID] = msg->sensor_id;
    bts_pack_u64(&buffer[OFF_RDI_TIMESTAMP], msg->timestamp);
    bts_pack_u32(&buffer[OFF_RDI_MSG_COUNTER], msg->message_counter);
    bts_pack_u32(&buffer[OFF_RDI_CYCLE_TIME], msg->interface_cycle_time);
    buffer[OFF_RDI_CYCLE_VARIATION] = msg->interface_cycle_time_variation;
    buffer[OFF_RDI_DATA_QUALIFIER] = (uint8_t)msg->data_qualifier;

    /* Ambiguity domain (RadialVelocity) */
    bts_pack_f32(&buffer[OFF_RDI_VEL_AMB_BEGIN], msg->ambiguity_domain.radial_velocity_begin);
    bts_pack_f32(&buffer[OFF_RDI_VEL_AMB_END], msg->ambiguity_domain.radial_velocity_end);

    /* Detection info */
    bts_pack_u16(&buffer[OFF_RDI_DET_CAPABILITY], msg->recognised_detections_capability);
    bts_pack_u16(&buffer[OFF_RDI_NUM_DETECTIONS], msg->num_detections);

    /* Detections */
    if (msg->detections != NULL) {
        for (uint16_t i = 0; i < msg->num_detections; i++) {
            uint8_t* det_buf = &buffer[BTS_RDI_HEADER_SIZE + i * BTS_RDI_DETECTION_SIZE];
            serialize_detection(&msg->detections[i], det_buf);
        }
    }

    return (int)required_size;
}
