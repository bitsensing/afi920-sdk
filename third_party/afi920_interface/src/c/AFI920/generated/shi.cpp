// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

#include "shi.h"
std::set<shi_t::data_qualifier_enum_t> shi_t::_build_values_data_qualifier_enum_t() {
    std::set<shi_t::data_qualifier_enum_t> _t;
    _t.insert(shi_t::DATA_QUALIFIER_ENUM_DQ_NORMAL);
    _t.insert(shi_t::DATA_QUALIFIER_ENUM_DQ_NOT_AVAILABLE);
    _t.insert(shi_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_COVERAGE);
    _t.insert(shi_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_PERFORMANCE);
    _t.insert(shi_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_COVERAGE_AND_PERFORMANCE);
    _t.insert(shi_t::DATA_QUALIFIER_ENUM_DQ_TEST_MODE);
    _t.insert(shi_t::DATA_QUALIFIER_ENUM_DQ_INVALID);
    return _t;
}
const std::set<shi_t::data_qualifier_enum_t> shi_t::_values_data_qualifier_enum_t = shi_t::_build_values_data_qualifier_enum_t();
bool shi_t::_is_defined_data_qualifier_enum_t(shi_t::data_qualifier_enum_t v) {
    return shi_t::_values_data_qualifier_enum_t.find(v) != shi_t::_values_data_qualifier_enum_t.end();
}
std::set<shi_t::sensor_calibration_component_enum_t> shi_t::_build_values_sensor_calibration_component_enum_t() {
    std::set<shi_t::sensor_calibration_component_enum_t> _t;
    _t.insert(shi_t::SENSOR_CALIBRATION_COMPONENT_ENUM_SCC_INTRINSIC);
    _t.insert(shi_t::SENSOR_CALIBRATION_COMPONENT_ENUM_SCC_EXTRINSIC_ONLINE_HORIZONTAL);
    _t.insert(shi_t::SENSOR_CALIBRATION_COMPONENT_ENUM_SCC_EXTRINSIC_ONLINE_VERTICAL);
    return _t;
}
const std::set<shi_t::sensor_calibration_component_enum_t> shi_t::_values_sensor_calibration_component_enum_t = shi_t::_build_values_sensor_calibration_component_enum_t();
bool shi_t::_is_defined_sensor_calibration_component_enum_t(shi_t::sensor_calibration_component_enum_t v) {
    return shi_t::_values_sensor_calibration_component_enum_t.find(v) != shi_t::_values_sensor_calibration_component_enum_t.end();
}
std::set<shi_t::sensor_calibration_state_enum_t> shi_t::_build_values_sensor_calibration_state_enum_t() {
    std::set<shi_t::sensor_calibration_state_enum_t> _t;
    _t.insert(shi_t::SENSOR_CALIBRATION_STATE_ENUM_CPS_INITIAL_CALIBRATION_PERFORMED);
    _t.insert(shi_t::SENSOR_CALIBRATION_STATE_ENUM_CPS_INITIAL_CALIBRATION_NOT_PERFORMED);
    _t.insert(shi_t::SENSOR_CALIBRATION_STATE_ENUM_CPS_INITIAL_CALIBRATION_FAILED);
    _t.insert(shi_t::SENSOR_CALIBRATION_STATE_ENUM_CPS_RECALIBRATION_NEEDED_INTRINSIC);
    _t.insert(shi_t::SENSOR_CALIBRATION_STATE_ENUM_CPS_RECALIBRATION_NEEDED_EXTRINSIC);
    _t.insert(shi_t::SENSOR_CALIBRATION_STATE_ENUM_CPS_RECALIBRATION_NEEDED_FULL);
    return _t;
}
const std::set<shi_t::sensor_calibration_state_enum_t> shi_t::_values_sensor_calibration_state_enum_t = shi_t::_build_values_sensor_calibration_state_enum_t();
bool shi_t::_is_defined_sensor_calibration_state_enum_t(shi_t::sensor_calibration_state_enum_t v) {
    return shi_t::_values_sensor_calibration_state_enum_t.find(v) != shi_t::_values_sensor_calibration_state_enum_t.end();
}
std::set<shi_t::sensor_calibration_status_enum_t> shi_t::_build_values_sensor_calibration_status_enum_t() {
    std::set<shi_t::sensor_calibration_status_enum_t> _t;
    _t.insert(shi_t::SENSOR_CALIBRATION_STATUS_ENUM_SCS_CALIBRATED);
    _t.insert(shi_t::SENSOR_CALIBRATION_STATUS_ENUM_SCS_NOT_CALIBRATED);
    _t.insert(shi_t::SENSOR_CALIBRATION_STATUS_ENUM_SCS_DEGRADED);
    return _t;
}
const std::set<shi_t::sensor_calibration_status_enum_t> shi_t::_values_sensor_calibration_status_enum_t = shi_t::_build_values_sensor_calibration_status_enum_t();
bool shi_t::_is_defined_sensor_calibration_status_enum_t(shi_t::sensor_calibration_status_enum_t v) {
    return shi_t::_values_sensor_calibration_status_enum_t.find(v) != shi_t::_values_sensor_calibration_status_enum_t.end();
}
std::set<shi_t::sensor_defect_reason_enum_t> shi_t::_build_values_sensor_defect_reason_enum_t() {
    std::set<shi_t::sensor_defect_reason_enum_t> _t;
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_NO_DEFECT_RECOGNISED);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_INTERNAL_MEMORY_ERROR);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_HW_DEFECT);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_THERMAL_DEFECT);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_COMMUNICATION_ERROR);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_CALIBRATION_ERROR);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_CONFIGURATION_ERROR);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_MECHANICAL_DEFECT);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_SOFTWARE_DEFECT);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_COMPUTING_POWER_NOT_SUFFICIENT);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_OUT_OF_TIME_SYNCHRONISATION);
    _t.insert(shi_t::SENSOR_DEFECT_REASON_ENUM_SDR_SENSOR_EXTERNALLY_DISTURBED);
    return _t;
}
const std::set<shi_t::sensor_defect_reason_enum_t> shi_t::_values_sensor_defect_reason_enum_t = shi_t::_build_values_sensor_defect_reason_enum_t();
bool shi_t::_is_defined_sensor_defect_reason_enum_t(shi_t::sensor_defect_reason_enum_t v) {
    return shi_t::_values_sensor_defect_reason_enum_t.find(v) != shi_t::_values_sensor_defect_reason_enum_t.end();
}
std::set<shi_t::sensor_defect_recognised_enum_t> shi_t::_build_values_sensor_defect_recognised_enum_t() {
    std::set<shi_t::sensor_defect_recognised_enum_t> _t;
    _t.insert(shi_t::SENSOR_DEFECT_RECOGNISED_ENUM_SDR_SENSOR_FULLY_FUNCTIONAL);
    _t.insert(shi_t::SENSOR_DEFECT_RECOGNISED_ENUM_SDR_NOT_FULLY_FUNCTIONAL_DUE_TO_DEFECT);
    _t.insert(shi_t::SENSOR_DEFECT_RECOGNISED_ENUM_SDR_OUT_OF_ORDER);
    return _t;
}
const std::set<shi_t::sensor_defect_recognised_enum_t> shi_t::_values_sensor_defect_recognised_enum_t = shi_t::_build_values_sensor_defect_recognised_enum_t();
bool shi_t::_is_defined_sensor_defect_recognised_enum_t(shi_t::sensor_defect_recognised_enum_t v) {
    return shi_t::_values_sensor_defect_recognised_enum_t.find(v) != shi_t::_values_sensor_defect_recognised_enum_t.end();
}
std::set<shi_t::sensor_input_signal_status_enum_t> shi_t::_build_values_sensor_input_signal_status_enum_t() {
    std::set<shi_t::sensor_input_signal_status_enum_t> _t;
    _t.insert(shi_t::SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_VALID);
    _t.insert(shi_t::SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_IMPLAUSIBLE);
    _t.insert(shi_t::SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_MISSING);
    _t.insert(shi_t::SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_OUT_OF_RANGE);
    _t.insert(shi_t::SENSOR_INPUT_SIGNAL_STATUS_ENUM_SISS_TIMEOUT);
    return _t;
}
const std::set<shi_t::sensor_input_signal_status_enum_t> shi_t::_values_sensor_input_signal_status_enum_t = shi_t::_build_values_sensor_input_signal_status_enum_t();
bool shi_t::_is_defined_sensor_input_signal_status_enum_t(shi_t::sensor_input_signal_status_enum_t v) {
    return shi_t::_values_sensor_input_signal_status_enum_t.find(v) != shi_t::_values_sensor_input_signal_status_enum_t.end();
}
std::set<shi_t::sensor_input_signal_type_enum_t> shi_t::_build_values_sensor_input_signal_type_enum_t() {
    std::set<shi_t::sensor_input_signal_type_enum_t> _t;
    _t.insert(shi_t::SENSOR_INPUT_SIGNAL_TYPE_ENUM_SIST_CSII_SENSOR_OPERATION);
    _t.insert(shi_t::SENSOR_INPUT_SIGNAL_TYPE_ENUM_SIST_CSII_ENVIRONMENTAL_INFORMATION);
    _t.insert(shi_t::SENSOR_INPUT_SIGNAL_TYPE_ENUM_SIST_CSII_VEHICLE_STATE);
    _t.insert(shi_t::SENSOR_INPUT_SIGNAL_TYPE_ENUM_SIST_CSII_SENSOR_POSE);
    return _t;
}
const std::set<shi_t::sensor_input_signal_type_enum_t> shi_t::_values_sensor_input_signal_type_enum_t = shi_t::_build_values_sensor_input_signal_type_enum_t();
bool shi_t::_is_defined_sensor_input_signal_type_enum_t(shi_t::sensor_input_signal_type_enum_t v) {
    return shi_t::_values_sensor_input_signal_type_enum_t.find(v) != shi_t::_values_sensor_input_signal_type_enum_t.end();
}
std::set<shi_t::sensor_operation_mode_enum_t> shi_t::_build_values_sensor_operation_mode_enum_t() {
    std::set<shi_t::sensor_operation_mode_enum_t> _t;
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_DUAL_RANGE);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_LONG_RANGE);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_MIDDLE_RANGE);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_SHORT_RANGE);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_ULTRA_LONG_RANGE);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_NORMAL_ULTRA_SHORT_RANGE);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_DEGRADATION_MODE);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_EVALUATION_MODE);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_CALIBRATION_MODE);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_INITIALISING);
    _t.insert(shi_t::SENSOR_OPERATION_MODE_ENUM_SOM_TEST_MODE);
    return _t;
}
const std::set<shi_t::sensor_operation_mode_enum_t> shi_t::_values_sensor_operation_mode_enum_t = shi_t::_build_values_sensor_operation_mode_enum_t();
bool shi_t::_is_defined_sensor_operation_mode_enum_t(shi_t::sensor_operation_mode_enum_t v) {
    return shi_t::_values_sensor_operation_mode_enum_t.find(v) != shi_t::_values_sensor_operation_mode_enum_t.end();
}
std::set<shi_t::sensor_temperature_status_enum_t> shi_t::_build_values_sensor_temperature_status_enum_t() {
    std::set<shi_t::sensor_temperature_status_enum_t> _t;
    _t.insert(shi_t::SENSOR_TEMPERATURE_STATUS_ENUM_STS_UNDER_TEMPERATURE);
    _t.insert(shi_t::SENSOR_TEMPERATURE_STATUS_ENUM_STS_PRE_UNDER_TEMPERATURE);
    _t.insert(shi_t::SENSOR_TEMPERATURE_STATUS_ENUM_STS_TEMPERATURE_IN_LIMITS);
    _t.insert(shi_t::SENSOR_TEMPERATURE_STATUS_ENUM_STS_PRE_OVER_TEMPERATURE);
    _t.insert(shi_t::SENSOR_TEMPERATURE_STATUS_ENUM_STS_OVER_TEMPERATURE);
    return _t;
}
const std::set<shi_t::sensor_temperature_status_enum_t> shi_t::_values_sensor_temperature_status_enum_t = shi_t::_build_values_sensor_temperature_status_enum_t();
bool shi_t::_is_defined_sensor_temperature_status_enum_t(shi_t::sensor_temperature_status_enum_t v) {
    return shi_t::_values_sensor_temperature_status_enum_t.find(v) != shi_t::_values_sensor_temperature_status_enum_t.end();
}
std::set<shi_t::sensor_time_sync_enum_t> shi_t::_build_values_sensor_time_sync_enum_t() {
    std::set<shi_t::sensor_time_sync_enum_t> _t;
    _t.insert(shi_t::SENSOR_TIME_SYNC_ENUM_STS_WITHIN_LIMITS);
    _t.insert(shi_t::SENSOR_TIME_SYNC_ENUM_STS_OUT_OF_LIMITS);
    _t.insert(shi_t::SENSOR_TIME_SYNC_ENUM_STS_TIMEOUT);
    _t.insert(shi_t::SENSOR_TIME_SYNC_ENUM_STS_NOT_SYNCHRONIZED);
    return _t;
}
const std::set<shi_t::sensor_time_sync_enum_t> shi_t::_values_sensor_time_sync_enum_t = shi_t::_build_values_sensor_time_sync_enum_t();
bool shi_t::_is_defined_sensor_time_sync_enum_t(shi_t::sensor_time_sync_enum_t v) {
    return shi_t::_values_sensor_time_sync_enum_t.find(v) != shi_t::_values_sensor_time_sync_enum_t.end();
}
std::set<shi_t::supply_voltage_status_enum_t> shi_t::_build_values_supply_voltage_status_enum_t() {
    std::set<shi_t::supply_voltage_status_enum_t> _t;
    _t.insert(shi_t::SUPPLY_VOLTAGE_STATUS_ENUM_SVS_LOW);
    _t.insert(shi_t::SUPPLY_VOLTAGE_STATUS_ENUM_SVS_PRE_LOW);
    _t.insert(shi_t::SUPPLY_VOLTAGE_STATUS_ENUM_SVS_WITHIN_LIMITS);
    _t.insert(shi_t::SUPPLY_VOLTAGE_STATUS_ENUM_SVS_PRE_HIGH);
    _t.insert(shi_t::SUPPLY_VOLTAGE_STATUS_ENUM_SVS_HIGH);
    return _t;
}
const std::set<shi_t::supply_voltage_status_enum_t> shi_t::_values_supply_voltage_status_enum_t = shi_t::_build_values_supply_voltage_status_enum_t();
bool shi_t::_is_defined_supply_voltage_status_enum_t(shi_t::supply_voltage_status_enum_t v) {
    return shi_t::_values_supply_voltage_status_enum_t.find(v) != shi_t::_values_supply_voltage_status_enum_t.end();
}

shi_t::shi_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent, shi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root ? p__root : this;
    m_sensor_operation_modes = 0;
    m_sensor_input_signal_types = 0;
    m_sensor_input_signal_statuses = 0;
    m_sensor_calibration_components = 0;
    m_sensor_calibration_statuses = 0;
    m_sensor_calibration_states = 0;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void shi_t::_read() {
    m_interface_version_major = m__io->read_u1();
    m_interface_version_minor = m__io->read_u1();
    m_interface_version_patch = m__io->read_u1();
    m_interface_id = m__io->read_u1();
    m_number_of_valid_serving_sensors = m__io->read_u1();
    m_sensor_id = m__io->read_u1();
    m_timestamp = m__io->read_u8le();
    m_message_counter = m__io->read_u4le();
    m_interface_cycle_time = m__io->read_u4le();
    m_interface_cycle_time_variation = m__io->read_u1();
    m_data_qualifier = static_cast<shi_t::data_qualifier_enum_t>(m__io->read_u1());
    m_number_of_valid_sensor_operation_modes = m__io->read_u1();
    m_sensor_operation_modes = new std::vector<uint8_t>();
    const int l_sensor_operation_modes = number_of_valid_sensor_operation_modes();
    for (int i = 0; i < l_sensor_operation_modes; i++) {
        m_sensor_operation_modes->push_back(m__io->read_u1());
    }
    m_sensor_defect_recognised = static_cast<shi_t::sensor_defect_recognised_enum_t>(m__io->read_u1());
    m_sensor_defect_reason = static_cast<shi_t::sensor_defect_reason_enum_t>(m__io->read_u1());
    m_supply_voltage_status = static_cast<shi_t::supply_voltage_status_enum_t>(m__io->read_u1());
    m_sensor_temperature_status = static_cast<shi_t::sensor_temperature_status_enum_t>(m__io->read_u1());
    m_number_of_valid_sensor_input_signal_statuses = m__io->read_u1();
    m_sensor_input_signal_types = new std::vector<uint8_t>();
    const int l_sensor_input_signal_types = number_of_valid_sensor_input_signal_statuses();
    for (int i = 0; i < l_sensor_input_signal_types; i++) {
        m_sensor_input_signal_types->push_back(m__io->read_u1());
    }
    m_sensor_input_signal_statuses = new std::vector<uint8_t>();
    const int l_sensor_input_signal_statuses = number_of_valid_sensor_input_signal_statuses();
    for (int i = 0; i < l_sensor_input_signal_statuses; i++) {
        m_sensor_input_signal_statuses->push_back(m__io->read_u1());
    }
    m_sensor_time_sync = static_cast<shi_t::sensor_time_sync_enum_t>(m__io->read_u1());
    m_number_of_valid_sensor_calibration_components = m__io->read_u1();
    m_sensor_calibration_components = new std::vector<uint8_t>();
    const int l_sensor_calibration_components = number_of_valid_sensor_calibration_components();
    for (int i = 0; i < l_sensor_calibration_components; i++) {
        m_sensor_calibration_components->push_back(m__io->read_u1());
    }
    m_sensor_calibration_statuses = new std::vector<uint8_t>();
    const int l_sensor_calibration_statuses = number_of_valid_sensor_calibration_components();
    for (int i = 0; i < l_sensor_calibration_statuses; i++) {
        m_sensor_calibration_statuses->push_back(m__io->read_u1());
    }
    m_sensor_calibration_states = new std::vector<uint8_t>();
    const int l_sensor_calibration_states = number_of_valid_sensor_calibration_components();
    for (int i = 0; i < l_sensor_calibration_states; i++) {
        m_sensor_calibration_states->push_back(m__io->read_u1());
    }
}

shi_t::~shi_t() {
    _clean_up();
}

void shi_t::_clean_up() {
    if (m_sensor_operation_modes) {
        delete m_sensor_operation_modes; m_sensor_operation_modes = 0;
    }
    if (m_sensor_input_signal_types) {
        delete m_sensor_input_signal_types; m_sensor_input_signal_types = 0;
    }
    if (m_sensor_input_signal_statuses) {
        delete m_sensor_input_signal_statuses; m_sensor_input_signal_statuses = 0;
    }
    if (m_sensor_calibration_components) {
        delete m_sensor_calibration_components; m_sensor_calibration_components = 0;
    }
    if (m_sensor_calibration_statuses) {
        delete m_sensor_calibration_statuses; m_sensor_calibration_statuses = 0;
    }
    if (m_sensor_calibration_states) {
        delete m_sensor_calibration_states; m_sensor_calibration_states = 0;
    }
}
