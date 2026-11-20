/**
 * @file afi_compat.h
 * @brief Source compatibility names for enumerators removed or renamed in
 *        the AFI920 interface v1.2.0 headers (SDK v2.2.0).
 *
 * Each name keeps the value it had in SDK v2.1.0, so existing code compiles
 * and compares the same numbers as before. The sensor no longer reports some
 * of these values, and some values now carry a different meaning; the
 * deprecation message of each name states the current meaning. New code uses
 * the current enumerators of bts_iso23150_shi.h and bts_iso23150_spi.h.
 * Define AFI_NO_COMPAT_NAMES before including the SDK headers to omit them.
 */
#pragma once

#include "AFI920/bts_iso23150_shi.h"
#include "AFI920/bts_iso23150_spi.h"

#ifndef AFI_NO_COMPAT_NAMES

#define AFI_COMPAT_NAME(type, name, value, msg) \
    [[deprecated(msg)]] inline constexpr type name = static_cast<type>(value);

// bts_shi_sensor_operation_mode_t
AFI_COMPAT_NAME(bts_shi_sensor_operation_mode_t, BTS_SHI_SOM_EVALUATION_MODE, 50,
    "Value 50 is not defined in the current interface")
AFI_COMPAT_NAME(bts_shi_sensor_operation_mode_t, BTS_SHI_SOM_CALIBRATION_MODE, 100,
    "Value 100 is BTS_SHI_SOM_FAULT_MODE in the current interface")
AFI_COMPAT_NAME(bts_shi_sensor_operation_mode_t, BTS_SHI_SOM_TEST_MODE, 201,
    "Value 201 is not defined in the current interface")

// bts_shi_sensor_defect_recognised_t
AFI_COMPAT_NAME(bts_shi_sensor_defect_recognised_t, BTS_SHI_SDR_OUT_OF_ORDER, 2,
    "Value 2 is not defined in the current interface")

// bts_shi_sensor_defect_reason_t
AFI_COMPAT_NAME(bts_shi_sensor_defect_reason_t, BTS_SHI_SDR_INTERNAL_MEMORY_ERROR, 1,
    "Value 1 is BTS_SHI_SDR_SUPPLY_VOLTAGE_FAULT in the current interface")
AFI_COMPAT_NAME(bts_shi_sensor_defect_reason_t, BTS_SHI_SDR_COMMUNICATION_ERROR, 4,
    "Value 4 is not defined in the current interface")
AFI_COMPAT_NAME(bts_shi_sensor_defect_reason_t, BTS_SHI_SDR_CONFIGURATION_ERROR, 6,
    "Value 6 is not defined in the current interface")
AFI_COMPAT_NAME(bts_shi_sensor_defect_reason_t, BTS_SHI_SDR_SOFTWARE_DEFECT, 8,
    "Value 8 is not defined in the current interface")
AFI_COMPAT_NAME(bts_shi_sensor_defect_reason_t, BTS_SHI_SDR_COMPUTING_POWER, 9,
    "Value 9 is not defined in the current interface")

// bts_shi_sensor_calibration_state_t
AFI_COMPAT_NAME(bts_shi_sensor_calibration_state_t, BTS_SHI_CPS_INITIAL_CAL_PERFORMED, 0,
    "Use BTS_SHI_SCST_COMPLETED (same value 0)")
AFI_COMPAT_NAME(bts_shi_sensor_calibration_state_t, BTS_SHI_CPS_INITIAL_CAL_NOT_PERFORMED, 1,
    "Use BTS_SHI_SCST_NOT_PERFORMED (same value 1)")
AFI_COMPAT_NAME(bts_shi_sensor_calibration_state_t, BTS_SHI_CPS_INITIAL_CAL_FAILED, 2,
    "Value 2 is BTS_SHI_SCST_IN_PROGRESS; failure is BTS_SHI_SCST_FAILED (3)")
AFI_COMPAT_NAME(bts_shi_sensor_calibration_state_t, BTS_SHI_CPS_RECAL_NEEDED_INTRINSIC, 3,
    "Value 3 is BTS_SHI_SCST_FAILED in the current interface")
AFI_COMPAT_NAME(bts_shi_sensor_calibration_state_t, BTS_SHI_CPS_RECAL_NEEDED_EXTRINSIC, 4,
    "Value 4 is not defined in the current interface")
AFI_COMPAT_NAME(bts_shi_sensor_calibration_state_t, BTS_SHI_CPS_RECAL_NEEDED_FULL, 5,
    "Value 5 is not defined in the current interface")

// bts_spi_blockage_status_t
AFI_COMPAT_NAME(bts_spi_blockage_status_t, BTS_SPI_BS_UNKNOWN, 3,
    "Value 3 is not defined in the current interface")

#undef AFI_COMPAT_NAME

#endif  // AFI_NO_COMPAT_NAMES
