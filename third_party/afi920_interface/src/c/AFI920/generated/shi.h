#ifndef SHI_H_
#define SHI_H_

// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

class shi_t;

#include "kaitai/kaitaistruct.h"
#include <stdint.h>
#include <set>
#include <vector>

#if KAITAI_STRUCT_VERSION < 11000L
#error "Incompatible Kaitai Struct C++/STL API: version 0.11 or later is required"
#endif

/**
 * ISO 23150 Sensor Health Information Interface (SHII) message payload.
 * Variable length: operation modes, input signals, and calibration
 * components are repeated per their num_valid_* count.
 */

class shi_t : public kaitai::kstruct {

public:

    enum data_qualifier_enum_t {
        DATA_QUALIFIER_ENUM_DQ_NORMAL = 0,
        DATA_QUALIFIER_ENUM_DQ_NOT_AVAILABLE = 1,
        DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_COVERAGE = 2,
        DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_PERFORMANCE = 3,
        DATA_QUALIFIER_ENUM_DQ_REDUCE_COVERAGE_AND_PERFORMANCE = 4,
        DATA_QUALIFIER_ENUM_DQ_TEST_MODE = 5,
        DATA_QUALIFIER_ENUM_DQ_INVALID = 6
    };
    static bool _is_defined_data_qualifier_enum_t(data_qualifier_enum_t v);

private:
    static const std::set<data_qualifier_enum_t> _values_data_qualifier_enum_t;
    static std::set<data_qualifier_enum_t> _build_values_data_qualifier_enum_t();

public:

    enum sensor_calibration_component_enum_t {
        SENSOR_CALIBRATION_COMPONENT_ENUM_SCC_INTRINSIC = 0,
        SENSOR_CALIBRATION_COMPONENT_ENUM_SCC_EXTRINSIC_ONLINE_HORIZONTAL = 1,
        SENSOR_CALIBRATION_COMPONENT_ENUM_SCC_EXTRINSIC_ONLINE_VERTICAL = 2
    };
    static bool _is_defined_sensor_calibration_component_enum_t(sensor_calibration_component_enum_t v);

private:
    static const std::set<sensor_calibration_component_enum_t> _values_sensor_calibration_component_enum_t;
    static std::set<sensor_calibration_component_enum_t> _build_values_sensor_calibration_component_enum_t();

public:

    enum sensor_calibration_state_enum_t {
        SENSOR_CALIBRATION_STATE_ENUM_CPS_INITIAL_CALIBRATION_PERFORMED = 0,
        SENSOR_CALIBRATION_STATE_ENUM_CPS_INITIAL_CALIBRATION_NOT_PERFORMED = 1,
        SENSOR_CALIBRATION_STATE_ENUM_CPS_INITIAL_CALIBRATION_FAILED = 2,
        SENSOR_CALIBRATION_STATE_ENUM_CPS_RECALIBRATION_NEEDED_INTRINSIC = 3,
        SENSOR_CALIBRATION_STATE_ENUM_CPS_RECALIBRATION_NEEDED_EXTRINSIC = 4,
        SENSOR_CALIBRATION_STATE_ENUM_CPS_RECALIBRATION_NEEDED_FULL = 5
    };
    static bool _is_defined_sensor_calibration_state_enum_t(sensor_calibration_state_enum_t v);

private:
    static const std::set<sensor_calibration_state_enum_t> _values_sensor_calibration_state_enum_t;
    static std::set<sensor_calibration_state_enum_t> _build_values_sensor_calibration_state_enum_t();

public:

    enum sensor_calibration_status_enum_t {
        SENSOR_CALIBRATION_STATUS_ENUM_SCS_CALIBRATED = 0,
        SENSOR_CALIBRATION_STATUS_ENUM_SCS_NOT_CALIBRATED = 1,
        SENSOR_CALIBRATION_STATUS_ENUM_SCS_DEGRADED = 2
    };
    static bool _is_defined_sensor_calibration_status_enum_t(sensor_calibration_status_enum_t v);

private:
    static const std::set<sensor_calibration_status_enum_t> _values_sensor_calibration_status_enum_t;
    static std::set<sensor_calibration_status_enum_t> _build_values_sensor_calibration_status_enum_t();

public:

    enum sensor_defect_reason_enum_t {
        SENSOR_DEFECT_REASON_ENUM_SDR_NO_DEFECT_RECOGNISED = 0,
        SENSOR_DEFECT_REASON_ENUM_SDR_INTERNAL_MEMORY_ERROR = 1,
        SENSOR_DEFECT_REASON_ENUM_SDR_HW_DEFECT = 2,
        SENSOR_DEFECT_REASON_ENUM_SDR_THERMAL_DEFECT = 3,
        SENSOR_DEFECT_REASON_ENUM_SDR_COMMUNICATION_ERROR = 4,
        SENSOR_DEFECT_REASON_ENUM_SDR_CALIBRATION_ERROR = 5,
        SENSOR_DEFECT_REASON_ENUM_SDR_CONFIGURATION_ERROR = 6,
        SENSOR_DEFECT_REASON_ENUM_SDR_MECHANICAL_DEFECT = 7,
        SENSOR_DEFECT_REASON_ENUM_SDR_SOFTWARE_DEFECT = 8,
        SENSOR_DEFECT_REASON_ENUM_SDR_COMPUTING_POWER_NOT_SUFFICIENT = 9,
        SENSOR_DEFECT_REASON_ENUM_SDR_OUT_OF_TIME_SYNCHRONISATION = 10,
        SENSOR_DEFECT_REASON_ENUM_SDR_SENSOR_EXTERNALLY_DISTURBED = 11
    };
    static bool _is_defined_sensor_defect_reason_enum_t(sensor_defect_reason_enum_t v);

private:
    static const std::set<sensor_defect_reason_enum_t> _values_sensor_defect_reason_enum_t;
    static std::set<sensor_defect_reason_enum_t> _build_values_sensor_defect_reason_enum_t();

public:

    enum sensor_defect_recognised_enum_t {
        SENSOR_DEFECT_RECOGNISED_ENUM_SDR_SENSOR_FULLY_FUNCTIONAL = 0,
        SENSOR_DEFECT_RECOGNISED_ENUM_SDR_NOT_FULLY_FUNCTIONAL_DUE_TO_DEFECT = 1,
        SENSOR_DEFECT_RECOGNISED_ENUM_SDR_OUT_OF_ORDER = 2
    };
    static bool _is_defined_sensor_defect_recognised_enum_t(sensor_defect_recognised_enum_t v);

private:
    static const std::set<sensor_defect_recognised_enum_t> _values_sensor_defect_recognised_enum_t;
    static std::set<sensor_defect_recognised_enum_t> _build_values_sensor_defect_recognised_enum_t();

public:

    enum sensor_input_signal_status_enum_t {
        SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_VALID = 0,
        SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_IMPLAUSIBLE = 1,
        SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_MISSING = 2,
        SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_OUT_OF_RANGE = 3,
        SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_TIMEOUT = 4
    };
    static bool _is_defined_sensor_input_signal_status_enum_t(sensor_input_signal_status_enum_t v);

private:
    static const std::set<sensor_input_signal_status_enum_t> _values_sensor_input_signal_status_enum_t;
    static std::set<sensor_input_signal_status_enum_t> _build_values_sensor_input_signal_status_enum_t();

public:

    enum sensor_input_signal_type_enum_t {
        SENSOR_INPUT_SIGNAL_TYPE_ENUM_SIST_CSII_SENSOR_OPERATION = 0,
        SENSOR_INPUT_SIGNAL_TYPE_ENUM_SIST_CSII_ENVIRONMENTAL_INFORMATION = 1,
        SENSOR_INPUT_SIGNAL_TYPE_ENUM_SIST_CSII_VEHICLE_STATE = 2,
        SENSOR_INPUT_SIGNAL_TYPE_ENUM_SIST_CSII_SENSOR_POSE = 3
    };
    static bool _is_defined_sensor_input_signal_type_enum_t(sensor_input_signal_type_enum_t v);

private:
    static const std::set<sensor_input_signal_type_enum_t> _values_sensor_input_signal_type_enum_t;
    static std::set<sensor_input_signal_type_enum_t> _build_values_sensor_input_signal_type_enum_t();

public:

    enum sensor_operation_mode_enum_t {
        SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_DUAL_RANGE = 0,
        SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_LONG_RANGE = 1,
        SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_MIDDLE_RANGE = 2,
        SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_SHORT_RANGE = 3,
        SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_ULTRA_LONG_RANGE = 4,
        SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_ULTRA_SHORT_RANGE = 5,
        SENSOR_OPERATION_MODE_ENUM_SOM_DEGRADATION_MODE = 10,
        SENSOR_OPERATION_MODE_ENUM_SOM_EVALUATION_MODE = 50,
        SENSOR_OPERATION_MODE_ENUM_SOM_CALIBRATION_MODE = 100,
        SENSOR_OPERATION_MODE_ENUM_SOM_INITIALISING = 200,
        SENSOR_OPERATION_MODE_ENUM_SOM_TEST_MODE = 201
    };
    static bool _is_defined_sensor_operation_mode_enum_t(sensor_operation_mode_enum_t v);

private:
    static const std::set<sensor_operation_mode_enum_t> _values_sensor_operation_mode_enum_t;
    static std::set<sensor_operation_mode_enum_t> _build_values_sensor_operation_mode_enum_t();

public:

    enum sensor_temperature_status_enum_t {
        SENSOR_TEMPERATURE_STATUS_ENUM_STS_UNDER_TEMPERATURE = 0,
        SENSOR_TEMPERATURE_STATUS_ENUM_STS_PRE_UNDER_TEMPERATURE = 1,
        SENSOR_TEMPERATURE_STATUS_ENUM_STS_TEMPERATURE_IN_LIMITS = 2,
        SENSOR_TEMPERATURE_STATUS_ENUM_STS_PRE_OVER_TEMPERATURE = 3,
        SENSOR_TEMPERATURE_STATUS_ENUM_STS_OVER_TEMPERATURE = 4
    };
    static bool _is_defined_sensor_temperature_status_enum_t(sensor_temperature_status_enum_t v);

private:
    static const std::set<sensor_temperature_status_enum_t> _values_sensor_temperature_status_enum_t;
    static std::set<sensor_temperature_status_enum_t> _build_values_sensor_temperature_status_enum_t();

public:

    enum sensor_time_sync_enum_t {
        SENSOR_TIME_SYNC_ENUM_STS_WITHIN_LIMITS = 0,
        SENSOR_TIME_SYNC_ENUM_STS_OUT_OF_LIMITS = 1,
        SENSOR_TIME_SYNC_ENUM_STS_TIMEOUT = 2,
        SENSOR_TIME_SYNC_ENUM_STS_NOT_SYNCHRONIZED = 3
    };
    static bool _is_defined_sensor_time_sync_enum_t(sensor_time_sync_enum_t v);

private:
    static const std::set<sensor_time_sync_enum_t> _values_sensor_time_sync_enum_t;
    static std::set<sensor_time_sync_enum_t> _build_values_sensor_time_sync_enum_t();

public:

    enum supply_voltage_status_enum_t {
        SUPPLY_VOLTAGE_STATUS_ENUM_SVS_LOW = 0,
        SUPPLY_VOLTAGE_STATUS_ENUM_SVS_PRE_LOW = 1,
        SUPPLY_VOLTAGE_STATUS_ENUM_SVS_WITHIN_LIMITS = 2,
        SUPPLY_VOLTAGE_STATUS_ENUM_SVS_PRE_HIGH = 3,
        SUPPLY_VOLTAGE_STATUS_ENUM_SVS_HIGH = 4
    };
    static bool _is_defined_supply_voltage_status_enum_t(supply_voltage_status_enum_t v);

private:
    static const std::set<supply_voltage_status_enum_t> _values_supply_voltage_status_enum_t;
    static std::set<supply_voltage_status_enum_t> _build_values_supply_voltage_status_enum_t();

public:

    shi_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent = 0, shi_t* p__root = 0);

private:
    void _read();
    void _clean_up();

public:
    ~shi_t();

private:
    uint8_t m_interface_version_major;
    uint8_t m_interface_version_minor;
    uint8_t m_interface_version_patch;
    uint8_t m_interface_id;
    uint8_t m_number_of_valid_serving_sensors;
    uint8_t m_sensor_id;
    uint64_t m_timestamp;
    uint32_t m_message_counter;
    uint32_t m_interface_cycle_time;
    uint8_t m_interface_cycle_time_variation;
    data_qualifier_enum_t m_data_qualifier;
    uint8_t m_number_of_valid_sensor_operation_modes;
    std::vector<uint8_t>* m_sensor_operation_modes;
    sensor_defect_recognised_enum_t m_sensor_defect_recognised;
    sensor_defect_reason_enum_t m_sensor_defect_reason;
    supply_voltage_status_enum_t m_supply_voltage_status;
    sensor_temperature_status_enum_t m_sensor_temperature_status;
    uint8_t m_number_of_valid_sensor_input_signal_statuses;
    std::vector<uint8_t>* m_sensor_input_signal_types;
    std::vector<uint8_t>* m_sensor_input_signal_statuses;
    sensor_time_sync_enum_t m_sensor_time_sync;
    uint8_t m_number_of_valid_sensor_calibration_components;
    std::vector<uint8_t>* m_sensor_calibration_components;
    std::vector<uint8_t>* m_sensor_calibration_statuses;
    std::vector<uint8_t>* m_sensor_calibration_states;
    shi_t* m__root;
    kaitai::kstruct* m__parent;

public:
    uint8_t interface_version_major() const { return m_interface_version_major; }
    uint8_t interface_version_minor() const { return m_interface_version_minor; }
    uint8_t interface_version_patch() const { return m_interface_version_patch; }

    /**
     * 0x0D = IID_SensorHealthInformation
     */
    uint8_t interface_id() const { return m_interface_id; }
    uint8_t number_of_valid_serving_sensors() const { return m_number_of_valid_serving_sensors; }
    uint8_t sensor_id() const { return m_sensor_id; }

    /**
     * Timestamp in nanoseconds
     */
    uint64_t timestamp() const { return m_timestamp; }
    uint32_t message_counter() const { return m_message_counter; }

    /**
     * Cycle time in nanoseconds (100ms = 100000000)
     */
    uint32_t interface_cycle_time() const { return m_interface_cycle_time; }

    /**
     * Variation in percent (0-100)
     */
    uint8_t interface_cycle_time_variation() const { return m_interface_cycle_time_variation; }
    data_qualifier_enum_t data_qualifier() const { return m_data_qualifier; }
    uint8_t number_of_valid_sensor_operation_modes() const { return m_number_of_valid_sensor_operation_modes; }

    /**
     * Array of sensor operation modes (enum sensor_operation_mode_enum)
     */
    std::vector<uint8_t>* sensor_operation_modes() const { return m_sensor_operation_modes; }
    sensor_defect_recognised_enum_t sensor_defect_recognised() const { return m_sensor_defect_recognised; }
    sensor_defect_reason_enum_t sensor_defect_reason() const { return m_sensor_defect_reason; }
    supply_voltage_status_enum_t supply_voltage_status() const { return m_supply_voltage_status; }
    sensor_temperature_status_enum_t sensor_temperature_status() const { return m_sensor_temperature_status; }
    uint8_t number_of_valid_sensor_input_signal_statuses() const { return m_number_of_valid_sensor_input_signal_statuses; }

    /**
     * Array of sensor input signal types (enum sensor_input_signal_type_enum)
     */
    std::vector<uint8_t>* sensor_input_signal_types() const { return m_sensor_input_signal_types; }

    /**
     * Array of sensor input signal statuses (enum sensor_input_signal_status_enum)
     */
    std::vector<uint8_t>* sensor_input_signal_statuses() const { return m_sensor_input_signal_statuses; }
    sensor_time_sync_enum_t sensor_time_sync() const { return m_sensor_time_sync; }
    uint8_t number_of_valid_sensor_calibration_components() const { return m_number_of_valid_sensor_calibration_components; }

    /**
     * Array of calibration component types (enum sensor_calibration_component_enum)
     */
    std::vector<uint8_t>* sensor_calibration_components() const { return m_sensor_calibration_components; }

    /**
     * Array of calibration statuses (enum sensor_calibration_status_enum)
     */
    std::vector<uint8_t>* sensor_calibration_statuses() const { return m_sensor_calibration_statuses; }

    /**
     * Array of calibration states (enum sensor_calibration_state_enum)
     */
    std::vector<uint8_t>* sensor_calibration_states() const { return m_sensor_calibration_states; }
    shi_t* _root() const { return m__root; }
    kaitai::kstruct* _parent() const { return m__parent; }
};

#endif  // SHI_H_
