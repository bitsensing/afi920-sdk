"""
Unit tests: afi_sdk.streams — E2E (CRC-32, v1.1.0) + ISO 23150 parsers.

Run: pytest test/test_python_streams.py  (or python -m pytest)
No sensor required.
"""

import struct
import sys
import zlib
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))

from afi_sdk import streams  # noqa: E402
from afi_sdk.someip import INTERFACE_GEN_V1_0, INTERFACE_GEN_V1_1  # noqa: E402

TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))
import rdi_udp_sender  # noqa: E402


# ── CRC-32 (IEEE 802.3) ──

def test_crc32_known_answer():
    assert streams.crc32_ieee(b"123456789") == 0xCBF43926
    assert streams.crc32_ieee(b"123456789") == zlib.crc32(b"123456789")


# ── E2E header ──

def test_crc64_known_answer():
    assert streams.crc64_xz(b"123456789") == 0x995DC9BBDF1939FA


LEGACY_DIR = Path(__file__).resolve().parent / "data" / "legacy_v1_0"


def _legacy_frames(name):
    data = (LEGACY_DIR / name).read_bytes()
    out, off = [], 0
    while off + 16 <= len(data):
        length = struct.unpack_from(">I", data, off + 4)[0]
        out.append(data[off:off + 8 + length])
        off += 8 + length
    return out


def test_legacy_v1_0_frames_validate_and_parse():
    """Interface v1.0.x frames captured from a firmware 3.0.0 sensor."""
    cases = [("rdi_3frames.bin", streams.E2E_DATA_ID_RDI, streams.parse_rdi_frame),
             ("shii_3frames.bin", streams.E2E_DATA_ID_SHII, streams.parse_shi),
             ("spi_3frames.bin", streams.E2E_DATA_ID_SPI, streams.parse_spi)]
    for name, did, parse in cases:
        frames = _legacy_frames(name)
        assert len(frames) == 3, name
        for i, frame in enumerate(frames):
            e2e, body = streams.strip_e2e(frame[16:], expected_data_id=did)
            assert e2e["ok"] and e2e["crc_high"] != 0, name
            assert e2e["generation"] == INTERFACE_GEN_V1_0
            assert e2e["counter"] == i + 1
            msg = parse(body)
            assert msg is not None, name
            assert tuple(msg["version"]) == (1, 0, 0)
            # The v1.0.x CRC-64 also covers Counter, unlike the v1.1.0 CRC-32.
            bad = bytearray(frame[16:])
            bad[15] ^= 1
            assert not streams.strip_e2e(bytes(bad), expected_data_id=did)[0]["ok"]
            bad = bytearray(frame[16:])
            bad[-1] ^= 0x80
            assert not streams.strip_e2e(bytes(bad), expected_data_id=did)[0]["ok"]


def test_e2e_generation_of_a_v110_frame():
    body = b"\x01\x02\x03"
    e2e, _ = streams.strip_e2e(streams.build_e2e_header(0x60008002, 1, body) + body)
    assert e2e["ok"] and e2e["generation"] == INTERFACE_GEN_V1_1


def test_e2e_header_layout():
    hdr = streams.build_e2e_header(streams.E2E_DATA_ID_RDI, 0x01020304,
                                   b"123456789")
    assert hdr == bytes.fromhex(
        "00000000" "CBF43926" "00000009" "01020304" "60008002")


def test_e2e_build_and_strip_roundtrip():
    payload = bytes(range(32))
    hdr = streams.build_e2e_header(streams.E2E_DATA_ID_RDI, 77, payload)
    assert len(hdr) == streams.E2E_HEADER_SIZE

    e2e, iso = streams.strip_e2e(
        hdr + payload, expected_data_id=streams.E2E_DATA_ID_RDI)
    assert e2e["crc_ok"] and e2e["ok"]
    assert e2e["length"] == 32
    assert e2e["counter"] == 77
    assert e2e["data_id"] == streams.E2E_DATA_ID_RDI
    assert iso == payload


def test_e2e_crc_mismatch_detected_and_body_kept():
    payload = b"\xaa" * 16
    hdr = streams.build_e2e_header(streams.E2E_DATA_ID_SHII, 1, payload)
    corrupted = hdr + payload[:-1] + b"\x00"
    e2e, iso = streams.strip_e2e(corrupted)
    assert not e2e["crc_ok"] and not e2e["ok"]
    assert iso == corrupted[20:]


def test_e2e_header_fields_outside_crc():
    payload = b"\x01\x02\x03\x04"
    hdr = bytearray(streams.build_e2e_header(streams.E2E_DATA_ID_SPI, 10, payload))
    hdr[15] ^= 0x01  # counter is not covered by the body-only CRC
    e2e, _ = streams.strip_e2e(bytes(hdr) + payload)
    assert e2e["crc_ok"] and e2e["counter"] == 11


def test_e2e_zero_crc_is_not_a_pass():
    payload = b"\x01\x02\x03\x04"
    hdr = bytearray(streams.build_e2e_header(streams.E2E_DATA_ID_SPI, 1, payload))
    hdr[0:8] = b"\x00" * 8
    e2e, iso = streams.strip_e2e(bytes(hdr) + payload)
    assert e2e["crc_zero"] and not e2e["crc_ok"] and not e2e["ok"]
    assert iso == payload


def test_e2e_too_short():
    e2e, iso = streams.strip_e2e(b"\x00" * 19)
    assert e2e is None
    assert iso == b""


def test_e2e_length_and_data_id_mismatch_reported_body_kept():
    payload = b"\x01\x02\x03\x04"
    hdr = bytearray(streams.build_e2e_header(streams.E2E_DATA_ID_SPI, 1, payload))
    hdr[11] = 3
    e2e, iso = streams.strip_e2e(bytes(hdr) + payload)
    assert not e2e["length_ok"] and not e2e["ok"]
    assert iso == payload

    valid = streams.build_e2e_header(streams.E2E_DATA_ID_SPI, 1, payload) + payload
    e2e, iso = streams.strip_e2e(
        valid, expected_data_id=streams.E2E_DATA_ID_RDI)
    assert not e2e["data_id_ok"] and not e2e["ok"]
    assert iso == payload


def test_e2e_data_ids():
    assert streams.E2E_DATA_ID_CSII == 0x60008001
    assert streams.E2E_DATA_ID_RDI == 0x60008002
    assert streams.E2E_DATA_ID_SHII == 0x60008003
    assert streams.E2E_DATA_ID_SPI == 0x60008004
    assert streams.E2E_DATA_ID_PMOI == 0x60008005
    assert streams.e2e_data_id(0x0020) == 0x60000020
    assert streams.e2e_data_id(0x00E1) == 0x600000E1


# ── SOME/IP-TP ──

def _tp_header(offset: int, more: bool) -> bytes:
    assert offset % 16 == 0
    return struct.pack(">I", ((offset // 16) << 4) | int(more))


def test_tp_out_of_order_requires_full_coverage():
    tp = streams.SomeipTpReassembler(64)
    assert tp.feed(_tp_header(16, False), b"B" * 16, 7) == b""
    assert tp.feed(_tp_header(0, True), b"A" * 16, 7) == b"A" * 16 + b"B" * 16


def test_tp_gap_and_oversize_rejected():
    tp = streams.SomeipTpReassembler(48)
    assert tp.feed(_tp_header(0, True), b"A" * 16, 1) == b""
    assert tp.feed(_tp_header(32, False), b"C" * 16, 1) == b""
    assert tp.feed(_tp_header(48, False), b"D" * 16, 2) == b""
    assert 2 not in tp._assemblies


def test_tp_invalid_segment_shape_rejected():
    tp = streams.SomeipTpReassembler(4096)
    assert tp.feed(_tp_header(0, False), b"A" * 1393, 1) == b""
    assert tp.feed(_tp_header(0, True), b"A" * 17, 1) == b""
    reserved_bits = struct.pack(">I", 0x02)
    assert tp.feed(reserved_bits, b"A" * 16, 1) == b""
    assert not tp._assemblies


# ── RDI ──

def _make_rdi(n: int) -> bytes:
    buf = bytearray(36 + n * 51)
    buf[0] = 1
    buf[3] = 0x08
    buf[4] = 1
    buf[5] = 3
    struct.pack_into("<Q", buf, 6, 1234567890)
    struct.pack_into("<I", buf, 14, 42)
    struct.pack_into("<f", buf, 24, -50.0)
    struct.pack_into("<f", buf, 28, 50.0)
    struct.pack_into("<H", buf, 32, 8192)
    struct.pack_into("<H", buf, 34, n)
    for i in range(n):
        off = 36 + i * 51
        buf[off] = 90
        struct.pack_into("<H", buf, off + 1, 100 + i)
        struct.pack_into("<H", buf, off + 3, 0xFFFF)
        struct.pack_into("<f", buf, off + 9, 10.5)
        struct.pack_into("<f", buf, off + 13, 20.0)
        struct.pack_into("<H", buf, off + 17, i)
        struct.pack_into("<f", buf, off + 19, 12.5 + i)
        struct.pack_into("<f", buf, off + 43, -3.5)
    return bytes(buf)


def test_rdi_layout_constants():
    assert streams.RDI_HEADER_SIZE == 36
    assert streams.RDI_DETECTION_SIZE == 51
    assert streams.RDI_MAX_DETECTIONS == 8192
    assert streams.RDI_MAX_PAYLOAD_SIZE == 36 + 8192 * 51


def test_rdi_parse_header_and_detections():
    payload = _make_rdi(3)
    hdr = streams.parse_rdi_frame(payload)
    assert hdr["num_detections"] == 3
    assert hdr["message_counter"] == 42
    assert hdr["sensor_id"] == 3
    assert hdr["detections_capability"] == 8192
    assert abs(hdr["radial_velocity_begin"] + 50.0) < 1e-6

    d0 = streams.parse_rdi_detection(payload, hdr["det_start"], 0)
    assert d0["existence_probability"] == 90
    assert d0["detection_id"] == 100
    assert d0["object_id_reference"] == 0xFFFF
    assert abs(d0["rcs"] - 10.5) < 1e-6
    assert abs(d0["radial_distance"] - 12.5) < 1e-6
    assert abs(d0["radial_velocity"] + 3.5) < 1e-6

    d2 = streams.parse_rdi_detection(payload, hdr["det_start"], 2)
    assert d2["detection_id"] == 102
    assert abs(d2["radial_distance"] - 14.5) < 1e-6


def test_rdi_short_rejected():
    assert streams.parse_rdi_frame(b"\x00" * 35) is None
    payload = _make_rdi(1)
    assert streams.parse_rdi_detection(payload, 36, 1) is None  # only 1 det
    assert streams.parse_rdi_frame(payload[:-1]) is None


def test_rdi_udp_sender_matches_current_wire_contract():
    payload = rdi_udp_sender.build_rdi_payload(7, 2, 123456789, 100_000_000)
    header = streams.parse_rdi_frame(payload)
    assert header is not None
    assert header["message_counter"] == 7
    assert header["num_detections"] == 2
    assert header["detections_capability"] == streams.RDI_MAX_DETECTIONS

    sent = []
    fake_socket = mock.Mock()
    fake_socket.sendto.side_effect = lambda packet, target: sent.append((packet, target))
    segment_count = rdi_udp_sender.send_rdi_frame_udp(
        fake_socket, ("127.0.0.1", 30509), payload, 3, 7)
    assert segment_count == 1
    frame = sent[0][0]
    assert struct.unpack_from(">HH", frame, 0) == (0x6000, 0x8002)
    e2e, iso = streams.strip_e2e(
        frame[16:], expected_data_id=streams.E2E_DATA_ID_RDI)
    assert e2e["crc_ok"] and e2e["counter"] == 7
    assert iso == payload

    large_payload = rdi_udp_sender.build_rdi_payload(
        8, 50, 123456790, 100_000_000)
    sent.clear()
    segment_count = rdi_udp_sender.send_rdi_frame_udp(
        fake_socket, ("127.0.0.1", 30509), large_payload, 4, 8)
    assert segment_count == 2
    reassembler = streams.SomeipTpReassembler(
        streams.E2E_HEADER_SIZE + streams.RDI_MAX_PAYLOAD_SIZE)
    protected = b""
    for packet, _ in sent:
        header = struct.unpack_from(">HHIHHBBBB", packet)
        assert header[0] == 0x6000 and header[1] == 0x8002
        assert header[7] == 0x22 and header[2] + 8 == len(packet)
        protected = reassembler.feed(packet[16:20], packet[20:], header[4])
    e2e, iso = streams.strip_e2e(
        protected, expected_data_id=streams.E2E_DATA_ID_RDI)
    assert e2e["crc_ok"] and e2e["counter"] == 8
    assert iso == large_payload


# ── PMOI ──

def _make_pmoi(n: int) -> bytes:
    buf = bytearray(streams.PMOI_HEADER_SIZE + n * streams.PMOI_OBJECT_SIZE)
    buf[0] = 1
    buf[3] = 0x02
    buf[4] = 1
    buf[5] = 4
    struct.pack_into("<Q", buf, 6, 9876543210)
    struct.pack_into("<I", buf, 14, 77)
    struct.pack_into("<I", buf, 18, 100_000_000)
    buf[24] = 0
    struct.pack_into("<H", buf, 25, streams.PMOI_MAX_OBJECTS)
    buf[27] = 1
    struct.pack_into("<H", buf, 28, n)
    for i in range(n):
        off = streams.PMOI_HEADER_SIZE + i * streams.PMOI_OBJECT_SIZE
        struct.pack_into("<f", buf, off, 95.0)
        buf[off + 4] = 10 + i
        buf[off + 5] = 2
        struct.pack_into("<f", buf, off + 6, 1.25)
        buf[off + 10] = 0
        buf[off + 11] = 2
        buf[off + 12:off + 16] = bytes([0, 80, 1, 20])
        buf[off + 22] = 1
        struct.pack_into("<22f", buf, off + 23,
                         *(float(v) + 0.5 for v in range(22)))
        buf[off + 111] = 0
        struct.pack_into("<f", buf, off + 112, 12.5)
    return bytes(buf)


def test_pmoi_layout_and_parse():
    assert streams.PMOI_HEADER_SIZE == 30
    assert streams.PMOI_OBJECT_SIZE == 116
    assert streams.PMOI_MAX_OBJECTS == 255
    assert streams.PMOI_MAX_PAYLOAD_SIZE == 30 + 255 * 116

    payload = _make_pmoi(2)
    hdr = streams.parse_pmoi_frame(payload)
    assert hdr["interface_id"] == 0x02
    assert hdr["version"] == (1, 0, 0)
    assert hdr["num_serving_sensors"] == 1
    assert hdr["sensor_id"] == 4
    assert hdr["timestamp"] == 9876543210
    assert hdr["message_counter"] == 77
    assert hdr["interface_cycle_time"] == 100_000_000
    assert hdr["motion_type"] == 0
    assert hdr["objects_capability"] == 255
    assert hdr["objects_status"] == 1
    assert hdr["num_objects"] == 2

    obj = streams.parse_pmoi_object(payload, hdr["object_start"], 0)
    assert obj["existence_probability"] == 95.0
    assert obj["object_id"] == 10
    assert obj["object_grouping_id"] == 2
    assert obj["age"] == 1.25
    assert obj["measurement_status"] == 0
    assert obj["num_classifications"] == 2
    assert obj["classifications"] == [
        {"type": 0, "confidence": 80},
        {"type": 1, "confidence": 20},
    ]
    assert obj["reference_point"] == 1
    float_fields = (
        "position_x", "position_y", "position_z",
        "position_error_x", "position_error_y", "position_error_z",
        "orientation_yaw",
        "bounding_box_length", "bounding_box_width", "bounding_box_height",
        "velocity_x", "velocity_y", "velocity_z",
        "velocity_error_x", "velocity_error_y", "velocity_error_z",
        "acceleration_x", "acceleration_y", "acceleration_z",
        "acceleration_error_x", "acceleration_error_y",
        "acceleration_error_z",
    )
    for index, field in enumerate(float_fields):
        assert abs(obj[field] - (index + 0.5)) < 1e-6
    assert obj["movement_status"] == 0
    assert obj["radar_cross_section"] == 12.5


def test_pmoi_malformed_rejected():
    assert streams.parse_pmoi_frame(b"\x00" * 29) is None
    assert streams.parse_pmoi_frame(_make_pmoi(1)[:-1]) is None
    payload = bytearray(_make_pmoi(1))
    payload[streams.PMOI_HEADER_SIZE + 11] = 6
    hdr = streams.parse_pmoi_frame(payload)
    assert hdr is not None
    assert streams.parse_pmoi_object(payload, hdr["object_start"], 0) is None


# ── SHII (variable length) ──

def _make_shi(n_op: int, n_in: int, n_cal: int) -> bytes:
    buf = bytearray(24)
    buf[0] = 1
    buf[3] = 0x0D
    buf[5] = 7
    struct.pack_into("<I", buf, 14, 99)
    modes = [0, 1, 2, 3, 4, 5, 10, 50, 100, 200, 201]
    buf += bytes([n_op]) + bytes(modes[:n_op])
    buf += bytes([1, 11])      # defect recognised / reason
    buf += bytes([2, 2])       # supply / temperature
    buf += bytes([n_in]) + bytes(range(n_in)) + bytes([0] * n_in)
    buf += bytes([3])          # time sync
    buf += bytes([n_cal]) + bytes(range(n_cal)) + bytes([1] * n_cal) + bytes([2] * n_cal)
    return bytes(buf)


def test_shi_parse_full():
    msg = streams.parse_shi(_make_shi(11, 4, 3))
    assert msg["sensor_id"] == 7
    assert msg["message_counter"] == 99
    assert msg["operation_modes"] == [0, 1, 2, 3, 4, 5, 10, 50, 100, 200, 201]
    assert msg["defect_reason"] == 11
    assert msg["input_signal_types"] == [0, 1, 2, 3]
    assert msg["time_sync"] == 3
    assert msg["calibration_components"] == [0, 1, 2]
    assert msg["calibration_statuses"] == [1, 1, 1]
    assert msg["calibration_states"] == [2, 2, 2]


def test_shi_parse_ultra_dual_range_operation_mode():
    payload = bytearray(_make_shi(1, 1, 1))
    payload[25] = 6  # SOM_Normal_UltraDualRange (v1.1.0)
    msg = streams.parse_shi(payload)
    assert msg["operation_modes"] == [6]


def test_shi_parse_minimal():
    payload = _make_shi(0, 0, 0)
    assert len(payload) == 32
    msg = streams.parse_shi(payload)
    assert msg["operation_modes"] == []
    assert msg["calibration_components"] == []


def test_shi_short_rejected():
    assert streams.parse_shi(b"\x00" * 31) is None
    payload = bytearray(_make_shi(1, 1, 1))
    payload[24] = streams.SHII_MAX_OPERATION_MODES + 1
    assert streams.parse_shi(payload) is None
    assert streams.parse_shi(_make_shi(1, 1, 1)[:-1]) is None
    assert streams.parse_shi(_make_shi(1, 1, 1) + b"\x00") is None


# ── SPI ──

def _make_spi(n_fov: int, n_obj: int = 0, n_ref: int = 0) -> bytes:
    buf = bytearray(57)
    buf[0] = 1
    buf[3] = 0x0C
    buf[5] = 2
    struct.pack_into("<I", buf, 14, 123)
    buf[24] = 1
    pose = (3.8, 0.0, 0.5, 0.01, -0.02, 0.03, 0.001, 0.002)
    struct.pack_into("<8f", buf, 25, *pose)
    buf += bytes([n_fov])
    for i in range(n_fov):
        buf += struct.pack("<4f", -1.0, 1.0, -0.3, 0.3) + bytes([0 if i == 0 else 3])
    buf += bytes([n_obj])
    for i in range(n_obj):
        buf += bytes([i]) + struct.pack("<2f", 1.0, 200.0)
    buf += bytes([n_ref])
    for _ in range(n_ref):
        buf += struct.pack("<4f", 10.0, 2.0, 150.0, 5.0)
    return bytes(buf)


def test_spi_parse_pose():
    msg = streams.parse_spi(_make_spi(0))
    assert msg["sensor_id"] == 2
    assert msg["vehicle_coord_system"] == 1
    assert abs(msg["pose"]["origin_x"] - 3.8) < 1e-6
    assert abs(msg["pose"]["roll"] - 0.03) < 1e-6
    assert abs(msg["pose"]["pitch_error"] - 0.002) < 1e-6
    assert "roll_error" not in msg["pose"]  # spi.ksy has no roll error


def test_spi_parse_fov():
    msg = streams.parse_spi(_make_spi(2))
    assert len(msg["fov_segments"]) == 2
    assert msg["fov_segments"][0]["blockage_status"] == 0
    assert msg["fov_segments"][1]["blockage_status"] == 3  # UNKNOWN


def test_spi_parse_object_and_reference_capabilities():
    msg = streams.parse_spi(_make_spi(0, 1, 1))
    assert msg["recognisable_object_types"][0] == {
        "object_type": 0,
        "detection_range_begin": 1.0,
        "detection_range_end": 200.0,
    }
    assert msg["reference_target_types"][0] == {
        "radar_cross_section": 10.0,
        "detection_range_begin": 2.0,
        "detection_range_end": 150.0,
        "signal_to_noise_ratio": 5.0,
    }


def test_spi_short_rejected():
    assert streams.parse_spi(b"\x00" * 59) is None
    payload = bytearray(_make_spi(0))
    payload[57] = streams.SPI_MAX_FOV_SEGMENTS + 1
    assert streams.parse_spi(payload) is None
    assert streams.parse_spi(_make_spi(2, 1, 1)[:-1]) is None
    assert streams.parse_spi(_make_spi(0) + b"\x00") is None


# ── CSII ──

def test_csii_payload_size_and_fields():
    p = streams.build_csii_payload(
        somc=streams.SOMC_START_CALIBRATION, sensor_id=4,
        timestamp_ns=987654321, message_counter=11,
        global_timestamp_utc=1718000000.5, coord_system=1,
        velocity_mps=(16.7, 0.0, 0.0),
        wheel_speeds_rps=(0.0, 0.0, 0.0, 33.3),
        steering_angle_rad=-0.2, gear_position=streams.GEAR_REVERSE)
    assert len(p) == 79
    assert p[3] == 0x0E
    assert p[5] == 4
    assert struct.unpack_from("<Q", p, 6)[0] == 987654321
    assert struct.unpack_from("<I", p, 14)[0] == 11
    assert p[24] == streams.SOMC_START_CALIBRATION
    assert struct.unpack_from("<d", p, 25)[0] == 1718000000.5
    assert p[33] == 1
    assert abs(struct.unpack_from("<f", p, 34)[0] - 16.7) < 1e-5
    assert abs(struct.unpack_from("<f", p, 70)[0] - 33.3) < 1e-5
    assert abs(struct.unpack_from("<f", p, 74)[0] + 0.2) < 1e-6
    assert p[78] == streams.GEAR_REVERSE


def test_csii_frame_roundtrip():
    p = streams.build_csii_payload(timestamp_ns=1)
    f = streams.build_csii_frame(p, counter=5, session_id=9)
    assert len(f) == 115

    svc, evt, length = struct.unpack_from(">HHI", f, 0)
    assert svc == 0x6000 and evt == 0x8001
    assert length == 8 + 20 + 79
    assert f[14] == 0x02  # NOTIFICATION

    e2e, iso = streams.strip_e2e(f[16:])
    assert e2e["crc_ok"]
    assert e2e["counter"] == 5
    assert e2e["data_id"] == streams.E2E_DATA_ID_CSII
    assert iso == p


if __name__ == "__main__":
    # Allow running without pytest
    failed = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"  PASS: {name}")
            except AssertionError as e:
                print(f"  FAIL: {name} ({e})")
                failed += 1
    print(f"\n{'FAILED' if failed else 'ALL PASS'} ({failed} failures)")
    sys.exit(1 if failed else 0)
