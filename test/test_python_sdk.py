"""Unit tests for high-level Python SDK contract validation (no sensor).

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))

from afi_sdk import (  # noqa: E402
    AfiSdk, AfiSdkConfig, AccessMode, OK, INVALID_PARAM,
    ACCESS_DENIED, SESSION_REQUIRED,
)
from afi_sdk.someip import (  # noqa: E402
    METHOD_GET_DETECTION_THRESHOLD,
    METHOD_SET_DETECTION_THRESHOLD,
    METHOD_SET_NETWORK_CONFIG,
    METHOD_SET_PTP_CONFIG,
    METHOD_GET_MODE,
    METHOD_SET_MODE,
)


class FakeClient:
    connected = True

    def __init__(self):
        self.calls = []
        self.detection_response = {
            "snr_threshold_db": 5,
            "cfar_table_first_frame": [5] * 19,
        }

    def request(self, method_id, payload=None):
        self.calls.append((method_id, payload))
        if method_id == METHOD_GET_DETECTION_THRESHOLD:
            return 0, self.detection_response
        if method_id == METHOD_SET_PTP_CONFIG:
            return 0, {"profile": payload["profile"], "restart_required": True}
        return 0, {}

    def request_raw(self, method_id, body):
        self.calls.append((method_id, bytes(body)))
        return 0, {"bytes_received": len(body)}


class ModeRadar(FakeClient):
    """Fake that follows the access-mode rules of 11_Access_Mode."""

    def __init__(self, mode=0):
        super().__init__()
        self.mode = mode

    def request(self, method_id, payload=None):
        self.calls.append((method_id, payload))
        if method_id == METHOD_GET_MODE:
            return 0, {"access_mode": self.mode, "elapsed_ms": 1,
                       "streams_halted": int(self.mode == 2)}
        if method_id == METHOD_SET_MODE:
            want = payload["access_mode"]
            if want == self.mode or self.mode == 2:
                return 0x0D, {}
            if want == 2 and self.mode == 0:
                return 0x0C, {}
            self.mode = want
            return 0, {"access_mode": want, "streams_halted": int(want == 2)}
        return 0, {}


def make_sdk():
    sdk = AfiSdk()
    sdk._client = FakeClient()
    return sdk


def test_config_has_pmoi_port():
    assert AfiSdkConfig().pmoi_port == 30512

    legacy = AfiSdkConfig(
        "127.0.0.1", 30000, 30001, 30002, 30003, 30004, 0.25, "udp", False)
    assert legacy.spi_port == 30004
    assert legacy.timeout == 0.25
    assert legacy.stream_transport == "udp"
    assert legacy.validate_e2e is False
    assert legacy.pmoi_port == 30512


def test_stream_destination_keys_match_v110():
    sdk = make_sdk()
    assert sdk.set_stream_destination("detection", "192.168.10.100", 30509, "udp") == OK
    assert sdk._client.calls[-1] == (
        METHOD_SET_NETWORK_CONFIG,
        {"detection_ip": "192.168.10.100", "detection_port": 30509,
         "detection_protocol": "udp"},
    )
    # v1.1.0 defines no objects_* keys (02_Network.md 02.2).
    calls = len(sdk._client.calls)
    assert sdk.set_stream_destination("objects", "192.168.10.100", 30512, "udp") == INVALID_PARAM
    assert len(sdk._client.calls) == calls
    assert sdk.set_stream_destination("unknown", "127.0.0.1", 1) == INVALID_PARAM


def test_detection_level_validation():
    sdk = make_sdk()
    assert sdk.set_detection_threshold(snr_db=5) == OK
    assert sdk._client.calls[-1] == (
        METHOD_SET_DETECTION_THRESHOLD, {"snr_threshold_db": 5})
    call_count = len(sdk._client.calls)
    for invalid in (0, 10, 0.5, 9.01, True, math.nan, math.inf):
        assert sdk.set_detection_threshold(snr_db=invalid) == INVALID_PARAM
    assert len(sdk._client.calls) == call_count
    assert sdk.set_detection_threshold(snr_db=5.5) == OK
    assert sdk._client.calls[-1] == (
        METHOD_SET_DETECTION_THRESHOLD, {"snr_threshold_db": 5.5})


def test_detection_response_validation():
    sdk = make_sdk()
    assert sdk.get_detection_threshold() == {
        "snr_threshold_db": 5,
        "cfar_table_first_frame": [5] * 19,
    }
    sdk._client.detection_response = {
        "snr_threshold_db": 5.5,
        "cfar_table_first_frame": [5] * 19,
    }
    assert sdk.get_detection_threshold()["snr_threshold_db"] == 5.5
    sdk._client.detection_response = {
        "snr_threshold_db": 9.5,
        "cfar_table_first_frame": [5] * 19,
    }
    assert sdk.get_detection_threshold() is None
    sdk._client.detection_response = {
        "snr_threshold_db": 5,
        "cfar_table_first_frame": [5] * 18,
    }
    assert sdk.get_detection_threshold() is None


def test_ptp_profiles_and_result():
    sdk = make_sdk()
    for profile in (0, 1, 2):
        assert sdk.set_ptp_config(profile) == OK
    assert sdk.set_ptp_config(3) == INVALID_PARAM
    assert sdk.set_ptp_config(True) == INVALID_PARAM

    result = sdk.set_ptp_config_with_result(1)
    assert result == {"profile": 1, "restart_required": True}
    assert sdk.set_ptp_config_with_result(4) is None


def test_set_mode_validates_before_sending():
    sdk = make_sdk()
    assert sdk.set_mode(AccessMode.NORMAL) == INVALID_PARAM
    assert sdk.set_mode(3) == INVALID_PARAM
    assert sdk._client.calls == []
    assert sdk.set_mode(AccessMode.DIAGNOSTIC) == OK
    assert sdk._client.calls[-1] == (METHOD_SET_MODE, {"access_mode": 1})


def test_enter_mode_walks_the_one_way_chain():
    for start, target, want, sets in [
            (0, AccessMode.DIAGNOSTIC, OK, [1]),
            (0, AccessMode.PROGRAMMING, OK, [1, 2]),
            (1, AccessMode.PROGRAMMING, OK, [2]),
            (1, AccessMode.DIAGNOSTIC, OK, []),
            (2, AccessMode.PROGRAMMING, OK, []),
            (2, AccessMode.DIAGNOSTIC, ACCESS_DENIED, [])]:
        sdk = AfiSdk()
        sdk._client = ModeRadar(start)
        assert sdk.enter_mode(target) == want, (start, target)
        sent = [p["access_mode"] for m, p in sdk._client.calls if m == METHOD_SET_MODE]
        assert sent == sets, (start, target)


def test_set_mode_reports_radar_refusals():
    sdk = AfiSdk()
    sdk._client = ModeRadar(0)
    assert sdk.set_mode(AccessMode.PROGRAMMING) == SESSION_REQUIRED
    assert sdk.set_mode(AccessMode.DIAGNOSTIC) == OK
    assert sdk.set_mode(AccessMode.DIAGNOSTIC) == ACCESS_DENIED


def test_set_network_config_drops_objects_keys():
    import warnings
    sdk = make_sdk()
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        sdk.set_network_config(detection_protocol="udp", objects_ip="1.2.3.4",
                               objects_port=30512)
    assert sdk._client.calls[-1] == (METHOD_SET_NETWORK_CONFIG,
                                     {"detection_protocol": "udp"})
    assert any(issubclass(w.category, DeprecationWarning) for w in caught)
    calls = len(sdk._client.calls)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        assert sdk.set_network_config(objects_port=30512) is None
    assert len(sdk._client.calls) == calls


def test_config_client_e2e_loopback():
    """Request and response carry the 20B E2E header (v1.1.0)."""
    import socket
    import struct
    import threading
    import zlib
    from afi_sdk.someip import ConfigClient, METHOD_GET_DEVICE_INFO

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    port = listener.getsockname()[1]
    seen = {}

    def recv_exact(conn, n):
        buf = b""
        while len(buf) < n:
            chunk = conn.recv(n - len(buf))
            if not chunk:
                raise OSError("closed")
            buf += chunk
        return buf

    def serve():
        conn, _ = listener.accept()
        with conn:
            # Request 0 is the GetDeviceInfo probe that opens the connection.
            for _ in range(2):
                hdr = recv_exact(conn, 16)
                svc, method, length, client, session = struct.unpack_from(
                    ">HHIHH", hdr)
                rest = recv_exact(conn, length - 8)
                crc_high, crc, blen, counter, did = struct.unpack_from(
                    ">IIIII", rest)
                body = rest[20:]
                seen.update(method=method, length=length, crc_ok=(
                    crc_high == 0 and crc == zlib.crc32(body)), blen=blen,
                    did=did, body=body)
                reply = json.dumps({"return_code": 0, "return_name": "SUCCESS",
                                    "data": {"fw_hash": "9f99a7cd"}}).encode()
                e2e = struct.pack(">IIIII", 0, zlib.crc32(reply), len(reply), 1,
                                  (0x6000 << 16) | method)
                conn.sendall(struct.pack(">HHIHHBBBB", svc, method,
                                         8 + 20 + len(reply), client, session,
                                         1, 1, 0x80, 0) + e2e + reply)

    t = threading.Thread(target=serve)
    t.start()
    client = ConfigClient("127.0.0.1", port, timeout=2.0)
    assert client.connect()
    rc, data = client.request(METHOD_GET_DEVICE_INFO)
    client.close()
    t.join()
    listener.close()

    assert rc == 0 and data == {"fw_hash": "9f99a7cd"}
    assert client.last_response_e2e["ok"]
    assert seen["method"] == METHOD_GET_DEVICE_INFO
    assert seen["length"] == 8 + 20 + 2
    assert seen["crc_ok"] and seen["blen"] == 2 and seen["body"] == b"{}"
    assert seen["did"] == 0x60000010


if __name__ == "__main__":
    failed = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"  PASS: {name}")
            except AssertionError as exc:
                print(f"  FAIL: {name} ({exc})")
                failed += 1
    print(f"\n{'FAILED' if failed else 'ALL PASS'} ({failed} failures)")
    sys.exit(1 if failed else 0)
