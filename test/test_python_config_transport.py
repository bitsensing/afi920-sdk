"""
Config API transport behaviour of the Python ConfigClient (no sensor).

A scripted fake radar answers each request; the tests pin what the client does
when an answer is wrong, late or missing. The contract is Config API Overview
§1 and System Spec §6/§7: the radar echoes Service/Method/Client/Session, uses
protocol and interface version 0x01, puts the same return code in the header
and the JSON body, and every frame carries the 20B E2E header.

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import json
import socket
import struct
import sys
import threading
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))

from afi_sdk import (  # noqa: E402
    AfiSdk, AfiSdkConfig, OK, SESSION_REQUIRED, ACCESS_DENIED, BUSY,
    INVALID_STATE, NOT_SUPPORTED, INVALID_PARAM, SENSOR_ERROR, TIMEOUT,
    PROTOCOL_ERROR, DISCONNECTED, NOT_INITIALIZED,
)
from afi_sdk.someip import (  # noqa: E402
    ConfigClient, ERR_PROTOCOL, ERR_TIMEOUT, ERR_TOO_LARGE, ERR_TRANSPORT,
    MAX_REQUEST_BODY, METHOD_GET_DEVICE_INFO, METHOD_SET_ROTATION,
    METHOD_RESTART, METHOD_RESET_ALL_SETTINGS, METHOD_FW_SWITCH_SLOT,
    METHOD_FW_UPDATE_REBOOT, RC_NOT_SUPPORTED, INTERFACE_GEN_UNKNOWN,
    INTERFACE_GEN_V1_0, INTERFACE_GEN_V1_1, discovery_generation,
)


def _recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise OSError("closed")
        buf += chunk
    return buf


def reply_frame(req, body_obj, *, rc=0, method=None, session=None,
                proto=1, iface=1, msg_type=0x80, corrupt_crc=False):
    body = json.dumps(body_obj).encode()
    method = req["method"] if method is None else method
    crc = zlib.crc32(body) ^ (1 if corrupt_crc else 0)
    e2e = struct.pack(">IIIII", 0, crc, len(body), 1, (0x6000 << 16) | method)
    hdr = struct.pack(">HHIHHBBBB", 0x6000, method, 8 + 20 + len(body),
                      req["client"], req["session"] if session is None else session,
                      proto, iface, msg_type, rc)
    return hdr + e2e + body


LEGACY_DIR = Path(__file__).resolve().parent / "data" / "legacy_v1_0"

# JSON body of the GetDeviceInfo answer a firmware 3.0.0 sensor (interface
# v1.0.x) gave to a v1.1.0 request.
LEGACY_DEVICE_INFO = (LEGACY_DIR / "getdeviceinfo_rsp.bin").read_bytes()[16:]


def legacy_reply(req, body):
    """Interface v1.0.x framing: SOME/IP header and JSON, no E2E header."""
    return struct.pack(">HHIHHBBBB", 0x6000, req["method"], 8 + len(body),
                       req["client"], req["session"], 1, 1, 0x80, 0) + body


class FakeRadar:
    """Accepts connections in turn and answers each request with ``script``.

    ``script(index, request)`` returns bytes to send, or None to stay silent.
    A v1.1.0 radar answers the GetDeviceInfo probe that opens each
    connection itself, so the script and ``requests`` see only the calls
    under test. A ``legacy`` (v1.0.x) radar answers every request, the probe
    included, with LEGACY_DEVICE_INFO and records all of them.
    """

    def __init__(self, script, legacy=False):
        self.script = script
        self.legacy = legacy
        self.requests = []
        self.connections = 0
        self.probes = 0
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(4)
        self.listener.settimeout(5.0)
        self.port = self.listener.getsockname()[1]
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _serve(self):
        while not self._stop.is_set():
            try:
                conn, _ = self.listener.accept()
            except OSError:
                return
            self.connections += 1
            first = True
            with conn:
                conn.settimeout(5.0)
                while not self._stop.is_set():
                    try:
                        hdr = _recv_exact(conn, 16)
                    except OSError:
                        break
                    svc, method, length, client, session = struct.unpack_from(
                        ">HHIHH", hdr)
                    rest = _recv_exact(conn, length - 8)
                    req = {"method": method, "client": client, "session": session,
                           "body": rest[20:], "e2e": rest[:20]}
                    probe = first and method == METHOD_GET_DEVICE_INFO
                    first = False
                    self.probes += probe
                    if self.legacy:
                        self.requests.append(req)
                        conn.sendall(legacy_reply(req, LEGACY_DEVICE_INFO))
                        continue
                    if probe:
                        conn.sendall(ok_reply(req, {"serial_number": "probe"}))
                        continue
                    self.requests.append(req)
                    out = self.script(len(self.requests) - 1, req)
                    if out is not None:
                        conn.sendall(out)

    def close(self):
        self._stop.set()
        # On Linux close() alone does not wake the accept() in _serve, and the
        # socket keeps accepting until that call returns.
        try:
            self.listener.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.listener.close()


def ok_reply(req, data=None, rc=0, name="SUCCESS"):
    return reply_frame(req, {"return_code": rc, "return_name": name,
                             "data": data if data is not None else {}}, rc=rc)


def test_probe_finds_a_v110_sensor():
    radar = FakeRadar(lambda i, req: ok_reply(req))
    client = ConfigClient("127.0.0.1", radar.port, timeout=2.0)
    assert client.interface_generation == INTERFACE_GEN_UNKNOWN
    assert client.connect()
    assert client.interface_generation == INTERFACE_GEN_V1_1
    assert client.connected and radar.probes == 1 and radar.requests == []
    client.close()
    radar.close()


def test_legacy_sensor_gets_no_request_after_the_probe():
    radar = FakeRadar(lambda i, req: None, legacy=True)
    client = ConfigClient("127.0.0.1", radar.port, timeout=2.0)
    assert client.connect()
    assert client.interface_generation == INTERFACE_GEN_V1_0
    assert not client.connected      # the one Config API slot is left free
    assert len(radar.requests) == 1 and radar.probes == 1

    # The methods a v1.0.x sensor would run without arguments.
    for method in (METHOD_RESTART, METHOD_RESET_ALL_SETTINGS,
                   METHOD_FW_SWITCH_SLOT, METHOD_FW_UPDATE_REBOOT,
                   METHOD_GET_DEVICE_INFO):
        assert client.request(method) == (RC_NOT_SUPPORTED, None)
        assert client.last_result["return_name"] == "NOT_SUPPORTED"
        assert client.last_result["error_message"]
    assert client.request_raw(0x0081, b"chunk") == (RC_NOT_SUPPORTED, None)
    assert len(radar.requests) == 1 and radar.connections == 1

    # An explicit connect() probes again (a migrated sensor answers v1.1.0).
    assert client.connect()
    assert len(radar.requests) == 2 and radar.connections == 2
    client.close()
    radar.close()


def test_sdk_reports_not_supported_for_a_legacy_sensor():
    radar = FakeRadar(lambda i, req: None, legacy=True)
    sdk = AfiSdk()
    assert sdk.init(AfiSdkConfig(sensor_ip="127.0.0.1", config_port=radar.port,
                                 timeout=2.0)) == OK
    assert sdk.restart() == NOT_SUPPORTED
    assert sdk.reset_all_settings() == NOT_SUPPORTED
    assert sdk.get_device_info() is None and sdk.last_error == NOT_SUPPORTED
    assert sdk.last_result["return_name"] == "NOT_SUPPORTED"
    assert len(radar.requests) == 1
    sdk.close()
    radar.close()


class CsiiSink:
    """Loopback TCP and UDP listeners on one port number for CSII frames."""

    def __init__(self):
        self.tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp.bind(("127.0.0.1", 0))
        self.tcp.listen(1)
        self.port = self.tcp.getsockname()[1]
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", self.port))
        self.udp.settimeout(0.1)
        self.tcp_frames = 0
        self.udp_frames = 0
        self._stop = threading.Event()
        threading.Thread(target=self._tcp, daemon=True).start()
        threading.Thread(target=self._udp, daemon=True).start()

    def _tcp(self):
        try:
            conn, _ = self.tcp.accept()
        except OSError:
            return
        with conn:
            conn.settimeout(3.0)
            while True:
                try:
                    frame = _recv_exact(conn, 115)
                except OSError:
                    return
                if frame[:4] == b"\x60\x00\x80\x01":
                    self.tcp_frames += 1

    def _udp(self):
        while not self._stop.is_set():
            try:
                frame = self.udp.recv(256)
            except OSError:
                continue
            if len(frame) == 115 and frame[:4] == b"\x60\x00\x80\x01":
                self.udp_frames += 1

    def wait(self, attr, n):
        for _ in range(200):
            if getattr(self, attr) >= n:
                return True
            time.sleep(0.01)
        return False

    def close(self):
        self._stop.set()
        try:
            self.tcp.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.tcp.close()
        self.udp.close()


def test_sdk_sends_csii_over_tcp_to_a_legacy_sensor():
    radar = FakeRadar(lambda i, req: None, legacy=True)
    sink = CsiiSink()
    sdk = AfiSdk()
    assert sdk.init(AfiSdkConfig(sensor_ip="127.0.0.1", config_port=radar.port,
                                 csii_port=sink.port, timeout=2.0)) == OK
    assert sdk.interface_generation == INTERFACE_GEN_V1_0
    for _ in range(3):
        assert sdk.send_vehicle_input() == OK
    assert sink.wait("tcp_frames", 3) and sink.udp_frames == 0
    sdk.close()
    sink.close()
    radar.close()


def test_sdk_sends_csii_over_udp_to_a_v110_sensor():
    radar = FakeRadar(lambda i, req: ok_reply(req))
    sink = CsiiSink()
    sdk = AfiSdk()
    assert sdk.init(AfiSdkConfig(sensor_ip="127.0.0.1", config_port=radar.port,
                                 csii_port=sink.port, timeout=2.0)) == OK
    assert sdk.interface_generation == INTERFACE_GEN_V1_1
    for _ in range(3):
        assert sdk.send_vehicle_input() == OK
    assert sink.wait("udp_frames", 3) and sink.tcp_frames == 0
    sdk.close()
    sink.close()
    radar.close()


def test_csii_transport_setting_overrides_the_generation():
    radar = FakeRadar(lambda i, req: None, legacy=True)
    sink = CsiiSink()
    sdk = AfiSdk()
    assert sdk.init(AfiSdkConfig(sensor_ip="127.0.0.1", config_port=radar.port,
                                 csii_port=sink.port, timeout=2.0,
                                 csii_transport="udp")) == OK
    assert sdk.send_vehicle_input() == OK
    assert sink.wait("udp_frames", 1) and sink.tcp_frames == 0
    sdk.close()
    assert AfiSdk().init(AfiSdkConfig(csii_transport="sctp")) == INVALID_PARAM
    sink.close()
    radar.close()


def test_discovery_generation():
    raw = (LEGACY_DIR / "discovery_rsp.bin").read_bytes()
    legacy = json.loads(raw[16:].decode())["data"]
    assert discovery_generation(legacy) == INTERFACE_GEN_V1_0
    assert discovery_generation({"software_version": "0.0.0",
                                 "m72_ptp_state": "SLAVE"}) == INTERFACE_GEN_V1_1
    assert discovery_generation({"software_version": "0.0.0"}) == INTERFACE_GEN_UNKNOWN


def test_wrong_session_echo_is_a_protocol_error_and_drops_the_connection():
    def script(i, req):
        if i == 0:
            return reply_frame(req, {"return_code": 0, "data": {"x": 1}},
                               session=req["session"] + 5)
        return ok_reply(req, {"x": 2})

    radar = FakeRadar(script)
    client = ConfigClient("127.0.0.1", radar.port, timeout=2.0)
    assert client.connect()
    rc, data = client.request(METHOD_GET_DEVICE_INFO)
    assert (rc, data) == (ERR_PROTOCOL, None)
    assert not client.connected
    rc, data = client.request(METHOD_GET_DEVICE_INFO)   # reconnects
    assert (rc, data) == (0, {"x": 2})
    assert radar.connections == 2
    client.close()
    radar.close()


def test_wrong_method_or_version_is_rejected():
    for kwargs in ({"method": METHOD_SET_ROTATION}, {"proto": 2}, {"iface": 2},
                   {"msg_type": 0x02}):
        radar = FakeRadar(lambda i, req, kw=kwargs: reply_frame(
            req, {"return_code": 0, "data": {}}, **kw))
        client = ConfigClient("127.0.0.1", radar.port, timeout=2.0)
        assert client.connect()
        rc, _ = client.request(METHOD_GET_DEVICE_INFO)
        assert rc == ERR_PROTOCOL, kwargs
        client.close()
        radar.close()


def test_a_late_reply_is_never_read_as_the_next_answer():
    late = {}

    def script(i, req):
        if i == 0:
            late["frame"] = ok_reply(req, {"answer": "first"})
            return None                       # silent: the client times out
        return ok_reply(req, {"answer": "second"})

    radar = FakeRadar(script)
    client = ConfigClient("127.0.0.1", radar.port, timeout=0.3)
    assert client.connect()
    rc, _ = client.request(METHOD_GET_DEVICE_INFO)
    assert rc == ERR_TIMEOUT and not client.connected
    rc, data = client.request(METHOD_GET_DEVICE_INFO)
    assert (rc, data) == (0, {"answer": "second"})
    client.close()
    radar.close()


def test_body_above_the_request_limit_is_not_sent():
    radar = FakeRadar(lambda i, req: ok_reply(req))
    client = ConfigClient("127.0.0.1", radar.port, timeout=2.0)
    assert client.connect()
    rc, _ = client.request_raw(0x0081, b"\0" * (MAX_REQUEST_BODY + 1))
    assert rc == ERR_TOO_LARGE
    rc, _ = client.request_raw(0x0081, b"\0" * MAX_REQUEST_BODY)
    assert rc == 0
    assert [len(r["body"]) for r in radar.requests] == [MAX_REQUEST_BODY]
    client.close()
    radar.close()


def test_closed_client_does_not_reconnect():
    radar = FakeRadar(lambda i, req: ok_reply(req))
    client = ConfigClient("127.0.0.1", radar.port, timeout=2.0)
    assert client.connect()
    client.close()
    rc, _ = client.request(METHOD_GET_DEVICE_INFO)
    assert rc == ERR_TRANSPORT and radar.connections <= 1
    radar.close()


def test_peer_close_is_a_transport_error():
    radar = FakeRadar(lambda i, req: b"")      # accept, read, answer nothing
    client = ConfigClient("127.0.0.1", radar.port, timeout=0.3)
    assert client.connect()
    radar.close()
    rc, _ = client.request(METHOD_GET_DEVICE_INFO)
    assert rc in (ERR_TRANSPORT, ERR_TIMEOUT)
    assert not client.connected


def test_failed_response_e2e_is_reported_and_the_answer_kept():
    radar = FakeRadar(lambda i, req: reply_frame(
        req, {"return_code": 0, "data": {"fw_hash": "f8c75c6f"}},
        corrupt_crc=True))
    client = ConfigClient("127.0.0.1", radar.port, timeout=2.0)
    assert client.connect()
    rc, data = client.request(METHOD_GET_DEVICE_INFO)
    assert (rc, data) == (0, {"fw_hash": "f8c75c6f"})
    assert client.last_response_e2e["ok"] is False
    assert client.connected
    client.close()
    radar.close()


def test_radar_return_codes_map_to_distinct_sdk_errors():
    cases = [(0x00, "SUCCESS", OK), (0x01, "INVALID_PARAM", INVALID_PARAM),
             (0x05, "NOT_SUPPORTED", NOT_SUPPORTED), (0x07, "INTERNAL_ERROR", SENSOR_ERROR),
             (0x08, "BUSY", BUSY), (0x0C, "SESSION_REQUIRED", SESSION_REQUIRED),
             (0x0D, "ACCESS_DENIED", ACCESS_DENIED),
             (0x0E, "INVALID_STATE", INVALID_STATE)]
    it = iter(cases)

    def script(i, req):
        rc, name, _ = cases[i]
        return reply_frame(req, {"return_code": rc, "return_name": name,
                                 "data": {"error_message": f"m{rc}"} if rc else {}},
                           rc=rc)

    radar = FakeRadar(script)
    sdk = AfiSdk()
    assert sdk.init(AfiSdkConfig(sensor_ip="127.0.0.1", config_port=radar.port,
                                 timeout=2.0)) == OK
    for rc, name, want in it:
        got = sdk.set_rotation(0.0, 0.0, 0.0)
        assert got == want, name
        assert sdk.last_error == want
        assert sdk.last_result["return_code"] == rc
        assert sdk.last_result["return_name"] == name
        if rc:
            assert sdk.last_result["error_message"] == f"m{rc}"
    sdk.close()
    radar.close()


def test_sdk_reports_transport_errors_and_missing_init():
    sdk = AfiSdk()
    assert sdk.set_rotation(0.0, 0.0, 0.0) == NOT_INITIALIZED
    assert sdk.get_device_info() is None and sdk.last_error == NOT_INITIALIZED

    radar = FakeRadar(lambda i, req: None if i == 0 else
                      reply_frame(req, {"return_code": 0}, session=0))
    assert sdk.init(AfiSdkConfig(sensor_ip="127.0.0.1", config_port=radar.port,
                                 timeout=0.3)) == OK
    assert sdk.get_device_info() is None and sdk.last_error == TIMEOUT
    assert sdk.get_device_info() is None and sdk.last_error == PROTOCOL_ERROR
    radar.close()
    sdk._client._reconnect = True
    sdk._client._drop()
    assert sdk.get_device_info() is None and sdk.last_error == DISCONNECTED
    sdk.close()


def test_reconnect_after_restart():
    radar = FakeRadar(lambda i, req: ok_reply(req, {"n": i}))
    sdk = AfiSdk()
    assert sdk.init(AfiSdkConfig(sensor_ip="127.0.0.1", config_port=radar.port,
                                 timeout=2.0)) == OK
    sdk._client._drop()                          # what a radar reset leaves
    assert sdk.reconnect(timeout_s=2.0, interval_s=0.05) == OK
    assert sdk.get_device_info() == {"n": 0}
    sdk.close()
    radar.close()


# ── Discovery (UDP 30520) ──

def _discovery_responder(replies):
    """UDP peer that answers one request with each frame in ``replies``."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(3.0)

    def serve():
        try:
            _, peer = sock.recvfrom(4096)
        except OSError:
            return
        for frame in replies:
            sock.sendto(frame, peer)
        sock.close()

    threading.Thread(target=serve, daemon=True).start()
    return sock.getsockname()[1]


def _discovery_frame(info):
    body = json.dumps({"return_code": 0, "data": info}).encode()
    return struct.pack(">HHIHHBBBB", 0x6000, 0x00C1, 8 + len(body), 1, 1,
                       1, 1, 0x80, 0) + body


def test_discovery_dedups_by_source_ip_and_serial():
    from afi_sdk.someip import discover
    a = {"serial_number": "SN-A", "ip_address": "10.0.0.1"}
    b = {"serial_number": "SN-B", "ip_address": "10.0.0.1"}  # same claimed IP
    port = _discovery_responder([_discovery_frame(a), _discovery_frame(a),
                                 _discovery_frame(b)])
    found = discover(timeout_s=0.5, broadcast_ip="127.0.0.1", port=port)
    assert sorted(x["serial_number"] for x in found) == ["SN-A", "SN-B"]
    assert all(x["_source_ip"] == "127.0.0.1" for x in found)


def test_discovery_survives_an_unreachable_target():
    from afi_sdk.someip import discover
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    closed_port = probe.getsockname()[1]
    probe.close()                        # nothing listens there any more
    assert discover(timeout_s=0.3, broadcast_ip="127.0.0.1",
                    port=closed_port) == []
