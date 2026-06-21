#ifndef CSII_H_
#define CSII_H_

// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild

class csii_t;

#include "kaitai/kaitaistruct.h"
#include <stdint.h>
#include <set>

#if KAITAI_STRUCT_VERSION < 11000L
#error "Incompatible Kaitai Struct C++/STL API: version 0.11 or later is required"
#endif

/**
 * ISO 23150 Common Sensor Input Interface (CSII) message payload.
 * Host -> Radar direction. Carries vehicle state, sensor operation command,
 * and environmental time.
 * Fixed size: 79 bytes (with N=NumberOfValidServingSensors=1).
 */

class csii_t : public kaitai::kstruct {

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

    enum gear_position_enum_t {
        GEAR_POSITION_ENUM_GP_NEUTRAL = 0,
        GEAR_POSITION_ENUM_GP_PARKING = 1,
        GEAR_POSITION_ENUM_GP_FORWARD = 2,
        GEAR_POSITION_ENUM_GP_REVERSE = 3,
        GEAR_POSITION_ENUM_GP_LOW_GEAR = 4,
        GEAR_POSITION_ENUM_GP_2ND_GEAR = 5,
        GEAR_POSITION_ENUM_GP_3RD_GEAR = 6,
        GEAR_POSITION_ENUM_GP_4TH_GEAR = 7,
        GEAR_POSITION_ENUM_GP_5TH_GEAR = 8,
        GEAR_POSITION_ENUM_GP_6TH_GEAR = 9
    };
    static bool _is_defined_gear_position_enum_t(gear_position_enum_t v);

private:
    static const std::set<gear_position_enum_t> _values_gear_position_enum_t;
    static std::set<gear_position_enum_t> _build_values_gear_position_enum_t();

public:

    enum somc_enum_t {
        SOMC_ENUM_SOMC_INITIALIZING = 0,
        SOMC_ENUM_SOMC_OFF = 1,
        SOMC_ENUM_SOMC_DORMANCY = 2,
        SOMC_ENUM_SOMC_LOW_POWER_MODE = 3,
        SOMC_ENUM_SOMC_NORMAL_MODE = 16,
        SOMC_ENUM_SOMC_NORMAL_MODE_SR = 17,
        SOMC_ENUM_SOMC_NORMAL_MODE_LR = 18,
        SOMC_ENUM_SOMC_NORMAL_MODE_SRLR = 19,
        SOMC_ENUM_SOMC_NORMAL_MODE_USR = 20,
        SOMC_ENUM_SOMC_START_CALIBRATION = 32,
        SOMC_ENUM_SOMC_START_CLEANING = 33
    };
    static bool _is_defined_somc_enum_t(somc_enum_t v);

private:
    static const std::set<somc_enum_t> _values_somc_enum_t;
    static std::set<somc_enum_t> _build_values_somc_enum_t();

public:

    enum vehicle_coord_system_enum_t {
        VEHICLE_COORD_SYSTEM_ENUM_VCS_REAR_AXLE = 0,
        VEHICLE_COORD_SYSTEM_ENUM_VCS_ROAD_LEVEL = 1
    };
    static bool _is_defined_vehicle_coord_system_enum_t(vehicle_coord_system_enum_t v);

private:
    static const std::set<vehicle_coord_system_enum_t> _values_vehicle_coord_system_enum_t;
    static std::set<vehicle_coord_system_enum_t> _build_values_vehicle_coord_system_enum_t();

public:

    csii_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent = 0, csii_t* p__root = 0);

private:
    void _read();
    void _clean_up();

public:
    ~csii_t();

private:
    uint8_t m_interface_version_major;
    uint8_t m_interface_version_minor;
    uint8_t m_interface_version_patch;
    uint8_t m_interface_id;
    uint8_t m_number_of_valid_serving_sensors;
    uint8_t m_sensor_id;
    uint64_t m_timestamp_measurement;
    uint32_t m_message_counter;
    uint32_t m_interface_cycle_time;
    uint8_t m_interface_cycle_time_variation;
    data_qualifier_enum_t m_data_qualifier;
    somc_enum_t m_somc;
    double m_global_timestamp_utc;
    vehicle_coord_system_enum_t m_vehicle_coord_system;
    float m_velocity_x_mps;
    float m_velocity_y_mps;
    float m_velocity_z_mps;
    float m_rotation_rate_yaw_rps;
    float m_rotation_rate_pitch_rps;
    float m_rotation_rate_roll_rps;
    float m_wheel_flp_rps;
    float m_wheel_frp_rps;
    float m_wheel_rlp_rps;
    float m_wheel_rrp_rps;
    float m_steering_angle_rad;
    gear_position_enum_t m_gear_position;
    csii_t* m__root;
    kaitai::kstruct* m__parent;

public:
    uint8_t interface_version_major() const { return m_interface_version_major; }
    uint8_t interface_version_minor() const { return m_interface_version_minor; }
    uint8_t interface_version_patch() const { return m_interface_version_patch; }

    /**
     * 0x0E = IID_CommonSensorInput
     */
    uint8_t interface_id() const { return m_interface_id; }

    /**
     * Fixed 1 for AFI920 CSII (single host source)
     */
    uint8_t number_of_valid_serving_sensors() const { return m_number_of_valid_serving_sensors; }

    /**
     * Single source identifier (0 = default host)
     */
    uint8_t sensor_id() const { return m_sensor_id; }

    /**
     * Vehicle-global PTP nanoseconds
     */
    uint64_t timestamp_measurement() const { return m_timestamp_measurement; }

    /**
     * Sender-side counter (monotonic)
     */
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
     * Sensor operation mode command
     */
    somc_enum_t somc() const { return m_somc; }

    /**
     * UTC seconds (double)
     */
    double global_timestamp_utc() const { return m_global_timestamp_utc; }
    vehicle_coord_system_enum_t vehicle_coord_system() const { return m_vehicle_coord_system; }

    /**
     * Forward velocity m/s
     */
    float velocity_x_mps() const { return m_velocity_x_mps; }

    /**
     * Lateral (left+) velocity m/s
     */
    float velocity_y_mps() const { return m_velocity_y_mps; }

    /**
     * Vertical (up+) velocity m/s (optional)
     */
    float velocity_z_mps() const { return m_velocity_z_mps; }

    /**
     * Yaw rate rad/s
     */
    float rotation_rate_yaw_rps() const { return m_rotation_rate_yaw_rps; }

    /**
     * Pitch rate rad/s (optional)
     */
    float rotation_rate_pitch_rps() const { return m_rotation_rate_pitch_rps; }

    /**
     * Roll rate rad/s (optional)
     */
    float rotation_rate_roll_rps() const { return m_rotation_rate_roll_rps; }

    /**
     * Front-Left wheel angular velocity rad/s
     */
    float wheel_flp_rps() const { return m_wheel_flp_rps; }

    /**
     * Front-Right wheel angular velocity rad/s
     */
    float wheel_frp_rps() const { return m_wheel_frp_rps; }

    /**
     * Rear-Left wheel angular velocity rad/s
     */
    float wheel_rlp_rps() const { return m_wheel_rlp_rps; }

    /**
     * Rear-Right wheel angular velocity rad/s
     */
    float wheel_rrp_rps() const { return m_wheel_rrp_rps; }

    /**
     * Steering wheel angle rad (optional)
     */
    float steering_angle_rad() const { return m_steering_angle_rad; }
    gear_position_enum_t gear_position() const { return m_gear_position; }
    csii_t* _root() const { return m__root; }
    kaitai::kstruct* _parent() const { return m__parent; }
};

#endif  // CSII_H_
