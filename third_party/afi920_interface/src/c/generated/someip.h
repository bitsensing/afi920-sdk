#ifndef SOMEIP_H_
#define SOMEIP_H_

// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

class someip_t;

#include "kaitai/kaitaistruct.h"
#include <stdint.h>
#include <set>

#if KAITAI_STRUCT_VERSION < 11000L
#error "Incompatible Kaitai Struct C++/STL API: version 0.11 or later is required"
#endif

/**
 * SOME/IP (Scalable service-Oriented MiddlewarE over IP) protocol header.
 * Used for automotive Ethernet communication (ISO 23150).
 */

class someip_t : public kaitai::kstruct {

public:
    class tp_header_t;

    enum event_id_enum_t {
        EVENT_ID_ENUM_SII = 32769,
        EVENT_ID_ENUM_RDI = 32770,
        EVENT_ID_ENUM_SHII = 32771,
        EVENT_ID_ENUM_SPI = 32772,
    };
    static bool _is_defined_event_id_enum_t(event_id_enum_t v);

private:
    static const std::set<event_id_enum_t> _values_event_id_enum_t;
    static std::set<event_id_enum_t> _build_values_event_id_enum_t();

public:

    enum message_type_enum_t {
        MESSAGE_TYPE_ENUM_REQUEST = 0,
        MESSAGE_TYPE_ENUM_REQUEST_NO_RETURN = 1,
        MESSAGE_TYPE_ENUM_NOTIFICATION = 2,
        MESSAGE_TYPE_ENUM_NOTIFICATION_TP = 34,
        MESSAGE_TYPE_ENUM_RESPONSE = 128,
        MESSAGE_TYPE_ENUM_ERROR = 129
    };
    static bool _is_defined_message_type_enum_t(message_type_enum_t v);

private:
    static const std::set<message_type_enum_t> _values_message_type_enum_t;
    static std::set<message_type_enum_t> _build_values_message_type_enum_t();

public:

    someip_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent = 0, someip_t* p__root = 0);

private:
    void _read();
    void _clean_up();

public:
    ~someip_t();

    /**
     * SOME/IP Transport Protocol header for message segmentation
     */

    class tp_header_t : public kaitai::kstruct {

    public:

        tp_header_t(kaitai::kstream* p__io, someip_t* p__parent = 0, someip_t* p__root = 0);

    private:
        void _read();
        void _clean_up();

    public:
        ~tp_header_t();

    private:
        bool f_more_segments;
        bool m_more_segments;

    public:

        /**
         * True if more segments follow
         */
        bool more_segments();

    private:
        bool f_offset_bytes;
        int32_t m_offset_bytes;

    public:

        /**
         * Offset in bytes
         */
        int32_t offset_bytes();

    private:
        uint32_t m_segment_info;
        someip_t* m__root;
        someip_t* m__parent;

    public:

        /**
         * Offset (bits 31:4, in 16-byte units), Reserved (bits 3:1), More flag (bit 0)
         */
        uint32_t segment_info() const { return m_segment_info; }
        someip_t* _root() const { return m__root; }
        someip_t* _parent() const { return m__parent; }
    };

private:
    bool f_client_id;
    int32_t m_client_id;

public:
    int32_t client_id();

private:
    bool f_event_id;
    int32_t m_event_id;

public:
    int32_t event_id();

private:
    bool f_is_tp;
    bool m_is_tp;

public:
    bool is_tp();

private:
    bool f_payload_size;
    int32_t m_payload_size;

public:
    int32_t payload_size();

private:
    bool f_service_id;
    int32_t m_service_id;

public:
    int32_t service_id();

private:
    bool f_session_id;
    int32_t m_session_id;

public:
    int32_t session_id();

private:
    uint32_t m_message_id;
    uint32_t m_length;
    uint32_t m_request_id;
    uint8_t m_protocol_version;
    uint8_t m_interface_version;
    uint8_t m_message_type;
    uint8_t m_return_code;
    tp_header_t* m_tp;
    bool n_tp;

public:
    bool _is_null_tp() { tp(); return n_tp; };

private:
    std::string m_payload;
    someip_t* m__root;
    kaitai::kstruct* m__parent;

public:

    /**
     * Service ID (upper 16 bits) + Method/Event ID (lower 16 bits)
     */
    uint32_t message_id() const { return m_message_id; }

    /**
     * Length from Request ID to end of payload (8 + payload_size)
     */
    uint32_t length() const { return m_length; }

    /**
     * Client ID (upper 16 bits) + Session ID (lower 16 bits)
     */
    uint32_t request_id() const { return m_request_id; }

    /**
     * SOME/IP protocol version (0x01)
     */
    uint8_t protocol_version() const { return m_protocol_version; }

    /**
     * Service interface version
     */
    uint8_t interface_version() const { return m_interface_version; }

    /**
     * Message type (0x02 = NOTIFICATION, bit5 = TP flag)
     */
    uint8_t message_type() const { return m_message_type; }

    /**
     * Return code (0x00 = OK)
     */
    uint8_t return_code() const { return m_return_code; }

    /**
     * Transport Protocol header for segmented messages
     */
    tp_header_t* tp() const { return m_tp; }

    /**
     * Payload data (PDU)
     */
    std::string payload() const { return m_payload; }
    someip_t* _root() const { return m__root; }
    kaitai::kstruct* _parent() const { return m__parent; }
};

#endif  // SOMEIP_H_
