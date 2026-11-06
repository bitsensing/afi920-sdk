"""
AFI920 Python SDK

High-level Python API for AFI920 4D Imaging Radar.
Mirrors the C++ AfiSdk API for consistent developer experience.

Usage::

    from afi_sdk import AfiSdk, AfiSdkConfig

    config = AfiSdkConfig(sensor_ip="192.168.10.150")
    sdk = AfiSdk()
    sdk.init(config)

    info = sdk.get_device_info()
    print(info["serial_number"], info["software_version"])

    sdk.close()

Data stream parsing (E2E + ISO 23150) lives in :mod:`afi_sdk.streams`.

Dependencies: Python 3.8+ (stdlib only)

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

__version__ = "2.2.0"

from afi_sdk._sdk import (
    AfiSdk,
    AfiSdkConfig,
    RangeMode,
    AccessMode,
    OK,
    NOT_INITIALIZED,
    CONNECT_FAILED,
    DISCONNECTED,
    INVALID_PARAM,
    INTERNAL_ERROR,
    BUSY,
    SESSION_REQUIRED,
    ACCESS_DENIED,
    INVALID_STATE,
    error_from_result,
    TIMEOUT,
    PROTOCOL_ERROR,
    SENSOR_ERROR,
    NOT_SUPPORTED,
)
from afi_sdk.someip import (
    INTERFACE_GEN_UNKNOWN,
    INTERFACE_GEN_V1_0,
    INTERFACE_GEN_V1_1,
    discovery_generation,
)
from afi_sdk import streams

__all__ = [
    "AfiSdk",
    "AfiSdkConfig",
    "RangeMode",
    "AccessMode",
    "streams",
    "OK",
    "NOT_INITIALIZED",
    "CONNECT_FAILED",
    "DISCONNECTED",
    "INVALID_PARAM",
    "INTERNAL_ERROR",
    "BUSY",
    "SESSION_REQUIRED",
    "ACCESS_DENIED",
    "INVALID_STATE",
    "error_from_result",
    "TIMEOUT",
    "PROTOCOL_ERROR",
    "SENSOR_ERROR",
    "NOT_SUPPORTED",
    "INTERFACE_GEN_UNKNOWN",
    "INTERFACE_GEN_V1_0",
    "INTERFACE_GEN_V1_1",
    "discovery_generation",
]
