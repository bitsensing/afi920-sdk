// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

#include "spi.h"
std::set<spi_t::blockage_status_enum_t> spi_t::_build_values_blockage_status_enum_t() {
    std::set<spi_t::blockage_status_enum_t> _t;
    _t.insert(spi_t::BLOCKAGE_STATUS_ENUM_BS_NONE);
    _t.insert(spi_t::BLOCKAGE_STATUS_ENUM_BS_PARTIAL_BLOCKAGE);
    _t.insert(spi_t::BLOCKAGE_STATUS_ENUM_BS_FULL_BLOCKAGE);
    return _t;
}
const std::set<spi_t::blockage_status_enum_t> spi_t::_values_blockage_status_enum_t = spi_t::_build_values_blockage_status_enum_t();
bool spi_t::_is_defined_blockage_status_enum_t(spi_t::blockage_status_enum_t v) {
    return spi_t::_values_blockage_status_enum_t.find(v) != spi_t::_values_blockage_status_enum_t.end();
}
std::set<spi_t::data_qualifier_enum_t> spi_t::_build_values_data_qualifier_enum_t() {
    std::set<spi_t::data_qualifier_enum_t> _t;
    _t.insert(spi_t::DATA_QUALIFIER_ENUM_DQ_NORMAL);
    _t.insert(spi_t::DATA_QUALIFIER_ENUM_DQ_NOT_AVAILABLE);
    _t.insert(spi_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_COVERAGE);
    _t.insert(spi_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_PERFORMANCE);
    _t.insert(spi_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_COVERAGE_AND_PERFORMANCE);
    _t.insert(spi_t::DATA_QUALIFIER_ENUM_DQ_TEST_MODE);
    _t.insert(spi_t::DATA_QUALIFIER_ENUM_DQ_INVALID);
    return _t;
}
const std::set<spi_t::data_qualifier_enum_t> spi_t::_values_data_qualifier_enum_t = spi_t::_build_values_data_qualifier_enum_t();
bool spi_t::_is_defined_data_qualifier_enum_t(spi_t::data_qualifier_enum_t v) {
    return spi_t::_values_data_qualifier_enum_t.find(v) != spi_t::_values_data_qualifier_enum_t.end();
}
std::set<spi_t::recognised_object_type_enum_t> spi_t::_build_values_recognised_object_type_enum_t() {
    std::set<spi_t::recognised_object_type_enum_t> _t;
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_CAR);
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_HEAVY_TRUCK);
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_MOTORBIKE);
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_BICYCLE);
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_PEDESTRIAN);
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_POLE);
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_GUARD_RAIL);
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_BUILDING);
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_TRAFFIC_SIGN);
    _t.insert(spi_t::RECOGNISED_OBJECT_TYPE_ENUM_ROT_TRAFFIC_LIGHT);
    return _t;
}
const std::set<spi_t::recognised_object_type_enum_t> spi_t::_values_recognised_object_type_enum_t = spi_t::_build_values_recognised_object_type_enum_t();
bool spi_t::_is_defined_recognised_object_type_enum_t(spi_t::recognised_object_type_enum_t v) {
    return spi_t::_values_recognised_object_type_enum_t.find(v) != spi_t::_values_recognised_object_type_enum_t.end();
}
std::set<spi_t::vehicle_coordinate_system_enum_t> spi_t::_build_values_vehicle_coordinate_system_enum_t() {
    std::set<spi_t::vehicle_coordinate_system_enum_t> _t;
    _t.insert(spi_t::VEHICLE_COORDINATE_SYSTEM_ENUM_VCST_REAR_AXLE);
    _t.insert(spi_t::VEHICLE_COORDINATE_SYSTEM_ENUM_VCST_ROAD_LEVEL);
    return _t;
}
const std::set<spi_t::vehicle_coordinate_system_enum_t> spi_t::_values_vehicle_coordinate_system_enum_t = spi_t::_build_values_vehicle_coordinate_system_enum_t();
bool spi_t::_is_defined_vehicle_coordinate_system_enum_t(spi_t::vehicle_coordinate_system_enum_t v) {
    return spi_t::_values_vehicle_coordinate_system_enum_t.find(v) != spi_t::_values_vehicle_coordinate_system_enum_t.end();
}

spi_t::spi_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent, spi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root ? p__root : this;
    m_header = 0;
    m_pose = 0;
    m_fov_segments = 0;
    m_recognisable_object_types = 0;
    m_reference_target_types = 0;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void spi_t::_read() {
    m_header = new spi_header_t(m__io, this, m__root);
    m_vehicle_coordinate_system = static_cast<spi_t::vehicle_coordinate_system_enum_t>(m__io->read_u1());
    m_pose = new sensor_pose_t(m__io, this, m__root);
    m_number_of_valid_fov_segments = m__io->read_u1();
    m_fov_segments = new std::vector<fov_segment_t*>();
    const int l_fov_segments = number_of_valid_fov_segments();
    for (int i = 0; i < l_fov_segments; i++) {
        m_fov_segments->push_back(new fov_segment_t(m__io, this, m__root));
    }
    m_number_of_valid_recognisable_object_types = m__io->read_u1();
    m_recognisable_object_types = new std::vector<recognisable_object_type_t*>();
    const int l_recognisable_object_types = number_of_valid_recognisable_object_types();
    for (int i = 0; i < l_recognisable_object_types; i++) {
        m_recognisable_object_types->push_back(new recognisable_object_type_t(m__io, this, m__root));
    }
    m_number_of_valid_reference_target_types = m__io->read_u1();
    m_reference_target_types = new std::vector<reference_target_type_t*>();
    const int l_reference_target_types = number_of_valid_reference_target_types();
    for (int i = 0; i < l_reference_target_types; i++) {
        m_reference_target_types->push_back(new reference_target_type_t(m__io, this, m__root));
    }
}

spi_t::~spi_t() {
    _clean_up();
}

void spi_t::_clean_up() {
    if (m_header) {
        delete m_header; m_header = 0;
    }
    if (m_pose) {
        delete m_pose; m_pose = 0;
    }
    if (m_fov_segments) {
        for (std::vector<fov_segment_t*>::iterator it = m_fov_segments->begin(); it != m_fov_segments->end(); ++it) {
            delete *it;
        }
        delete m_fov_segments; m_fov_segments = 0;
    }
    if (m_recognisable_object_types) {
        for (std::vector<recognisable_object_type_t*>::iterator it = m_recognisable_object_types->begin(); it != m_recognisable_object_types->end(); ++it) {
            delete *it;
        }
        delete m_recognisable_object_types; m_recognisable_object_types = 0;
    }
    if (m_reference_target_types) {
        for (std::vector<reference_target_type_t*>::iterator it = m_reference_target_types->begin(); it != m_reference_target_types->end(); ++it) {
            delete *it;
        }
        delete m_reference_target_types; m_reference_target_types = 0;
    }
}

spi_t::fov_segment_t::fov_segment_t(kaitai::kstream* p__io, spi_t* p__parent, spi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void spi_t::fov_segment_t::_read() {
    m_segment_azimuth_begin = m__io->read_f4le();
    m_segment_azimuth_end = m__io->read_f4le();
    m_segment_elevation_begin = m__io->read_f4le();
    m_segment_elevation_end = m__io->read_f4le();
    m_blockage_status = static_cast<spi_t::blockage_status_enum_t>(m__io->read_u1());
}

spi_t::fov_segment_t::~fov_segment_t() {
    _clean_up();
}

void spi_t::fov_segment_t::_clean_up() {
}

spi_t::recognisable_object_type_t::recognisable_object_type_t(kaitai::kstream* p__io, spi_t* p__parent, spi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void spi_t::recognisable_object_type_t::_read() {
    m_recognised_object_type = static_cast<spi_t::recognised_object_type_enum_t>(m__io->read_u1());
    m_detection_range_radial_distance_begin = m__io->read_f4le();
    m_detection_range_radial_distance_end = m__io->read_f4le();
}

spi_t::recognisable_object_type_t::~recognisable_object_type_t() {
    _clean_up();
}

void spi_t::recognisable_object_type_t::_clean_up() {
}

spi_t::reference_target_type_t::reference_target_type_t(kaitai::kstream* p__io, spi_t* p__parent, spi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void spi_t::reference_target_type_t::_read() {
    m_radar_cross_section_reference_target = m__io->read_f4le();
    m_detection_range_radial_distance_begin = m__io->read_f4le();
    m_detection_range_radial_distance_end = m__io->read_f4le();
    m_signal_to_noise_ratio_support_level = m__io->read_f4le();
}

spi_t::reference_target_type_t::~reference_target_type_t() {
    _clean_up();
}

void spi_t::reference_target_type_t::_clean_up() {
}

spi_t::sensor_pose_t::sensor_pose_t(kaitai::kstream* p__io, spi_t* p__parent, spi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void spi_t::sensor_pose_t::_read() {
    m_origin_point_x = m__io->read_f4le();
    m_origin_point_y = m__io->read_f4le();
    m_origin_point_z = m__io->read_f4le();
    m_orientation_yaw = m__io->read_f4le();
    m_orientation_pitch = m__io->read_f4le();
    m_orientation_roll = m__io->read_f4le();
    m_orientation_error_yaw = m__io->read_f4le();
    m_orientation_error_pitch = m__io->read_f4le();
}

spi_t::sensor_pose_t::~sensor_pose_t() {
    _clean_up();
}

void spi_t::sensor_pose_t::_clean_up() {
}

spi_t::spi_header_t::spi_header_t(kaitai::kstream* p__io, spi_t* p__parent, spi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void spi_t::spi_header_t::_read() {
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
    m_data_qualifier = static_cast<spi_t::data_qualifier_enum_t>(m__io->read_u1());
}

spi_t::spi_header_t::~spi_header_t() {
    _clean_up();
}

void spi_t::spi_header_t::_clean_up() {
}
