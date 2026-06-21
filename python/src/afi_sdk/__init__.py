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

__version__ = "3.0.0"

from afi_sdk._sdk import (
    AfiSdk,
    AfiSdkConfig,
    RangeMode,
    OK,
    DISCONNECTED,
    TIMEOUT,
    PROTOCOL_ERROR,
    SENSOR_ERROR,
    NOT_SUPPORTED,
)
from afi_sdk import streams

__all__ = [
    "AfiSdk",
    "AfiSdkConfig",
    "RangeMode",
    "streams",
    "OK",
    "DISCONNECTED",
    "TIMEOUT",
    "PROTOCOL_ERROR",
    "SENSOR_ERROR",
    "NOT_SUPPORTED",
]
