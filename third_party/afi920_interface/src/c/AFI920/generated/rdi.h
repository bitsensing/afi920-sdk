#ifndef RDI_H_
#define RDI_H_

// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

class rdi_t;

#include "kaitai/kaitaistruct.h"
#include <stdint.h>
#include <set>
#include <vector>

#if KAITAI_STRUCT_VERSION < 11000L
#error "Incompatible Kaitai Struct C++/STL API: version 0.11 or later is required"
#endif

/**
 * ISO 23150 Radar Detection Interface (RDI) message payload.
 * Contains radar detection data including position, velocity, and signal quality.
 * Header: 36 bytes, Detection: 51 bytes each, Max 4096 detections.
 */

class rdi_t : public kaitai::kstruct {

public:
    class detection_t;
    class rdi_header_t;

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

    rdi_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent = 0, rdi_t* p__root = 0);

private:
    void _read();
    void _clean_up();

public:
    ~rdi_t();

    /**
     * Single radar detection (51 bytes = Status 9 + Info 10 + Position 24 + Dynamics 8)
     */

    class detection_t : public kaitai::kstruct {

    public:

        detection_t(kaitai::kstream* p__io, rdi_t* p__parent = 0, rdi_t* p__root = 0);

    private:
        void _read();
        void _clean_up();

    public:
        ~detection_t();

    private:
        uint8_t m_existence_probability;
        uint16_t m_detection_id;
        uint16_t m_object_id_reference;
        float m_timestamp_difference;
        float m_radar_cross_section;
        float m_signal_to_noise_ratio;
        uint16_t m_ambiguity_grouping_id;
        float m_position_radial_distance;
        float m_position_azimuth;
        float m_position_elevation;
        float m_position_radial_distance_error;
        float m_position_azimuth_error;
        float m_position_elevation_error;
        float m_relative_velocity_radial_distance;
        float m_relative_velocity_radial_distance_error;
        rdi_t* m__root;
        rdi_t* m__parent;

    public:

        /**
         * 0-100 percent
         */
        uint8_t existence_probability() const { return m_existence_probability; }
        uint16_t detection_id() const { return m_detection_id; }

        /**
         * associated Object ID (reserve)
         */
        uint16_t object_id_reference() const { return m_object_id_reference; }

        /**
         * seconds
         */
        float timestamp_difference() const { return m_timestamp_difference; }

        /**
         * dBsm
         */
        float radar_cross_section() const { return m_radar_cross_section; }

        /**
         * dB
         */
        float signal_to_noise_ratio() const { return m_signal_to_noise_ratio; }
        uint16_t ambiguity_grouping_id() const { return m_ambiguity_grouping_id; }

        /**
         * meters
         */
        float position_radial_distance() const { return m_position_radial_distance; }

        /**
         * radians
         */
        float position_azimuth() const { return m_position_azimuth; }

        /**
         * radians
         */
        float position_elevation() const { return m_position_elevation; }

        /**
         * meters
         */
        float position_radial_distance_error() const { return m_position_radial_distance_error; }

        /**
         * radians
         */
        float position_azimuth_error() const { return m_position_azimuth_error; }

        /**
         * radians
         */
        float position_elevation_error() const { return m_position_elevation_error; }

        /**
         * m/s
         */
        float relative_velocity_radial_distance() const { return m_relative_velocity_radial_distance; }

        /**
         * m/s
         */
        float relative_velocity_radial_distance_error() const { return m_relative_velocity_radial_distance_error; }
        rdi_t* _root() const { return m__root; }
        rdi_t* _parent() const { return m__parent; }
    };

    /**
     * RDI header (36 bytes)
     */

    class rdi_header_t : public kaitai::kstruct {

    public:

        rdi_header_t(kaitai::kstream* p__io, rdi_t* p__parent = 0, rdi_t* p__root = 0);

    private:
        void _read();
        void _clean_up();

    public:
        ~rdi_header_t();

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
        float m_radial_velocity_ambiguity_domain_begin;
        float m_radial_velocity_ambiguity_domain_end;
        uint16_t m_recognised_detections_capability;
        uint16_t m_number_of_valid_detections;
        rdi_t* m__root;
        rdi_t* m__parent;

    public:
        uint8_t interface_version_major() const { return m_interface_version_major; }
        uint8_t interface_version_minor() const { return m_interface_version_minor; }
        uint8_t interface_version_patch() const { return m_interface_version_patch; }

        /**
         * 0x08 = IID_RadarDetection
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

        /**
         * m/s
         */
        float radial_velocity_ambiguity_domain_begin() const { return m_radial_velocity_ambiguity_domain_begin; }

        /**
         * m/s
         */
        float radial_velocity_ambiguity_domain_end() const { return m_radial_velocity_ambiguity_domain_end; }

        /**
         * Max detections (typically 4096)
         */
        uint16_t recognised_detections_capability() const { return m_recognised_detections_capability; }
        uint16_t number_of_valid_detections() const { return m_number_of_valid_detections; }
        rdi_t* _root() const { return m__root; }
        rdi_t* _parent() const { return m__parent; }
    };

private:
    rdi_header_t* m_header;
    std::vector<detection_t*>* m_detections;
    rdi_t* m__root;
    kaitai::kstruct* m__parent;

public:
    rdi_header_t* header() const { return m_header; }
    std::vector<detection_t*>* detections() const { return m_detections; }
    rdi_t* _root() const { return m__root; }
    kaitai::kstruct* _parent() const { return m__parent; }
};

#endif  // RDI_H_
