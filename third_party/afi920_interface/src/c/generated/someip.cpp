// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

#include "someip.h"
std::set<someip_t::event_id_enum_t> someip_t::_build_values_event_id_enum_t() {
    std::set<someip_t::event_id_enum_t> _t;
    _t.insert(someip_t::EVENT_ID_ENUM_SII);
    _t.insert(someip_t::EVENT_ID_ENUM_RDI);
    _t.insert(someip_t::EVENT_ID_ENUM_SHII);
    _t.insert(someip_t::EVENT_ID_ENUM_SPI);
    return _t;
}
const std::set<someip_t::event_id_enum_t> someip_t::_values_event_id_enum_t = someip_t::_build_values_event_id_enum_t();
bool someip_t::_is_defined_event_id_enum_t(someip_t::event_id_enum_t v) {
    return someip_t::_values_event_id_enum_t.find(v) != someip_t::_values_event_id_enum_t.end();
}
std::set<someip_t::message_type_enum_t> someip_t::_build_values_message_type_enum_t() {
    std::set<someip_t::message_type_enum_t> _t;
    _t.insert(someip_t::MESSAGE_TYPE_ENUM_REQUEST);
    _t.insert(someip_t::MESSAGE_TYPE_ENUM_REQUEST_NO_RETURN);
    _t.insert(someip_t::MESSAGE_TYPE_ENUM_NOTIFICATION);
    _t.insert(someip_t::MESSAGE_TYPE_ENUM_NOTIFICATION_TP);
    _t.insert(someip_t::MESSAGE_TYPE_ENUM_RESPONSE);
    _t.insert(someip_t::MESSAGE_TYPE_ENUM_ERROR);
    return _t;
}
const std::set<someip_t::message_type_enum_t> someip_t::_values_message_type_enum_t = someip_t::_build_values_message_type_enum_t();
bool someip_t::_is_defined_message_type_enum_t(someip_t::message_type_enum_t v) {
    return someip_t::_values_message_type_enum_t.find(v) != someip_t::_values_message_type_enum_t.end();
}

someip_t::someip_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent, someip_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root ? p__root : this;
    m_tp = 0;
    f_client_id = false;
    f_event_id = false;
    f_is_tp = false;
    f_payload_size = false;
    f_service_id = false;
    f_session_id = false;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void someip_t::_read() {
    m_message_id = m__io->read_u4be();
    m_length = m__io->read_u4be();
    m_request_id = m__io->read_u4be();
    m_protocol_version = m__io->read_u1();
    m_interface_version = m__io->read_u1();
    m_message_type = m__io->read_u1();
    m_return_code = m__io->read_u1();
    n_tp = true;
    if (is_tp()) {
        n_tp = false;
        m_tp = new tp_header_t(m__io, this, m__root);
    }
    m_payload = m__io->read_bytes(payload_size());
}

someip_t::~someip_t() {
    _clean_up();
}

void someip_t::_clean_up() {
    if (!n_tp) {
        if (m_tp) {
            delete m_tp; m_tp = 0;
        }
    }
}

someip_t::tp_header_t::tp_header_t(kaitai::kstream* p__io, someip_t* p__parent, someip_t* p__root) : kaitai::kstruct(p__io) {
    m__parent = p__parent;
    m__root = p__root;
    f_more_segments = false;
    f_offset_bytes = false;

    try {
        _read();
    } catch(...) {
        _clean_up();
        throw;
    }
}

void someip_t::tp_header_t::_read() {
    m_segment_info = m__io->read_u4be();
}

someip_t::tp_header_t::~tp_header_t() {
    _clean_up();
}

void someip_t::tp_header_t::_clean_up() {
}

bool someip_t::tp_header_t::more_segments() {
    if (f_more_segments)
        return m_more_segments;
    f_more_segments = true;
    m_more_segments = (segment_info() & 1) != 0;
    return m_more_segments;
}

int32_t someip_t::tp_header_t::offset_bytes() {
    if (f_offset_bytes)
        return m_offset_bytes;
    f_offset_bytes = true;
    m_offset_bytes = (segment_info() >> 4 & 268435455) * 16;
    return m_offset_bytes;
}

int32_t someip_t::client_id() {
    if (f_client_id)
        return m_client_id;
    f_client_id = true;
    m_client_id = request_id() >> 16 & 65535;
    return m_client_id;
}

int32_t someip_t::event_id() {
    if (f_event_id)
        return m_event_id;
    f_event_id = true;
    m_event_id = message_id() & 65535;
    return m_event_id;
}

bool someip_t::is_tp() {
    if (f_is_tp)
        return m_is_tp;
    f_is_tp = true;
    m_is_tp = (message_type() & 32) != 0;
    return m_is_tp;
}

int32_t someip_t::payload_size() {
    if (f_payload_size)
        return m_payload_size;
    f_payload_size = true;
    m_payload_size = ((is_tp()) ? ((length() - 8) - 4) : (length() - 8));
    return m_payload_size;
}

int32_t someip_t::service_id() {
    if (f_service_id)
        return m_service_id;
    f_service_id = true;
    m_service_id = message_id() >> 16 & 65535;
    return m_service_id;
}

int32_t someip_t::session_id() {
    if (f_session_id)
        return m_session_id;
    f_session_id = true;
    m_session_id = request_id() & 65535;
    return m_session_id;
}
