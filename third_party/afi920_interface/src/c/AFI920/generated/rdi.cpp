// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

#include "rdi.h"
std::set<rdi_t::data_qualifier_enum_t> rdi_t::_build_values_data_qualifier_enum_t() {
    std::set<rdi_t::data_qualifier_enum_t> _t;
    _t.insert(rdi_t::DATA_QUALIFIER_ENUM_DQ_NORMAL);
    _t.insert(rdi_t::DATA_QUALIFIER_ENUM_DQ_NOT_AVAILABLE);
    _t.insert(rdi_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_COVERAGE);
    _t.insert(rdi_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_IN_PERFORMANCE);
    _t.insert(rdi_t::DATA_QUALIFIER_ENUM_DQ_REDUCE_COVERAGE_AND_PERFORMANCE);
    _t.insert(rdi_t::DATA_QUALIFIER_ENUM_DQ_TEST_MODE);
    _t.insert(rdi_t::DATA_QUALIFIER_ENUM_DQ_INVALID);
    return _t;
}
const std::set<rdi_t::data_qualifier_enum_t> rdi_t::_values_data_qualifier_enum_t = rdi_t::_build_values_data_qualifier_enum_t();
bool rdi_t::_is_defined_data_qualifier_enum_t(rdi_t::data_qualifier_enum_t v) {
    return rdi_t::_values_data_qualifier_enum_t.find(v) != rdi_t::_values_data_qualifier_enum_t.end();
}

rdi_t::rdi_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent, rdi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root ? p__root : this;
    m_header = 0;
    m_detections = 0;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void rdi_t::_read() {
    m_header = new rdi_header_t(m__io, this, m__root);
    m_detections = new std::vector<detection_t*>();
    const int l_detections = header()->number_of_valid_detections();
    for (int i = 0; i < l_detections; i++) {
        m_detections->push_back(new detection_t(m__io, this, m__root));
    }
}

rdi_t::~rdi_t() {
    _clean_up();
}

void rdi_t::_clean_up() {
    if (m_header) {
        delete m_header; m_header = 0;
    }
    if (m_detections) {
        for (std::vector<detection_t*>::iterator it = m_detections->begin(); it != m_detections->end(); ++it) {
            delete *it;
        }
        delete m_detections; m_detections = 0;
    }
}

rdi_t::detection_t::detection_t(kaitai::kstream* p__io, rdi_t* p__parent, rdi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void rdi_t::detection_t::_read() {
    m_existence_probability = m__io->read_u1();
    m_detection_id = m__io->read_u2le();
    m_object_id_reference = m__io->read_u2le();
    m_timestamp_difference = m__io->read_f4le();
    m_radar_cross_section = m__io->read_f4le();
    m_signal_to_noise_ratio = m__io->read_f4le();
    m_ambiguity_grouping_id = m__io->read_u2le();
    m_position_radial_distance = m__io->read_f4le();
    m_position_azimuth = m__io->read_f4le();
    m_position_elevation = m__io->read_f4le();
    m_position_radial_distance_error = m__io->read_f4le();
    m_position_azimuth_error = m__io->read_f4le();
    m_position_elevation_error = m__io->read_f4le();
    m_relative_velocity_radial_distance = m__io->read_f4le();
    m_relative_velocity_radial_distance_error = m__io->read_f4le();
}

rdi_t::detection_t::~detection_t() {
    _clean_up();
}

void rdi_t::detection_t::_clean_up() {
}

rdi_t::rdi_header_t::rdi_header_t(kaitai::kstream* p__io, rdi_t* p__parent, rdi_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void rdi_t::rdi_header_t::_read() {
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
    m_data_qualifier = static_cast<rdi_t::data_qualifier_enum_t>(m__io->read_u1());
    m_radial_velocity_ambiguity_domain_begin = m__io->read_f4le();
    m_radial_velocity_ambiguity_domain_end = m__io->read_f4le();
    m_recognised_detections_capability = m__io->read_u2le();
    m_number_of_valid_detections = m__io->read_u2le();
}

rdi_t::rdi_header_t::~rdi_header_t() {
    _clean_up();
}

void rdi_t::rdi_header_t::_clean_up() {
}
