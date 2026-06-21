/**
 * @file bts_iso23150_shi.c
 * @brief Sensor Health Information Interface (SHII) serialization implementation - AFI920
 *
 * Variable-length payload (arrays driven by num_valid_* counts).
 */

#include <string.h>
#include "bts_iso23150_shi.h"

/*============================================================================
 * Fixed common-header field offsets (24 bytes)
 *============================================================================*/

#define OFF_SHI_VERSION_MAJOR           0
#define OFF_SHI_VERSION_MINOR           1
#define OFF_SHI_VERSION_PATCH           2
#define OFF_SHI_INTERFACE_ID            3
#define OFF_SHI_NUM_SERVING             4
#define OFF_SHI_SENSOR_ID               5
#define OFF_SHI_TIMESTAMP               6
#define OFF_SHI_MSG_COUNTER             14
#define OFF_SHI_CYCLE_TIME              18
#define OFF_SHI_CYCLE_VARIATION         22
#define OFF_SHI_DATA_QUALIFIER          23
#define OFF_SHI_BODY_START              24

/*============================================================================
 * Implementation
 *============================================================================*/

void bts_shi_init(bts_shi_message_t* msg)
{
    if (msg == NULL) return;

    memset(msg, 0, sizeof(*msg));

    msg->interface_version.major = 1;
    msg->interface_version.minor = 0;
    msg->interface_version.patch = 0;
    msg->interface_id = BTS_IID_SENSOR_HEALTH_INFORMATION;
    msg->num_serving_sensors = 1;
    msg->sensor_id = 0;
    msg->interface_cycle_time = 100000000; /* 100ms in ns */
    msg->interface_cycle_time_variation = 100;
    msg->data_qualifier = BTS_DQ_NORMAL;

    /* Default status */
    msg->num_valid_operation_modes = 1;
    msg->sensor_operation_modes[0] = BTS_SHI_SOM_NORMAL_DUAL_RANGE;
    msg->sensor_defect_recognised = BTS_SHI_SDR_FULLY_FUNCTIONAL;
    msg->sensor_defect_reason = BTS_SHI_SDR_NO_DEFECT;
    msg->supply_voltage_status = BTS_SHI_SVS_WITHIN_LIMITS;
    msg->sensor_temperature_status = BTS_SHI_STS_TEMPERATURE_IN_LIMITS;

    /* Default input signal status */
    msg->num_valid_input_signal_statuses = 1;
    msg->sensor_input_signal_types[0] = BTS_SHI_SIST_CSII_VEHICLE_STATE;
    msg->sensor_input_signal_statuses[0] = BTS_SHI_SISS_VALID;

    /* Time sync */
    msg->sensor_time_sync = BTS_SHI_STS_WITHIN_LIMITS;

    /* Sensor calibration (default: 3 SCC components, calibrated) */
    msg->num_valid_calibration_components = 3;
    msg->sensor_calibration_components[0] = BTS_SHI_SCC_INTRINSIC;
    msg->sensor_calibration_components[1] = BTS_SHI_SCC_EXTRINSIC_ONLINE_HORIZONTAL;
    msg->sensor_calibration_components[2] = BTS_SHI_SCC_EXTRINSIC_ONLINE_VERTICAL;
    msg->sensor_calibration_statuses[0] = BTS_SHI_SCS_CALIBRATED;
    msg->sensor_calibration_statuses[1] = BTS_SHI_SCS_CALIBRATED;
    msg->sensor_calibration_statuses[2] = BTS_SHI_SCS_CALIBRATED;
    msg->sensor_calibration_states[0] = BTS_SHI_CPS_INITIAL_CAL_PERFORMED;
    msg->sensor_calibration_states[1] = BTS_SHI_CPS_INITIAL_CAL_PERFORMED;
    msg->sensor_calibration_states[2] = BTS_SHI_CPS_INITIAL_CAL_PERFORMED;
}

void bts_shi_set_version(bts_shi_message_t* msg, uint8_t major, uint8_t minor, uint8_t patch)
{
    if (msg == NULL) return;
    msg->interface_version.major = major;
    msg->interface_version.minor = minor;
    msg->interface_version.patch = patch;
}

void bts_shi_set_sensor_info(bts_shi_message_t* msg, uint8_t sensor_id, uint8_t num_serving)
{
    if (msg == NULL) return;
    msg->sensor_id = sensor_id;
    msg->num_serving_sensors = num_serving;
}

void bts_shi_set_timestamp(bts_shi_message_t* msg, uint64_t timestamp_ns)
{
    if (msg == NULL) return;
    msg->timestamp = timestamp_ns;
}

void bts_shi_set_message_counter(bts_shi_message_t* msg, uint32_t counter)
{
    if (msg == NULL) return;
    msg->message_counter = counter;
}

void bts_shi_set_operation_modes(bts_shi_message_t* msg, const uint8_t* modes, uint8_t count)
{
    if (msg == NULL || modes == NULL) return;

    uint8_t n = (count > BTS_SHI_MAX_OPERATION_MODES) ? BTS_SHI_MAX_OPERATION_MODES : count;
    msg->num_valid_operation_modes = n;

    memset(msg->sensor_operation_modes, 0, sizeof(msg->sensor_operation_modes));
    memcpy(msg->sensor_operation_modes, modes, n);
}

void bts_shi_set_defect_info(bts_shi_message_t* msg,
                             bts_shi_sensor_defect_recognised_t recognised,
                             bts_shi_sensor_defect_reason_t reason)
{
    if (msg == NULL) return;
    msg->sensor_defect_recognised = recognised;
    msg->sensor_defect_reason = reason;
}

void bts_shi_set_status(bts_shi_message_t* msg,
                        bts_shi_supply_voltage_status_t voltage,
                        bts_shi_sensor_temperature_status_t temperature)
{
    if (msg == NULL) return;
    msg->supply_voltage_status = voltage;
    msg->sensor_temperature_status = temperature;
}

void bts_shi_set_input_signal_statuses(bts_shi_message_t* msg,
                                       const uint8_t* types,
                                       const uint8_t* statuses,
                                       uint8_t count)
{
    if (msg == NULL || types == NULL || statuses == NULL) return;

    uint8_t n = (count > BTS_SHI_MAX_INPUT_SIGNALS) ? BTS_SHI_MAX_INPUT_SIGNALS : count;
    msg->num_valid_input_signal_statuses = n;

    memset(msg->sensor_input_signal_types, 0, sizeof(msg->sensor_input_signal_types));
    memset(msg->sensor_input_signal_statuses, 0, sizeof(msg->sensor_input_signal_statuses));
    memcpy(msg->sensor_input_signal_types, types, n);
    memcpy(msg->sensor_input_signal_statuses, statuses, n);
}

void bts_shi_set_time_sync(bts_shi_message_t* msg,
                           bts_shi_sensor_time_sync_t sync_status)
{
    if (msg == NULL) return;
    msg->sensor_time_sync = sync_status;
}

void bts_shi_set_calibration(bts_shi_message_t* msg,
                             const uint8_t* components,
                             const uint8_t* statuses,
                             const uint8_t* states,
                             uint8_t count)
{
    if (msg == NULL || components == NULL || statuses == NULL || states == NULL) return;

    uint8_t n = (count > BTS_SHI_MAX_CALIBRATION_COMPONENTS) ? BTS_SHI_MAX_CALIBRATION_COMPONENTS : count;
    msg->num_valid_calibration_components = n;

    memset(msg->sensor_calibration_components, 0, sizeof(msg->sensor_calibration_components));
    memset(msg->sensor_calibration_statuses, 0, sizeof(msg->sensor_calibration_statuses));
    memset(msg->sensor_calibration_states, 0, sizeof(msg->sensor_calibration_states));
    memcpy(msg->sensor_calibration_components, components, n);
    memcpy(msg->sensor_calibration_statuses, statuses, n);
    memcpy(msg->sensor_calibration_states, states, n);
}

size_t bts_shi_serialized_size(const bts_shi_message_t* msg)
{
    if (msg == NULL) return 0;

    uint8_t n_op  = (msg->num_valid_operation_modes > BTS_SHI_MAX_OPERATION_MODES)
                    ? BTS_SHI_MAX_OPERATION_MODES : msg->num_valid_operation_modes;
    uint8_t n_in  = (msg->num_valid_input_signal_statuses > BTS_SHI_MAX_INPUT_SIGNALS)
                    ? BTS_SHI_MAX_INPUT_SIGNALS : msg->num_valid_input_signal_statuses;
    uint8_t n_cal = (msg->num_valid_calibration_components > BTS_SHI_MAX_CALIBRATION_COMPONENTS)
                    ? BTS_SHI_MAX_CALIBRATION_COMPONENTS : msg->num_valid_calibration_components;

    /* 24 common + 1+n_op + 2 defect + 2 supply/temp + 1+2*n_in + 1 time_sync + 1+3*n_cal */
    return (size_t)24u + 1u + n_op + 2u + 2u + 1u + 2u * n_in + 1u + 1u + 3u * n_cal;
}

int bts_shi_serialize(const bts_shi_message_t* msg, uint8_t* buffer, size_t buffer_size)
{
    if (msg == NULL || buffer == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    size_t required = bts_shi_serialized_size(msg);
    if (buffer_size < required) {
        return BTS_ISO23150_ERR_BUFFER_TOO_SMALL;
    }

    memset(buffer, 0, required);

    /* Common header */
    buffer[OFF_SHI_VERSION_MAJOR] = msg->interface_version.major;
    buffer[OFF_SHI_VERSION_MINOR] = msg->interface_version.minor;
    buffer[OFF_SHI_VERSION_PATCH] = msg->interface_version.patch;
    buffer[OFF_SHI_INTERFACE_ID] = msg->interface_id;
    buffer[OFF_SHI_NUM_SERVING] = msg->num_serving_sensors;
    buffer[OFF_SHI_SENSOR_ID] = msg->sensor_id;
    bts_pack_u64(&buffer[OFF_SHI_TIMESTAMP], msg->timestamp);
    bts_pack_u32(&buffer[OFF_SHI_MSG_COUNTER], msg->message_counter);
    bts_pack_u32(&buffer[OFF_SHI_CYCLE_TIME], msg->interface_cycle_time);
    buffer[OFF_SHI_CYCLE_VARIATION] = msg->interface_cycle_time_variation;
    buffer[OFF_SHI_DATA_QUALIFIER] = (uint8_t)msg->data_qualifier;

    size_t off = OFF_SHI_BODY_START;

    uint8_t n_op  = (msg->num_valid_operation_modes > BTS_SHI_MAX_OPERATION_MODES)
                    ? BTS_SHI_MAX_OPERATION_MODES : msg->num_valid_operation_modes;
    uint8_t n_in  = (msg->num_valid_input_signal_statuses > BTS_SHI_MAX_INPUT_SIGNALS)
                    ? BTS_SHI_MAX_INPUT_SIGNALS : msg->num_valid_input_signal_statuses;
    uint8_t n_cal = (msg->num_valid_calibration_components > BTS_SHI_MAX_CALIBRATION_COMPONENTS)
                    ? BTS_SHI_MAX_CALIBRATION_COMPONENTS : msg->num_valid_calibration_components;

    /* Operation modes */
    buffer[off++] = n_op;
    memcpy(&buffer[off], msg->sensor_operation_modes, n_op);
    off += n_op;

    /* Defect */
    buffer[off++] = (uint8_t)msg->sensor_defect_recognised;
    buffer[off++] = (uint8_t)msg->sensor_defect_reason;

    /* Supply / temperature */
    buffer[off++] = (uint8_t)msg->supply_voltage_status;
    buffer[off++] = (uint8_t)msg->sensor_temperature_status;

    /* Input signal status */
    buffer[off++] = n_in;
    memcpy(&buffer[off], msg->sensor_input_signal_types, n_in);
    off += n_in;
    memcpy(&buffer[off], msg->sensor_input_signal_statuses, n_in);
    off += n_in;

    /* Time sync */
    buffer[off++] = (uint8_t)msg->sensor_time_sync;

    /* Sensor calibration */
    buffer[off++] = n_cal;
    memcpy(&buffer[off], msg->sensor_calibration_components, n_cal);
    off += n_cal;
    memcpy(&buffer[off], msg->sensor_calibration_statuses, n_cal);
    off += n_cal;
    memcpy(&buffer[off], msg->sensor_calibration_states, n_cal);
    off += n_cal;

    return (int)off;
}
