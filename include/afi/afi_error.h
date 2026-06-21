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
    kNotSupported   = -10,
};

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
        default:              return "Unknown error";
    }
}

} // namespace afi
