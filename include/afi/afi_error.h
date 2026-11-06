/**
 * @file afi_error.h
 * @brief AFI920 SDK error codes
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

namespace afi {

enum AfiError {
    kOk             =  0,
    kNotInitialized = -1,
    kAlreadyRunning = -2,
    kConnectFailed  = -3,
    kTimeout        = -4,
    kDisconnected   = -5,
    kInvalidParam   = -6,
    kProtocolError  = -7,
    kSensorError    = -8,
    kInternalError  = -9,
    kNotSupported   = -10,  ///< SDK feature unavailable, or radar NOT_SUPPORTED (0x05)
    kBusy           = -11,  ///< Radar BUSY (0x08): request rate above 200 per second
    kSessionRequired = -12, ///< Radar SESSION_REQUIRED (0x0C): enter the access mode first
    kAccessDenied   = -13,  ///< Radar ACCESS_DENIED (0x0D): precondition not met
    kInvalidState   = -14,  ///< Radar INVALID_STATE (0x0E): chained step out of order
};

/// Radar return codes (System Spec §6), sent in the SOME/IP header and the
/// JSON body of every Config API response.
enum AfiReturnCode {
    kRcSuccess               = 0x00,
    kRcInvalidParam          = 0x01,
    kRcConnectionFail        = 0x02,
    kRcNoData                = 0x03,
    kRcMemoryError           = 0x04,
    kRcNotSupported          = 0x05,
    kRcInternalError         = 0x07,
    kRcBusy                  = 0x08,
    kRcModeChangeFail        = 0x09,
    kRcCalibrationInProgress = 0x0A,
    kRcTimeout               = 0x0B,
    kRcSessionRequired       = 0x0C,
    kRcAccessDenied          = 0x0D,
    kRcInvalidState          = 0x0E,
};

/// SDK error for a radar return code (kSensorError for codes without one).
inline int AfiErrorFromReturnCode(int rc) {
    switch (rc) {
        case kRcSuccess:         return kOk;
        case kRcInvalidParam:    return kInvalidParam;
        case kRcNotSupported:    return kNotSupported;
        case kRcBusy:            return kBusy;
        case kRcSessionRequired: return kSessionRequired;
        case kRcAccessDenied:    return kAccessDenied;
        case kRcInvalidState:    return kInvalidState;
        default:                 return kSensorError;
    }
}

/// Name of a radar return code, as the radar spells it in `return_name`.
inline const char* AfiReturnCodeName(int rc) {
    switch (rc) {
        case kRcSuccess:               return "SUCCESS";
        case kRcInvalidParam:          return "INVALID_PARAM";
        case kRcConnectionFail:        return "CONNECTION_FAIL";
        case kRcNoData:                return "NO_DATA";
        case kRcMemoryError:           return "MEMORY_ERROR";
        case kRcNotSupported:          return "NOT_SUPPORTED";
        case kRcInternalError:         return "INTERNAL_ERROR";
        case kRcBusy:                  return "BUSY";
        case kRcModeChangeFail:        return "MODE_CHANGE_FAIL";
        case kRcCalibrationInProgress: return "CALIBRATION_IN_PROGRESS";
        case kRcTimeout:               return "TIMEOUT";
        case kRcSessionRequired:       return "SESSION_REQUIRED";
        case kRcAccessDenied:          return "ACCESS_DENIED";
        case kRcInvalidState:          return "INVALID_STATE";
        default:                       return "UNKNOWN";
    }
}

/**
 * @brief Get human-readable error message
 */
inline const char* AfiErrorStr(int code) {
    switch (code) {
        case kOk:             return "OK";
        case kNotInitialized: return "Not initialized";
        case kAlreadyRunning: return "Already running";
        case kConnectFailed:  return "Connection failed";
        case kTimeout:        return "Timeout";
        case kDisconnected:   return "Disconnected";
        case kInvalidParam:   return "Invalid parameter";
        case kProtocolError:  return "Protocol error";
        case kSensorError:    return "Sensor returned error";
        case kInternalError:  return "Internal error";
        case kNotSupported:   return "Not supported";
        case kBusy:           return "Sensor busy (request rate limit)";
        case kSessionRequired: return "Access mode required (SetMode)";
        case kAccessDenied:   return "Access denied by sensor";
        case kInvalidState:   return "Invalid state for this step";
        default:              return "Unknown error";
    }
}

} // namespace afi
