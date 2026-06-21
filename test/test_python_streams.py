"""
Unit tests: afi_sdk.streams — E2E Profile 7 + ISO 23150 v3.0 parsers.

Run: pytest test/test_python_streams.py  (or python -m pytest)
No sensor required.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))

from afi_sdk import streams  # noqa: E402


# ── CRC-64/XZ ──

def test_crc64_known_answer():
    assert streams.crc64_xz(b"123456789") == 0x995DC9BBDF1939FA


def test_crc64_empty():
    # CRC of empty data = init ^ xorout = 0
    assert streams.crc64_xz(b"") == 0


# ── E2E header ──

def test_e2e_build_and_strip_roundtrip():
    payload = bytes(range(32))
    hdr = streams.build_e2e_header(streams.E2E_DATA_ID_RDI, 77, payload)
    assert len(hdr) == streams.E2E_HEADER_SIZE

    e2e, iso = streams.strip_e2e(hdr + payload)
    assert e2e["crc_ok"]
    assert e2e["length"] == 32
    assert e2e["counter"] == 77
    assert e2e["data_id"] == streams.E2E_DATA_ID_RDI
    assert iso == payload


def test_e2e_crc_mismatch_detected():
    payload = b"\xaa" * 16
    hdr = streams.build_e2e_header(streams.E2E_DATA_ID_SHII, 1, payload)
    corrupted = hdr + payload[:-1] + b"\x00"
    e2e, _ = streams.strip_e2e(corrupted)
    assert not e2e["crc_ok"]


def test_e2e_too_short():
    e2e, iso = streams.strip_e2e(b"\x00" * 19)
    assert e2e is None
    assert iso == b""


def test_e2e_data_ids():
    assert streams.E2E_DATA_ID_CSII == 0x60008001
    assert streams.E2E_DATA_ID_RDI == 0x60008002
    assert streams.E2E_DATA_ID_SHII == 0x60008003
    assert streams.E2E_DATA_ID_SPI == 0x60008004


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
    struct.pack_into("<H", buf, 32, 4096)
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


def test_rdi_parse_header_and_detections():
    payload = _make_rdi(3)
    hdr = streams.parse_rdi_frame(payload)
    assert hdr["num_detections"] == 3
    assert hdr["message_counter"] == 42
    assert hdr["sensor_id"] == 3
    assert hdr["detections_capability"] == 4096
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


def test_shi_parse_minimal():
    payload = _make_shi(0, 0, 0)
    assert len(payload) == 32
    msg = streams.parse_shi(payload)
    assert msg["operation_modes"] == []
    assert msg["calibration_components"] == []


def test_shi_short_rejected():
    assert streams.parse_shi(b"\x00" * 31) is None


# ── SPI ──

def _make_spi(n_fov: int) -> bytes:
    buf = bytearray(57)
    buf[0] = 1
    buf[3] = 0x0C
    buf[5] = 2
    struct.pack_into("<I", buf, 14, 123)
    buf[24] = 1
    pose = (3.8, 0.0, 0.5, 0.01, -0.02, 0.03, 0.001, 0.002)
    struct.pack_into("<8f", buf, 25, *pose)
    if n_fov:
        buf += bytes([n_fov])
        for i in range(n_fov):
            buf += struct.pack("<4f", -1.0, 1.0, -0.3, 0.3) + bytes([0 if i == 0 else 3])
    return bytes(buf)


def test_spi_parse_pose():
    msg = streams.parse_spi(_make_spi(0))
    assert msg["sensor_id"] == 2
    assert msg["vehicle_coord_system"] == 1
    assert abs(msg["pose"]["origin_x"] - 3.8) < 1e-6
    assert abs(msg["pose"]["roll"] - 0.03) < 1e-6
    assert abs(msg["pose"]["pitch_error"] - 0.002) < 1e-6
    assert "roll_error" not in msg["pose"]  # removed in v3.0


def test_spi_parse_fov():
    msg = streams.parse_spi(_make_spi(2))
    assert len(msg["fov_segments"]) == 2
    assert msg["fov_segments"][0]["blockage_status"] == 0
    assert msg["fov_segments"][1]["blockage_status"] == 3  # UNKNOWN (new in v3.0)


def test_spi_short_rejected():
    assert streams.parse_spi(b"\x00" * 56) is None


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
