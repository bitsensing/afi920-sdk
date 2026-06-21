"""
AFI920 data stream helpers — E2E Profile 7 and ISO 23150 payload parsing.

Protocol v3.0 wire format:
  SOME/IP header (16B, BE) + E2E header (20B, BE) + ISO 23150 payload (LE)

E2E header layout: CRC-64/XZ (8B) | Length (4B) | Counter (4B) | Data ID (4B)
The CRC protects frame bytes [8:] (header tail + ISO payload).

Dependencies: Python 3.8+ (stdlib only)

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import struct
import time
from typing import Optional, Tuple

from afi_sdk.someip import SERVICE_ID, EVENT_CSII

# ── E2E Profile 7 ──

E2E_HEADER_SIZE = 20

E2E_DATA_ID_CSII = 0x60008001
E2E_DATA_ID_RDI  = 0x60008002
E2E_DATA_ID_SHII = 0x60008003
E2E_DATA_ID_SPI  = 0x60008004

# CRC-64/XZ (AUTOSAR Crc_CalculateCRC64): poly 0x42F0E1EBA9EA3693 (reflected
# 0xC96C5795D7870F42), init/xorout all-ones, refin/refout true.
# KAT: crc64_xz(b"123456789") == 0x995DC9BBDF1939FA
_CRC64_REFLECTED_POLY = 0xC96C5795D7870F42
_CRC64_INIT = 0xFFFFFFFFFFFFFFFF
_CRC64_XOROUT = 0xFFFFFFFFFFFFFFFF
_MASK64 = 0xFFFFFFFFFFFFFFFF
CRC64_CHECK = 0x995DC9BBDF1939FA


def _make_crc64_table():
    tbl = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ _CRC64_REFLECTED_POLY if (c & 1) else (c >> 1)
        tbl.append(c & _MASK64)
    return tbl


_CRC64_TABLE = _make_crc64_table()


def crc64_xz(data: bytes) -> int:
    """AUTOSAR Crc_CalculateCRC64 (CRC-64/XZ), table-driven reflected."""
    crc = _CRC64_INIT
    for b in data:
        crc = ((crc >> 8) ^ _CRC64_TABLE[(crc ^ b) & 0xFF]) & _MASK64
    return crc ^ _CRC64_XOROUT


assert crc64_xz(b"123456789") == CRC64_CHECK, "CRC-64/XZ self-test failed"


def parse_e2e(frame: bytes) -> Optional[dict]:
    """Parse the 20B E2E header at the start of `frame`.

    Returns dict {crc, length, counter, data_id} or None if too short.
    """
    if len(frame) < E2E_HEADER_SIZE:
        return None
    crc, length, counter, data_id = struct.unpack(">QIII", frame[:E2E_HEADER_SIZE])
    return {"crc": crc, "length": length, "counter": counter, "data_id": data_id}


def strip_e2e(frame: bytes, validate: bool = True
              ) -> Tuple[Optional[dict], bytes]:
    """Strip (and optionally CRC-verify) the E2E header from a stream payload.

    Args:
        frame: SOME/IP payload starting at the E2E header
        validate: verify CRC-64/XZ over frame[8:]

    Returns:
        (e2e_dict_with["crc_ok"], iso_payload). e2e_dict is None and payload
        empty if the frame is shorter than the E2E header.
    """
    e2e = parse_e2e(frame)
    if e2e is None:
        return None, b""
    if validate:
        e2e["crc_ok"] = (crc64_xz(frame[8:]) == e2e["crc"])
    return e2e, frame[E2E_HEADER_SIZE:]


def build_e2e_header(data_id: int, counter: int, payload: bytes) -> bytes:
    """Build a 20B E2E header (with computed CRC) for `payload`."""
    tail = struct.pack(">III", len(payload) & 0xFFFFFFFF,
                       counter & 0xFFFFFFFF, data_id & 0xFFFFFFFF)
    crc = crc64_xz(tail + payload)
    return struct.pack(">Q", crc) + tail


# ── RDI (Radar Detections, Event 0x8002) ──

RDI_HEADER_SIZE = 36
RDI_DETECTION_SIZE = 51
RDI_MAX_DETECTIONS = 4096


def parse_rdi_frame(payload: bytes) -> Optional[dict]:
    """Parse the RDI header (36B) of an ISO 23150 payload (post-E2E).

    Returns header dict with `num_detections` and `det_start` offset,
    or None if too short.
    """
    if len(payload) < RDI_HEADER_SIZE:
        return None
    return {
        "version": (payload[0], payload[1], payload[2]),
        "interface_id": payload[3],
        "num_serving_sensors": payload[4],
        "sensor_id": payload[5],
        "timestamp": struct.unpack_from("<Q", payload, 6)[0],
        "message_counter": struct.unpack_from("<I", payload, 14)[0],
        "interface_cycle_time": struct.unpack_from("<I", payload, 18)[0],
        "cycle_time_variation": payload[22],
        "data_qualifier": payload[23],
        "radial_velocity_begin": struct.unpack_from("<f", payload, 24)[0],
        "radial_velocity_end": struct.unpack_from("<f", payload, 28)[0],
        "detections_capability": struct.unpack_from("<H", payload, 32)[0],
        "num_detections": struct.unpack_from("<H", payload, 34)[0],
        "det_start": RDI_HEADER_SIZE,
    }


def parse_rdi_detection(payload: bytes, det_start: int, index: int
                        ) -> Optional[dict]:
    """Parse a single RDI detection (51B, little-endian)."""
    off = det_start + index * RDI_DETECTION_SIZE
    if off + RDI_DETECTION_SIZE > len(payload):
        return None
    return {
        "existence_probability": payload[off],
        "detection_id": struct.unpack_from("<H", payload, off + 1)[0],
        "object_id_reference": struct.unpack_from("<H", payload, off + 3)[0],
        "timestamp_difference": struct.unpack_from("<f", payload, off + 5)[0],
        "rcs": struct.unpack_from("<f", payload, off + 9)[0],
        "snr": struct.unpack_from("<f", payload, off + 13)[0],
        "ambiguity_grouping_id": struct.unpack_from("<H", payload, off + 17)[0],
        "radial_distance": struct.unpack_from("<f", payload, off + 19)[0],
        "azimuth": struct.unpack_from("<f", payload, off + 23)[0],
        "elevation": struct.unpack_from("<f", payload, off + 27)[0],
        "radial_distance_error": struct.unpack_from("<f", payload, off + 31)[0],
        "azimuth_error": struct.unpack_from("<f", payload, off + 35)[0],
        "elevation_error": struct.unpack_from("<f", payload, off + 39)[0],
        "radial_velocity": struct.unpack_from("<f", payload, off + 43)[0],
        "radial_velocity_error": struct.unpack_from("<f", payload, off + 47)[0],
    }


# ── SHII (Sensor Health, Event 0x8003) — variable length ──

SHII_MIN_SIZE = 32


def parse_shi(payload: bytes) -> Optional[dict]:
    """Parse a variable-length SHII payload (post-E2E)."""
    if len(payload) < SHII_MIN_SIZE:
        return None
    msg = {
        "version": (payload[0], payload[1], payload[2]),
        "interface_id": payload[3],
        "sensor_id": payload[5],
        "timestamp": struct.unpack_from("<Q", payload, 6)[0],
        "message_counter": struct.unpack_from("<I", payload, 14)[0],
        "data_qualifier": payload[23],
    }
    off = 24

    n_op = payload[off]; off += 1
    msg["operation_modes"] = list(payload[off:off + n_op]); off += n_op

    msg["defect_recognised"] = payload[off]; off += 1
    msg["defect_reason"] = payload[off]; off += 1
    msg["supply_voltage_status"] = payload[off]; off += 1
    msg["temperature_status"] = payload[off]; off += 1

    if off >= len(payload):
        return None
    n_in = payload[off]; off += 1
    msg["input_signal_types"] = list(payload[off:off + n_in]); off += n_in
    msg["input_signal_statuses"] = list(payload[off:off + n_in]); off += n_in

    if off >= len(payload):
        return None
    msg["time_sync"] = payload[off]; off += 1

    if off >= len(payload):
        return None
    n_cal = payload[off]; off += 1
    msg["calibration_components"] = list(payload[off:off + n_cal]); off += n_cal
    msg["calibration_statuses"] = list(payload[off:off + n_cal]); off += n_cal
    msg["calibration_states"] = list(payload[off:off + n_cal]); off += n_cal

    return msg


# ── SPI (Sensor Performance, Event 0x8004) ──

SPI_MIN_SIZE = 57  # 24 (CIH) + 1 (vcs) + 32 (pose)


def parse_spi(payload: bytes) -> Optional[dict]:
    """Parse an SPI payload (post-E2E). Pose is 32B in v3.0."""
    if len(payload) < SPI_MIN_SIZE:
        return None
    msg = {
        "version": (payload[0], payload[1], payload[2]),
        "interface_id": payload[3],
        "sensor_id": payload[5],
        "timestamp": struct.unpack_from("<Q", payload, 6)[0],
        "message_counter": struct.unpack_from("<I", payload, 14)[0],
        "vehicle_coord_system": payload[24],
    }
    pose = struct.unpack_from("<8f", payload, 25)
    msg["pose"] = {
        "origin_x": pose[0], "origin_y": pose[1], "origin_z": pose[2],
        "yaw": pose[3], "pitch": pose[4], "roll": pose[5],
        "yaw_error": pose[6], "pitch_error": pose[7],
    }
    off = 57

    if off < len(payload):
        n_fov = payload[off]; off += 1
        segments = []
        for _ in range(n_fov):
            if off + 17 > len(payload):
                break
            az0, az1, el0, el1 = struct.unpack_from("<4f", payload, off)
            segments.append({
                "azimuth_begin": az0, "azimuth_end": az1,
                "elevation_begin": el0, "elevation_end": el1,
                "blockage_status": payload[off + 16],
            })
            off += 17
        msg["fov_segments"] = segments

    return msg


# ── CSII (Common Sensor Input, Event 0x8001, Host → Radar) ──

CSII_PAYLOAD_SIZE = 79
CSII_FRAME_SIZE = 16 + E2E_HEADER_SIZE + CSII_PAYLOAD_SIZE  # 115

IID_COMMON_SENSOR_INPUT = 0x0E

# Sensor Operation Mode Command values
SOMC_INITIALIZING      = 0
SOMC_OFF               = 1
SOMC_DORMANCY          = 2
SOMC_LOW_POWER_MODE    = 3
SOMC_NORMAL_MODE       = 16
SOMC_NORMAL_MODE_SR    = 17
SOMC_NORMAL_MODE_LR    = 18
SOMC_NORMAL_MODE_SRLR  = 19
SOMC_NORMAL_MODE_USR   = 20
SOMC_START_CALIBRATION = 32
SOMC_START_CLEANING    = 33

# Gear positions
GEAR_NEUTRAL = 0
GEAR_PARKING = 1
GEAR_FORWARD = 2
GEAR_REVERSE = 3


def build_csii_payload(somc: int = SOMC_NORMAL_MODE,
                       sensor_id: int = 0,
                       timestamp_ns: int = 0,
                       message_counter: int = 0,
                       global_timestamp_utc: float = 0.0,
                       coord_system: int = 0,
                       velocity_mps: Tuple[float, float, float] = (0.0, 0.0, 0.0),
                       rotation_rate_rps: Tuple[float, float, float] = (0.0, 0.0, 0.0),
                       wheel_speeds_rps: Tuple[float, float, float, float] = (0.0, 0.0, 0.0, 0.0),
                       steering_angle_rad: float = 0.0,
                       gear_position: int = GEAR_NEUTRAL) -> bytes:
    """Serialize a 79B CSII ISO payload (little-endian)."""
    if timestamp_ns == 0:
        timestamp_ns = time.time_ns()
    cih = struct.pack("<BBBBBBQIIBB",
                      1, 0, 0,                     # interface version
                      IID_COMMON_SENSOR_INPUT, 1, sensor_id,
                      timestamp_ns, message_counter,
                      100_000_000,                 # cycle time: 100 ms
                      0, 0)                        # variation, data_qualifier
    soc_time = struct.pack("<Bd", somc, global_timestamp_utc)
    vs = struct.pack("<B11fB", coord_system,
                     velocity_mps[0], velocity_mps[1], velocity_mps[2],
                     rotation_rate_rps[0], rotation_rate_rps[1], rotation_rate_rps[2],
                     wheel_speeds_rps[0], wheel_speeds_rps[1],
                     wheel_speeds_rps[2], wheel_speeds_rps[3],
                     steering_angle_rad, gear_position)
    payload = cih + soc_time + vs
    assert len(payload) == CSII_PAYLOAD_SIZE
    return payload


def build_csii_frame(payload: bytes, counter: int, session_id: int) -> bytes:
    """Wrap a CSII payload with E2E (CRC computed) and SOME/IP headers."""
    e2e = build_e2e_header(E2E_DATA_ID_CSII, counter, payload)
    length = 8 + len(e2e) + len(payload)
    someip = struct.pack(">HHIHHBBBB",
                         SERVICE_ID, EVENT_CSII, length,
                         0x0001, session_id & 0xFFFF,
                         0x01, 0x01, 0x02, 0x00)  # proto, iface, NOTIFICATION, OK
    return someip + e2e + payload
