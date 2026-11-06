"""
AFI920 data stream helpers — E2E header (CRC-32) and ISO 23150 payload parsing.

Interface v1.1.0 wire format:
  SOME/IP header (16B, BE) + E2E header (20B, BE) + ISO 23150 payload (LE)

E2E header layout: CRC high (4B, 0) | CRC-32 (4B) | Length (4B) | Counter (4B)
| Data ID (4B). The IEEE 802.3 CRC-32 (zlib.crc32) covers the body only.
Interface v1.0.x (firmware 3.0.0 to 3.1.0) keeps the layout but puts a
CRC-64/XZ in the first 8 bytes, over Length, Counter, Data ID and the body;
``strip_e2e`` accepts it on receive (this SDK release only).

Dependencies: Python 3.8+ (stdlib only)

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import logging
import struct
import time
import zlib
from typing import Dict, Optional, Tuple

from afi_sdk.someip import (SERVICE_ID, EVENT_CSII, INTERFACE_GEN_V1_0,
                            INTERFACE_GEN_V1_1)

_log = logging.getLogger("afi_sdk.streams")

# ── E2E header (System Spec §7) ──

E2E_HEADER_SIZE = 20
E2E_SERVICE_PREFIX = 0x6000

E2E_DATA_ID_CSII = 0x60008001
E2E_DATA_ID_RDI  = 0x60008002
E2E_DATA_ID_SHII = 0x60008003
E2E_DATA_ID_SPI  = 0x60008004
E2E_DATA_ID_PMOI = 0x60008005

# IEEE 802.3 CRC-32 (zlib): poly 0x04C11DB7 reflected, init/xorout all-ones.
CRC32_CHECK = 0xCBF43926


def e2e_data_id(sub16: int) -> int:
    """E2E Data ID: Service 0x6000 in the high half, channel id in the low half.

    The Config API uses the Method ID.
    """
    return (E2E_SERVICE_PREFIX << 16) | (sub16 & 0xFFFF)


def crc32_ieee(data: bytes) -> int:
    """IEEE 802.3 CRC-32, identical to ``zlib.crc32``."""
    return zlib.crc32(data) & 0xFFFFFFFF


assert crc32_ieee(b"123456789") == CRC32_CHECK, "CRC-32 self-test failed"


_CRC64_REFLECTED_POLY = 0xC96C5795D7870F42
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
    """CRC-64/XZ, the interface v1.0.x E2E CRC (v1.1.0 uses crc32_ieee)."""
    crc = _MASK64
    for b in data:
        crc = ((crc >> 8) ^ _CRC64_TABLE[(crc ^ b) & 0xFF]) & _MASK64
    return crc ^ _MASK64


assert crc64_xz(b"123456789") == CRC64_CHECK, "CRC-64/XZ self-test failed"


def parse_e2e(frame: bytes) -> Optional[dict]:
    """Parse the 20B E2E header at the start of `frame`.

    Returns dict {crc_high, crc, length, counter, data_id} or None if too short.
    """
    if len(frame) < E2E_HEADER_SIZE:
        return None
    crc_high, crc, length, counter, data_id = struct.unpack(
        ">IIIII", frame[:E2E_HEADER_SIZE])
    return {"crc_high": crc_high, "crc": crc, "length": length,
            "counter": counter, "data_id": data_id}


_e2e_failures: Dict[int, int] = {}


def strip_e2e(frame: bytes, validate: bool = True,
              expected_data_id: Optional[int] = None
              ) -> Tuple[Optional[dict], bytes]:
    """Strip and verify the E2E header of a payload.

    Verification never removes the body: a failed check is logged as a
    warning and the body after the 20B header is still returned.

    Args:
        frame: payload starting at the E2E header
        validate: recompute the CRC (CRC-32 over the body; CRC-64/XZ over
            Length, Counter, Data ID and the body when the high word is not 0)
        expected_data_id: channel Data ID, or None to skip the identity check

    Returns:
        (e2e_dict, body). The dictionary carries ``length_ok``,
        ``data_id_ok``, ``crc_zero``, ``crc_ok`` (when ``validate``), ``ok``
        (all checks passed) and ``generation`` (``INTERFACE_GEN_V1_1`` when
        the CRC high word is 0, ``INTERFACE_GEN_V1_0`` otherwise). It is None
        and the body is empty only when the frame is shorter than the E2E
        header.
    """
    e2e = parse_e2e(frame)
    if e2e is None:
        return None, b""
    body = frame[E2E_HEADER_SIZE:]
    e2e["length_ok"] = (e2e["length"] == len(body))
    e2e["data_id_ok"] = (
        expected_data_id is None or e2e["data_id"] == expected_data_id
    )
    # System Spec §7.5: a zero CRC field never counts as a pass.
    e2e["crc_zero"] = (e2e["crc_high"] == 0 and e2e["crc"] == 0)
    e2e["generation"] = (INTERFACE_GEN_V1_1 if e2e["crc_high"] == 0
                         else INTERFACE_GEN_V1_0)
    ok = e2e["length_ok"] and e2e["data_id_ok"]
    if validate:
        if e2e["crc_high"] == 0:
            e2e["crc_ok"] = (not e2e["crc_zero"]
                             and crc32_ieee(body) == e2e["crc"])
        else:
            e2e["crc_ok"] = (crc64_xz(frame[8:])
                             == (e2e["crc_high"] << 32) | e2e["crc"])
        ok = ok and e2e["crc_ok"]
    e2e["ok"] = ok
    if not ok:
        key = expected_data_id if expected_data_id is not None else e2e["data_id"]
        n = _e2e_failures.get(key, 0) + 1
        _e2e_failures[key] = n
        if n <= 10 or n % 1000 == 0:
            _log.warning(
                "E2E check failed for data_id 0x%08X (length_ok=%s data_id_ok=%s "
                "crc_ok=%s crc_zero=%s counter=%d failures=%d); body kept",
                e2e["data_id"], e2e["length_ok"], e2e["data_id_ok"],
                e2e.get("crc_ok"), e2e["crc_zero"], e2e["counter"], n)
    return e2e, body


def build_e2e_header(data_id: int, counter: int, payload: bytes) -> bytes:
    """Build a 20B E2E header (CRC-32 over `payload`) for `payload`."""
    return struct.pack(">IIIII", 0, crc32_ieee(payload),
                       len(payload) & 0xFFFFFFFF, counter & 0xFFFFFFFF,
                       data_id & 0xFFFFFFFF)


# ── SOME/IP-TP receive helper (UDP data streams) ──

class SomeipTpReassembler:
    """Bounded, session-aware SOME/IP-TP payload reassembler."""

    def __init__(self, max_payload_size: int, max_sessions: int = 8,
                 max_segment_size: int = 1392):
        if max_payload_size <= 0 or max_sessions <= 0 or max_segment_size <= 0:
            raise ValueError("reassembly limits must be positive")
        self.max_payload_size = max_payload_size
        self.max_sessions = max_sessions
        self.max_segment_size = max_segment_size
        self._assemblies = {}

    def reset(self, session_id: Optional[int] = None):
        """Discard one session, or every incomplete message if omitted."""
        if session_id is None:
            self._assemblies.clear()
        else:
            self._assemblies.pop(session_id, None)

    @staticmethod
    def _add_range(ranges, begin: int, end: int):
        merged = []
        for current in sorted(ranges + [(begin, end)]):
            if not merged or current[0] > merged[-1][1]:
                merged.append(current)
            else:
                merged[-1] = (merged[-1][0], max(merged[-1][1], current[1]))
        return merged

    def feed(self, tp_header: bytes, payload: bytes, session_id: int) -> bytes:
        """Feed one TP segment and return the complete payload, else ``b""``."""
        if len(tp_header) != 4 or not payload:
            return b""
        raw = struct.unpack(">I", tp_header)[0]
        if raw & 0x0E:
            self.reset(session_id)
            return b""
        offset = (raw >> 4) * 16
        more_segments = (raw & 0x01) != 0
        if (len(payload) > self.max_segment_size
                or (more_segments and len(payload) % 16 != 0)):
            self.reset(session_id)
            return b""
        end = offset + len(payload)
        if end > self.max_payload_size:
            self.reset(session_id)
            return b""

        if session_id not in self._assemblies:
            if len(self._assemblies) >= self.max_sessions:
                self._assemblies.pop(next(iter(self._assemblies)))
            self._assemblies[session_id] = {
                "buffer": bytearray(), "ranges": [], "expected": None,
            }
        state = self._assemblies[session_id]
        expected = state["expected"]
        if expected is not None and end > expected:
            self.reset(session_id)
            return b""

        buf = state["buffer"]
        if end > len(buf):
            buf.extend(b"\x00" * (end - len(buf)))
        buf[offset:end] = payload
        state["ranges"] = self._add_range(state["ranges"], offset, end)

        if not more_segments:
            if expected is not None and expected != end:
                self.reset(session_id)
                return b""
            state["expected"] = end
            expected = end
            if state["ranges"][-1][1] > expected:
                self.reset(session_id)
                return b""

        if (expected is not None and len(state["ranges"]) == 1
                and state["ranges"][0][0] == 0
                and state["ranges"][0][1] >= expected):
            result = bytes(buf[:expected])
            self.reset(session_id)
            return result
        return b""


# ── RDI (Radar Detections, Event 0x8002) ──

RDI_HEADER_SIZE = 36
RDI_DETECTION_SIZE = 51
RDI_MAX_DETECTIONS = 8192        # protocol limit; FW v3.1.1 sends at most 2048
RDI_MAX_PAYLOAD_SIZE = RDI_HEADER_SIZE + RDI_MAX_DETECTIONS * RDI_DETECTION_SIZE


def parse_rdi_frame(payload: bytes) -> Optional[dict]:
    """Parse the RDI header (36B) of an ISO 23150 payload (post-E2E).

    Returns header dict with `num_detections` and `det_start` offset,
    or None if too short.
    """
    if len(payload) < RDI_HEADER_SIZE:
        return None
    num_detections = struct.unpack_from("<H", payload, 34)[0]
    if num_detections > RDI_MAX_DETECTIONS:
        return None
    if len(payload) != RDI_HEADER_SIZE + num_detections * RDI_DETECTION_SIZE:
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
        "num_detections": num_detections,
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
SHII_MAX_OPERATION_MODES = 11
SHII_MAX_INPUT_SIGNALS = 4
SHII_MAX_CALIBRATION_COMPONENTS = 3


def _common_header(payload: bytes) -> dict:
    """ISO 23150 Common Header (24 B, 02_Common_Header): the same keys the
    RDI header uses. DataQualifier 1 means "do not use the content" and 6 a
    resend of the previous slot (System Spec §8.3)."""
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
    }


def parse_shi(payload: bytes) -> Optional[dict]:
    """Parse a variable-length SHII payload (post-E2E)."""
    if len(payload) < SHII_MIN_SIZE:
        return None
    off = 24

    n_op = payload[off]
    off += 1
    if n_op > SHII_MAX_OPERATION_MODES or off + n_op + 4 > len(payload):
        return None
    operation_modes = list(payload[off:off + n_op])
    off += n_op

    defect_recognised = payload[off]
    defect_reason = payload[off + 1]
    supply_voltage_status = payload[off + 2]
    temperature_status = payload[off + 3]
    off += 4

    if off >= len(payload):
        return None
    n_in = payload[off]
    off += 1
    if n_in > SHII_MAX_INPUT_SIGNALS or off + 2 * n_in + 1 > len(payload):
        return None
    input_signal_types = list(payload[off:off + n_in])
    off += n_in
    input_signal_statuses = list(payload[off:off + n_in])
    off += n_in

    time_sync = payload[off]
    off += 1

    if off >= len(payload):
        return None
    n_cal = payload[off]
    off += 1
    if (n_cal > SHII_MAX_CALIBRATION_COMPONENTS
            or off + 3 * n_cal != len(payload)):
        return None
    calibration_components = list(payload[off:off + n_cal])
    off += n_cal
    calibration_statuses = list(payload[off:off + n_cal])
    off += n_cal
    calibration_states = list(payload[off:off + n_cal])

    return {
        **_common_header(payload),
        "operation_modes": operation_modes,
        "defect_recognised": defect_recognised,
        "defect_reason": defect_reason,
        "supply_voltage_status": supply_voltage_status,
        "temperature_status": temperature_status,
        "input_signal_types": input_signal_types,
        "input_signal_statuses": input_signal_statuses,
        "time_sync": time_sync,
        "calibration_components": calibration_components,
        "calibration_statuses": calibration_statuses,
        "calibration_states": calibration_states,
    }


# ── SPI (Sensor Performance, Event 0x8004) ──

SPI_MIN_SIZE = 60  # 24 (CIH) + 1 (vcs) + 32 (pose) + 3 count fields
SPI_FOV_SEGMENT_SIZE = 17
SPI_OBJECT_TYPE_SIZE = 9
SPI_REF_TARGET_SIZE = 16
SPI_MAX_FOV_SEGMENTS = 16
SPI_MAX_OBJECT_TYPES = 16
SPI_MAX_REF_TARGETS = 16


def parse_spi(payload: bytes) -> Optional[dict]:
    """Parse an SPI payload (post-E2E): Common Header, vehicle coordinate
    system, 32 B pose, FoV segments, object types and reference targets."""
    if len(payload) < SPI_MIN_SIZE:
        return None
    msg = {
        **_common_header(payload),
        "vehicle_coord_system": payload[24],
    }
    pose = struct.unpack_from("<8f", payload, 25)
    msg["pose"] = {
        "origin_x": pose[0], "origin_y": pose[1], "origin_z": pose[2],
        "yaw": pose[3], "pitch": pose[4], "roll": pose[5],
        "yaw_error": pose[6], "pitch_error": pose[7],
    }
    off = 57

    n_fov = payload[off]
    off += 1
    if (n_fov > SPI_MAX_FOV_SEGMENTS
            or off + n_fov * SPI_FOV_SEGMENT_SIZE + 2 > len(payload)):
        return None
    segments = []
    for _ in range(n_fov):
        az0, az1, el0, el1 = struct.unpack_from("<4f", payload, off)
        segments.append({
            "azimuth_begin": az0, "azimuth_end": az1,
            "elevation_begin": el0, "elevation_end": el1,
            "blockage_status": payload[off + 16],
        })
        off += SPI_FOV_SEGMENT_SIZE

    n_obj = payload[off]
    off += 1
    if (n_obj > SPI_MAX_OBJECT_TYPES
            or off + n_obj * SPI_OBJECT_TYPE_SIZE + 1 > len(payload)):
        return None
    object_types = []
    for _ in range(n_obj):
        range_begin, range_end = struct.unpack_from("<2f", payload, off + 1)
        object_types.append({
            "object_type": payload[off],
            "detection_range_begin": range_begin,
            "detection_range_end": range_end,
        })
        off += SPI_OBJECT_TYPE_SIZE

    n_ref = payload[off]
    off += 1
    if (n_ref > SPI_MAX_REF_TARGETS
            or off + n_ref * SPI_REF_TARGET_SIZE != len(payload)):
        return None
    reference_targets = []
    for _ in range(n_ref):
        rcs, range_begin, range_end, snr = struct.unpack_from("<4f", payload, off)
        reference_targets.append({
            "radar_cross_section": rcs,
            "detection_range_begin": range_begin,
            "detection_range_end": range_end,
            "signal_to_noise_ratio": snr,
        })
        off += SPI_REF_TARGET_SIZE

    msg["fov_segments"] = segments
    msg["recognisable_object_types"] = object_types
    msg["reference_target_types"] = reference_targets

    return msg


# ── PMOI (Potentially Moving Objects, Event 0x8005) ──

PMOI_HEADER_SIZE = 30
PMOI_OBJECT_SIZE = 116
PMOI_MAX_OBJECTS = 255
PMOI_MAX_CLASSIFICATIONS = 5
PMOI_MAX_PAYLOAD_SIZE = PMOI_HEADER_SIZE + PMOI_MAX_OBJECTS * PMOI_OBJECT_SIZE


def parse_pmoi_frame(payload: bytes) -> Optional[dict]:
    """Parse a complete PMOI ISO 23150 payload (post-E2E)."""
    if len(payload) < PMOI_HEADER_SIZE:
        return None
    num_objects = struct.unpack_from("<H", payload, 28)[0]
    if num_objects > PMOI_MAX_OBJECTS:
        return None
    if len(payload) != PMOI_HEADER_SIZE + num_objects * PMOI_OBJECT_SIZE:
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
        "motion_type": payload[24],
        "objects_capability": struct.unpack_from("<H", payload, 25)[0],
        "objects_status": payload[27],
        "num_objects": num_objects,
        "object_start": PMOI_HEADER_SIZE,
    }


def parse_pmoi_object(payload: bytes, object_start: int, index: int
                      ) -> Optional[dict]:
    """Parse one fixed-size PMOI object entity (116B, little-endian)."""
    off = object_start + index * PMOI_OBJECT_SIZE
    if index < 0 or off < 0 or off + PMOI_OBJECT_SIZE > len(payload):
        return None

    num_classifications = payload[off + 11]
    if num_classifications > PMOI_MAX_CLASSIFICATIONS:
        return None
    classifications = [
        {"type": payload[off + 12 + i * 2],
         "confidence": payload[off + 13 + i * 2]}
        for i in range(num_classifications)
    ]

    values = struct.unpack_from("<22f", payload, off + 23)
    return {
        "existence_probability": struct.unpack_from("<f", payload, off)[0],
        "object_id": payload[off + 4],
        "object_grouping_id": payload[off + 5],
        "age": struct.unpack_from("<f", payload, off + 6)[0],
        "measurement_status": payload[off + 10],
        "num_classifications": num_classifications,
        "classifications": classifications,
        "reference_point": payload[off + 22],
        "position_x": values[0], "position_y": values[1], "position_z": values[2],
        "position_error_x": values[3], "position_error_y": values[4],
        "position_error_z": values[5],
        "orientation_yaw": values[6],
        "bounding_box_length": values[7], "bounding_box_width": values[8],
        "bounding_box_height": values[9],
        "velocity_x": values[10], "velocity_y": values[11], "velocity_z": values[12],
        "velocity_error_x": values[13], "velocity_error_y": values[14],
        "velocity_error_z": values[15],
        "acceleration_x": values[16], "acceleration_y": values[17],
        "acceleration_z": values[18],
        "acceleration_error_x": values[19],
        "acceleration_error_y": values[20],
        "acceleration_error_z": values[21],
        "movement_status": payload[off + 111],
        "radar_cross_section": struct.unpack_from("<f", payload, off + 112)[0],
    }


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
                       sensor_id: int = 1,
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
                      1, 1, 0,                     # interface version 1.1.0
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
