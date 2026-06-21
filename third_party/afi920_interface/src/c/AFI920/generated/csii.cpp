// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

#include "csii.h"
std::set<csii_t::data_qualifier_enum_t> csii_t::_build_values_data_qualifier_enum_t() {
    std::set<csii_t::data_qualifier_enum_t> _t;
    _t.insert(csii_t::DATA_QUALIFIER_ENUM_DQ_NORMAL);
    _t.insert(csii_t::DATA_QUALIFIER_ENUM_DQ_NOT_AVAILABLE);
    _t.insert(csii_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_COVERAGE);
    _t.insert(csii_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_PERFORMANCE);
    _t.insert(csii_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_COVERAGE_AND_PERFORMANCE);
    _t.insert(csii_t::DATA_QUALIFIER_ENUM_DQ_TEST_MODE);
    _t.insert(csii_t::DATA_QUALIFIER_ENUM_DQ_INVALID);
    return _t;
}
const std::set<csii_t::data_qualifier_enum_t> csii_t::_values_data_qualifier_enum_t = csii_t::_build_values_data_qualifier_enum_t();
bool csii_t::_is_defined_data_qualifier_enum_t(csii_t::data_qualifier_enum_t v) {
    return csii_t::_values_data_qualifier_enum_t.find(v) != csii_t::_values_data_qualifier_enum_t.end();
}
std::set<csii_t::gear_position_enum_t> csii_t::_build_values_gear_position_enum_t() {
    std::set<csii_t::gear_position_enum_t> _t;
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_NEUTRAL);
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_PARKING);
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_FORWARD);
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_REVERSE);
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_LOW_GEAR);
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_2ND_GEAR);
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_3RD_GEAR);
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_4TH_GEAR);
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_5TH_GEAR);
    _t.insert(csii_t::GEAR_POSITION_ENUM_GP_6TH_GEAR);
    return _t;
}
const std::set<csii_t::gear_position_enum_t> csii_t::_values_gear_position_enum_t = csii_t::_build_values_gear_position_enum_t();
bool csii_t::_is_defined_gear_position_enum_t(csii_t::gear_position_enum_t v) {
    return csii_t::_values_gear_position_enum_t.find(v) != csii_t::_values_gear_position_enum_t.end();
}
std::set<csii_t::somc_enum_t> csii_t::_build_values_somc_enum_t() {
    std::set<csii_t::somc_enum_t> _t;
    _t.insert(csii_t::SOMC_ENUM_SOMC_INITIALIZING);
    _t.insert(csii_t::SOMC_ENUM_SOMC_OFF);
    _t.insert(csii_t::SOMC_ENUM_SOMC_DORMANCY);
    _t.insert(csii_t::SOMC_ENUM_SOMC_LOW_POWER_MODE);
    _t.insert(csii_t::SOMC_ENUM_SOMC_NORMAL_MODE);
    _t.insert(csii_t::SOMC_ENUM_SOMC_NORMAL_MODE_SR);
    _t.insert(csii_t::SOMC_ENUM_SOMC_NORMAL_MODE_LR);
    _t.insert(csii_t::SOMC_ENUM_SOMC_NORMAL_MODE_SRLR);
    _t.insert(csii_t::SOMC_ENUM_SOMC_NORMAL_MODE_USR);
    _t.insert(csii_t::SOMC_ENUM_SOMC_START_CALIBRATION);
    _t.insert(csii_t::SOMC_ENUM_SOMC_START_CLEANING);
    return _t;
}
const std::set<csii_t::somc_enum_t> csii_t::_values_somc_enum_t = csii_t::_build_values_somc_enum_t();
bool csii_t::_is_defined_somc_enum_t(csii_t::somc_enum_t v) {
    return csii_t::_values_somc_enum_t.find(v) != csii_t::_values_somc_enum_t.end();
}
std::set<csii_t::vehicle_coord_system_enum_t> csii_t::_build_values_vehicle_coord_system_enum_t() {
    std::set<csii_t::vehicle_coord_system_enum_t> _t;
    _t.insert(csii_t::VEHICLE_COORD_SYSTEM_ENUM_VCS_REAR_AXLE);
    _t.insert(csii_t::VEHICLE_COORD_SYSTEM_ENUM_VCS_ROAD_LEVEL);
    return _t;
}
const std::set<csii_t::vehicle_coord_system_enum_t> csii_t::_values_vehicle_coord_system_enum_t = csii_t::_build_values_vehicle_coord_system_enum_t();
bool csii_t::_is_defined_vehicle_coord_system_enum_t(csii_t::vehicle_coord_system_enum_t v) {
    return csii_t::_values_vehicle_coord_system_enum_t.find(v) != csii_t::_values_vehicle_coord_system_enum_t.end();
}

csii_t::csii_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent, csii_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root ? p__root : this;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void csii_t::_read() {
    m_interface_version_major = m__io->read_u1();
    m_interface_version_minor = m__io->read_u1();
    m_interface_version_patch = m__io->read_u1();
    m_interface_id = m__io->read_u1();
    m_number_of_valid_serving_sensors = m__io->read_u1();
    m_sensor_id = m__io->read_u1();
    m_timestamp_measurement = m__io->read_u8le();
    m_message_counter = m__io->read_u4le();
    m_interface_cycle_time = m__io->read_u4le();
    m_interface_cycle_time_variation = m__io->read_u1();
    m_data_qualifier = static_cast<csii_t::data_qualifier_enum_t>(m__io->read_u1());
    m_somc = static_cast<csii_t::somc_enum_t>(m__io->read_u1());
    m_global_timestamp_utc = m__io->read_f8le();
    m_vehicle_coord_system = static_cast<csii_t::vehicle_coord_system_enum_t>(m__io->read_u1());
    m_velocity_x_mps = m__io->read_f4le();
    m_velocity_y_mps = m__io->read_f4le();
    m_velocity_z_mps = m__io->read_f4le();
    m_rotation_rate_yaw_rps = m__io->read_f4le();
    m_rotation_rate_pitch_rps = m__io->read_f4le();
    m_rotation_rate_roll_rps = m__io->read_f4le();
    m_wheel_flp_rps = m__io->read_f4le();
    m_wheel_frp_rps = m__io->read_f4le();
    m_wheel_rlp_rps = m__io->read_f4le();
    m_wheel_rrp_rps = m__io->read_f4le();
    m_steering_angle_rad = m__io->read_f4le();
    m_gear_position = static_cast<csii_t::gear_position_enum_t>(m__io->read_u1());
}

csii_t::~csii_t() {
    _clean_up();
}

void csii_t::_clean_up() {
}
