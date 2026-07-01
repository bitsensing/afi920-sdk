#ifndef SPI_H_
#define SPI_H_

// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

class spi_t;

#include "kaitai/kaitaistruct.h"
#include <stdint.h>
#include <set>
#include <vector>

#if KAITAI_STRUCT_VERSION < 11000L
#error "Incompatible Kaitai Struct C++/STL API: version 0.11 or later is required"
#endif

/**
 * ISO 23150 Sensor Performance Interface (SPI) message payload.
 * Contains sensor pose, FOV segments, recognisable object types, and reference targets.
 * Variable length structure.
 */

class spi_t : public kaitai::kstruct {

public:
    class fov_segment_t;
    class recognisable_object_type_t;
    class reference_target_type_t;
    class sensor_pose_t;
    class spi_header_t;

    enum blockage_status_enum_t {
        BLOCKAGE_STATUS_ENUM_BS_NONE = 0,
        BLOCKAGE_STATUS_ENUM_BS_PARTIAL_BLOCKAGE = 1,
        BLOCKAGE_STATUS_ENUM_BS_FULL_BLOCKAGE = 2
    };
    static bool _is_defined_blockage_status_enum_t(blockage_status_enum_t v);

private:
    static const std::set<blockage_status_enum_t> _values_blockage_status_enum_t;
    static std::set<blockage_status_enum_t> _build_values_blockage_status_enum_t();

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

    enum recognised_object_type_enum_t {
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_CAR = 0,
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_HEAVY_TRUCK = 1,
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_MOTORBIKE = 2,
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_BICYCLE = 3,
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_PEDESTRIAN = 4,
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_POLE = 5,
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_GUARD_RAIL = 6,
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_BUILDING = 7,
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_TRAFFIC_SIGN = 8,
        RECOGNISED_OBJECT_TYPE_ENUM_ROT_TRAFFIC_LIGHT = 9
    };
    static bool _is_defined_recognised_object_type_enum_t(recognised_object_type_enum_t v);

private:
    static const std::set<recognised_object_type_enum_t> _values_recognised_object_type_enum_t;
    static std::set<recognised_object_type_enum_t> _build_values_recognised_object_type_enum_t();

public:

    enum vehicle_coordinate_system_enum_t {
        VEHICLE_COORDINATE_SYSTEM_ENUM_VCST_REAR_AXLE = 0,
        VEHICLE_COORDINATE_SYSTEM_ENUM_VCST_ROAD_LEVEL = 1
    };
    static bool _is_defined_vehicle_coordinate_system_enum_t(vehicle_coordinate_system_enum_t v);

private:
    static const std::set<vehicle_coordinate_system_enum_t> _values_vehicle_coordinate_system_enum_t;
    static std::set<vehicle_coordinate_system_enum_t> _build_values_vehicle_coordinate_system_enum_t();

public:

    spi_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent = 0, spi_t* p__root = 0);

private:
    void _read();
    void _clean_up();

public:
    ~spi_t();

    /**
     * Field of View segment (17 bytes)
     */

    class fov_segment_t : public kaitai::kstruct {

    public:

        fov_segment_t(kaitai::kstream* p__io, spi_t* p__parent = 0, spi_t* p__root = 0);

    private:
        void _read();
        void _clean_up();

    public:
        ~fov_segment_t();

    private:
        float m_segment_azimuth_begin;
        float m_segment_azimuth_end;
        float m_segment_elevation_begin;
        float m_segment_elevation_end;
        blockage_status_enum_t m_blockage_status;
        spi_t* m__root;
        spi_t* m__parent;

    public:

        /**
         * radians
         */
        float segment_azimuth_begin() const { return m_segment_azimuth_begin; }

        /**
         * radians
         */
        float segment_azimuth_end() const { return m_segment_azimuth_end; }

        /**
         * radians (optional)
         */
        float segment_elevation_begin() const { return m_segment_elevation_begin; }

        /**
         * radians (optional)
         */
        float segment_elevation_end() const { return m_segment_elevation_end; }
        blockage_status_enum_t blockage_status() const { return m_blockage_status; }
        spi_t* _root() const { return m__root; }
        spi_t* _parent() const { return m__parent; }
    };

    /**
     * Recognisable object type (9 bytes)
     */

    class recognisable_object_type_t : public kaitai::kstruct {

    public:

        recognisable_object_type_t(kaitai::kstream* p__io, spi_t* p__parent = 0, spi_t* p__root = 0);

    private:
        void _read();
        void _clean_up();

    public:
        ~recognisable_object_type_t();

    private:
        recognised_object_type_enum_t m_recognised_object_type;
        float m_detection_range_radial_distance_begin;
        float m_detection_range_radial_distance_end;
        spi_t* m__root;
        spi_t* m__parent;

    public:
        recognised_object_type_enum_t recognised_object_type() const { return m_recognised_object_type; }

        /**
         * meters
         */
        float detection_range_radial_distance_begin() const { return m_detection_range_radial_distance_begin; }

        /**
         * meters
         */
        float detection_range_radial_distance_end() const { return m_detection_range_radial_distance_end; }
        spi_t* _root() const { return m__root; }
        spi_t* _parent() const { return m__parent; }
    };

    /**
     * Reference target type (16 bytes)
     */

    class reference_target_type_t : public kaitai::kstruct {

    public:

        reference_target_type_t(kaitai::kstream* p__io, spi_t* p__parent = 0, spi_t* p__root = 0);

    private:
        void _read();
        void _clean_up();

    public:
        ~reference_target_type_t();

    private:
        float m_radar_cross_section_reference_target;
        float m_detection_range_radial_distance_begin;
        float m_detection_range_radial_distance_end;
        float m_signal_to_noise_ratio_support_level;
        spi_t* m__root;
        spi_t* m__parent;

    public:

        /**
         * dBm^2
         */
        float radar_cross_section_reference_target() const { return m_radar_cross_section_reference_target; }

        /**
         * meters
         */
        float detection_range_radial_distance_begin() const { return m_detection_range_radial_distance_begin; }

        /**
         * meters
         */
        float detection_range_radial_distance_end() const { return m_detection_range_radial_distance_end; }

        /**
         * dB
         */
        float signal_to_noise_ratio_support_level() const { return m_signal_to_noise_ratio_support_level; }
        spi_t* _root() const { return m__root; }
        spi_t* _parent() const { return m__parent; }
    };

    /**
     * Sensor position and orientation (32 bytes)
     */

    class sensor_pose_t : public kaitai::kstruct {

    public:

        sensor_pose_t(kaitai::kstream* p__io, spi_t* p__parent = 0, spi_t* p__root = 0);

    private:
        void _read();
        void _clean_up();

    public:
        ~sensor_pose_t();

    private:
        float m_origin_point_x;
        float m_origin_point_y;
        float m_origin_point_z;
        float m_orientation_yaw;
        float m_orientation_pitch;
        float m_orientation_roll;
        float m_orientation_error_yaw;
        float m_orientation_error_pitch;
        spi_t* m__root;
        spi_t* m__parent;

    public:

        /**
         * meters
         */
        float origin_point_x() const { return m_origin_point_x; }

        /**
         * meters
         */
        float origin_point_y() const { return m_origin_point_y; }

        /**
         * meters
         */
        float origin_point_z() const { return m_origin_point_z; }

        /**
         * radians
         */
        float orientation_yaw() const { return m_orientation_yaw; }

        /**
         * radians
         */
        float orientation_pitch() const { return m_orientation_pitch; }

        /**
         * radians
         */
        float orientation_roll() const { return m_orientation_roll; }

        /**
         * radians (optional)
         */
        float orientation_error_yaw() const { return m_orientation_error_yaw; }

        /**
         * radians (optional)
         */
        float orientation_error_pitch() const { return m_orientation_error_pitch; }
        spi_t* _root() const { return m__root; }
        spi_t* _parent() const { return m__parent; }
    };

    /**
     * SPI common header (24 bytes)
     */

    class spi_header_t : public kaitai::kstruct {

    public:

        spi_header_t(kaitai::kstream* p__io, spi_t* p__parent = 0, spi_t* p__root = 0);

    private:
        void _read();
        void _clean_up();

    public:
        ~spi_header_t();

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
        spi_t* m__root;
        spi_t* m__parent;

    public:
        uint8_t interface_version_major() const { return m_interface_version_major; }
        uint8_t interface_version_minor() const { return m_interface_version_minor; }
        uint8_t interface_version_patch() const { return m_interface_version_patch; }

        /**
         * 0x0C = IID_SensorPerformance
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
        spi_t* _root() const { return m__root; }
        spi_t* _parent() const { return m__parent; }
    };

private:
    spi_header_t* m_header;
    vehicle_coordinate_system_enum_t m_vehicle_coordinate_system;
    sensor_pose_t* m_pose;
    uint8_t m_number_of_valid_fov_segments;
    std::vector<fov_segment_t*>* m_fov_segments;
    uint8_t m_number_of_valid_recognisable_object_types;
    std::vector<recognisable_object_type_t*>* m_recognisable_object_types;
    uint8_t m_number_of_valid_reference_target_types;
    std::vector<reference_target_type_t*>* m_reference_target_types;
    spi_t* m__root;
    kaitai::kstruct* m__parent;

public:
    spi_header_t* header() const { return m_header; }
    vehicle_coordinate_system_enum_t vehicle_coordinate_system() const { return m_vehicle_coordinate_system; }
    sensor_pose_t* pose() const { return m_pose; }
    uint8_t number_of_valid_fov_segments() const { return m_number_of_valid_fov_segments; }
    std::vector<fov_segment_t*>* fov_segments() const { return m_fov_segments; }
    uint8_t number_of_valid_recognisable_object_types() const { return m_number_of_valid_recognisable_object_types; }
    std::vector<recognisable_object_type_t*>* recognisable_object_types() const { return m_recognisable_object_types; }
    uint8_t number_of_valid_reference_target_types() const { return m_number_of_valid_reference_target_types; }
    std::vector<reference_target_type_t*>* reference_target_types() const { return m_reference_target_types; }
    spi_t* _root() const { return m__root; }
    kaitai::kstruct* _parent() const { return m__parent; }
};

#endif  // SPI_H_
