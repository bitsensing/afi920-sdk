"""
AFI920 SOME/IP Client Library

Lightweight Python helper for AFI920 sensor communication.
Provides Config API (TCP) and Discovery (UDP) over SOME/IP protocol.

Interface v1.1.0: Config API requests and responses carry the 20B E2E header
(CRC-32 over the JSON body, Data ID 0x6000 << 16 | Method ID). Discovery has
no E2E header.

Dependencies: Python 3.8+ (stdlib only, no external packages required)

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import json
import logging
import socket
import struct
import time
import zlib
from typing import List, Optional, Tuple

_log = logging.getLogger("afi_sdk.someip")

# ── SOME/IP Constants ──

SERVICE_ID = 0x6000
PROTO_VER = 0x01
IFACE_VER = 0x01
MSG_TYPE_REQUEST = 0x00
MSG_TYPE_NOTIFICATION = 0x02
MSG_TYPE_RESPONSE = 0x80
MSG_TYPE_ERROR = 0x81      # defined; the radar answers errors as 0x80
MSG_TYPE_TP = 0x20

SOMEIP_HEADER_FMT = ">HHIHHBBBB"
CLIENT_ID = 0x0001
SOMEIP_HEADER_SIZE = 16
E2E_HEADER_FMT = ">IIIII"   # CRC high, CRC-32, Length, Counter, Data ID
E2E_HEADER_SIZE = 20

# ── Config API Method IDs (interface v1.1.0 registry) ──

# Device Info & Status (0x001x)
METHOD_GET_DEVICE_INFO          = 0x0010
METHOD_GET_SENSOR_STATUS        = 0x0011
METHOD_GET_DIAGNOSTIC_INFO      = 0x0012
METHOD_RESTART                  = 0x0014
METHOD_RESET_ALL_SETTINGS       = 0x0015
METHOD_GET_COMMAND_HISTORY      = 0x0016

# Network (0x002x) — unified radar network + stream destinations
METHOD_GET_NETWORK_CONFIG       = 0x0020
METHOD_SET_NETWORK_CONFIG       = 0x0021

# Range Mode (0x003x)
METHOD_GET_RANGE_MODE           = 0x0030
METHOD_SET_RANGE_MODE           = 0x0031

# Detection (0x004x)
METHOD_GET_DETECTION_THRESHOLD  = 0x0040
METHOD_SET_DETECTION_THRESHOLD  = 0x0041
METHOD_GET_DETECTION_FILTERS    = 0x0042
METHOD_SET_DETECTION_FILTERS    = 0x0043

# Time Sync (0x005x)
METHOD_GET_PTP_CONFIG           = 0x0050
METHOD_SET_PTP_CONFIG           = 0x0051

# Radar Software Update (0x006x) — reserved, firmware returns NOT_SUPPORTED

# Mounting (0x007x)
METHOD_GET_MOUNTING_POSITION    = 0x0070
METHOD_SET_MOUNTING_POSITION    = 0x0071
METHOD_GET_ROTATION             = 0x0072
METHOD_SET_ROTATION             = 0x0073
METHOD_GET_SENSOR_POSITION      = 0x0074
METHOD_SET_SENSOR_POSITION      = 0x0075

# Firmware Update (0x008x)
METHOD_FW_UPDATE_START          = 0x0080
METHOD_FW_DATA_CHUNK            = 0x0081
METHOD_FW_UPDATE_COMMIT         = 0x0082
METHOD_FW_UPDATE_STATUS         = 0x0083
METHOD_FW_UPDATE_ABORT          = 0x0084
METHOD_FW_SWITCH_SLOT           = 0x0085
METHOD_FW_UPDATE_REBOOT         = 0x0086
METHOD_GET_BOOT_INFO            = 0x0087

# Config Persistence (0x009x)
METHOD_GET_CONFIG_STATUS        = 0x0091
METHOD_RESET_CONFIG_DEFAULTS    = 0x0092

# Access Mode (0x00Ex)
METHOD_GET_MODE                 = 0x00E0
METHOD_SET_MODE                 = 0x00E1

ACCESS_MODE_NORMAL      = 0
ACCESS_MODE_DIAGNOSTIC  = 1   # opens every config SET
ACCESS_MODE_PROGRAMMING = 2   # opens the firmware update chain; halts data streams

# Discovery Method IDs (UDP 30520, separate channel)
METHOD_DISCOVERY_REQUEST        = 0x00C0
METHOD_DISCOVERY_RESPONSE       = 0x00C1

# ── Data Stream Event IDs ──

EVENT_CSII     = 0x8001   # Host -> Radar (UDP on v1.1.0, TCP on v1.0.x)
EVENT_RDI      = 0x8002
EVENT_SHII     = 0x8003
EVENT_SPI      = 0x8004
EVENT_PMOI     = 0x8005   # reserved; not sent on interface v1.1.0

# ── Default Ports ──

DEFAULT_CONFIG_PORT    = 30500
DEFAULT_CSII_PORT      = 30501
DEFAULT_RDI_PORT       = 30509
DEFAULT_SHII_PORT      = 30510
DEFAULT_SPI_PORT       = 30511
DEFAULT_PMOI_PORT      = 30512
DEFAULT_SYSLOG_PORT    = 30515
DEFAULT_DISCOVERY_PORT = 30520

# Safety limit for response payload size (matches C++ kMaxResponsePayload)
MAX_RESPONSE_PAYLOAD   = 4 * 1024 * 1024  # 4 MB

# Request body limit (Config API Overview §1). The radar closes the connection
# without a reply when the SOME/IP Length is below 28 or above 28 + this.
MAX_REQUEST_BODY       = 16384

# ── Return codes (System Spec §6) ──
# The radar sends the same code in the SOME/IP header and in the JSON body.

RC_SUCCESS                 = 0x00
RC_INVALID_PARAM           = 0x01
RC_CONNECTION_FAIL         = 0x02
RC_NO_DATA                 = 0x03
RC_MEMORY_ERROR            = 0x04
RC_NOT_SUPPORTED           = 0x05
RC_INTERNAL_ERROR          = 0x07
RC_BUSY                    = 0x08
RC_MODE_CHANGE_FAIL        = 0x09
RC_CALIBRATION_IN_PROGRESS = 0x0A
RC_TIMEOUT                 = 0x0B
RC_SESSION_REQUIRED        = 0x0C
RC_ACCESS_DENIED           = 0x0D
RC_INVALID_STATE           = 0x0E

RETURN_CODE_NAMES = {
    RC_SUCCESS: "SUCCESS", RC_INVALID_PARAM: "INVALID_PARAM",
    RC_CONNECTION_FAIL: "CONNECTION_FAIL", RC_NO_DATA: "NO_DATA",
    RC_MEMORY_ERROR: "MEMORY_ERROR", RC_NOT_SUPPORTED: "NOT_SUPPORTED",
    RC_INTERNAL_ERROR: "INTERNAL_ERROR", RC_BUSY: "BUSY",
    RC_MODE_CHANGE_FAIL: "MODE_CHANGE_FAIL",
    RC_CALIBRATION_IN_PROGRESS: "CALIBRATION_IN_PROGRESS",
    RC_TIMEOUT: "TIMEOUT", RC_SESSION_REQUIRED: "SESSION_REQUIRED",
    RC_ACCESS_DENIED: "ACCESS_DENIED", RC_INVALID_STATE: "INVALID_STATE",
}

# Transport results of ConfigClient.request (negative, never sent by a radar)
ERR_TRANSPORT = -1   # not connected, connect/send failed, or peer closed
ERR_TIMEOUT   = -2   # no complete response within the timeout
ERR_PROTOCOL  = -3   # response header or body does not belong to the request
ERR_TOO_LARGE = -4   # request body above MAX_REQUEST_BODY; nothing was sent

# Interface generation a sensor speaks (same values as C++
# afi::AfiInterfaceGeneration). RDI, SHII and SPI are received from both and
# CSII is sent to both; the Config API is v1.1.0 only. Compatibility with
# v1.0.x is limited to this SDK release.
INTERFACE_GEN_UNKNOWN = 0
INTERFACE_GEN_V1_0 = 1   # interface v1.0.x, firmware 3.0.0 to 3.1.0
INTERFACE_GEN_V1_1 = 2   # interface v1.1.0, firmware 3.1.1

TRANSPORT_ERROR_NAMES = {ERR_TRANSPORT: "TRANSPORT", ERR_TIMEOUT: "TIMEOUT",
                         ERR_PROTOCOL: "PROTOCOL", ERR_TOO_LARGE: "TOO_LARGE"}

# ── Range Mode (registry: 0=DR, 1=LR, 2=MR, 3=SR, 4=ULR, 5=USR, 6=UDR) ──

RANGE_MODE_DR  = 0     # Dual Range
RANGE_MODE_LR  = 1     # Long Range
RANGE_MODE_MR  = 2     # Middle Range
RANGE_MODE_SR  = 3     # Short Range
RANGE_MODE_ULR = 4     # Ultra Long Range
RANGE_MODE_USR = 5     # Ultra Short Range
RANGE_MODE_UDR = 6     # Ultra Dual Range (v1.1.0)

RANGE_MODE_NAMES = {0: "DR", 1: "LR", 2: "MR", 3: "SR", 4: "ULR", 5: "USR",
                    6: "UDR"}



class ConfigClient:
    """SOME/IP Config API client over TCP (interface v1.1.0).

    Every request and response carries the 20B E2E header. A response whose
    E2E check fails is logged and still returned; ``last_response_e2e["ok"]``
    holds the verdict (System Spec §7.5).

    Any transport or framing failure drops the connection: a late response
    left in the socket would otherwise be read as the answer to the next
    request. The next request reconnects, unless ``close()`` was called.

    Every connection starts with a GetDeviceInfo probe that sets
    ``interface_generation``. On an interface v1.0.x sensor every request
    returns ``RC_NOT_SUPPORTED`` without sending (see ``_probe``).

    Example::

        client = ConfigClient("192.168.10.150")
        client.connect()
        rc, data = client.request(METHOD_GET_DEVICE_INFO)
        print(data)
        client.close()
    """

    def __init__(self, host: str, port: int = DEFAULT_CONFIG_PORT,
                 timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock: Optional[socket.socket] = None
        self._session_id = 0
        self._e2e_counter = 0
        self._reconnect = False
        self.last_response_e2e: Optional[dict] = None
        #: Outcome of the last request: return_code, return_name,
        #: error_message (firmware update methods), data and e2e_ok.
        self.last_result: Optional[dict] = None
        #: INTERFACE_GEN_* found by the last probe.
        self.interface_generation = INTERFACE_GEN_UNKNOWN

    def connect(self) -> bool:
        """Connect to sensor Config API and probe its interface generation.

        Returns True on success, an interface v1.0.x sensor included (its
        connection is closed right after the probe). The radar accepts one
        Config API client; a second connection is reset right after it is
        accepted, so the probe on it fails.
        """
        self._drop()
        self.interface_generation = INTERFACE_GEN_UNKNOWN
        try:
            sock = socket.create_connection((self.host, self.port),
                                            timeout=self.timeout)
        except OSError as e:
            _log.warning("Config API connect to %s:%d failed: %s",
                         self.host, self.port, e)
            return False
        sock.settimeout(self.timeout)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._sock = sock
        self._session_id = 0
        self._reconnect = True
        return self._probe()

    def _probe(self) -> bool:
        """Decide the interface generation with a read-only GetDeviceInfo.

        A v1.0.x sensor (firmware 3.0.0 to 3.1.0) reads the JSON body from
        frame offset 16, so the E2E header of a v1.1.0 request lands in the
        JSON and the parse fails; the sensor still runs the handler with no
        arguments. Restart, ResetAllSettings, FirmwareSwitchSlot and
        FirmwareUpdateReboot need none, so a v1.1.0 request would act on the
        sensor. Its GetDeviceInfo answer starts with the JSON '{' where v1.1.0
        puts the E2E header.
        """
        out = self._roundtrip(METHOD_GET_DEVICE_INFO, b"{}", probe=True)
        if isinstance(out, int):
            _log.warning("Config API %s:%d: interface probe failed (%s)",
                         self.host, self.port, TRANSPORT_ERROR_NAMES[out])
            return False
        if out[1][:1] == b"{":
            self.interface_generation = INTERFACE_GEN_V1_0
            _log.warning("Config API %s:%d: the sensor speaks interface v1.0.x "
                         "(firmware 3.0.0 to 3.1.0); Config API requests return "
                         "RC_NOT_SUPPORTED without sending", self.host, self.port)
            self._drop()
            return True
        self.interface_generation = INTERFACE_GEN_V1_1
        return True

    def close(self):
        """Close the connection. Later requests fail until connect()."""
        self._reconnect = False
        self._drop()

    def _drop(self):
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def request(self, method_id: int, payload: Optional[dict] = None
                ) -> Tuple[int, Optional[dict]]:
        """Send a JSON request and receive the response.

        Returns ``(return_code, data)``: 0 is SUCCESS, a positive value is the
        radar's return code (``RC_*``), a negative value is ``ERR_*``. ``data``
        is the response ``data`` object when present, else the whole body.
        """
        body = json.dumps(payload or {}).encode("utf-8")
        return self._exchange(method_id, body)

    def request_raw(self, method_id: int, body: bytes
                    ) -> Tuple[int, Optional[dict]]:
        """Send a binary-body request (FirmwareDataChunk); the reply is JSON."""
        return self._exchange(method_id, bytes(body))

    def _exchange(self, method_id: int, body: bytes
                  ) -> Tuple[int, Optional[dict]]:
        self.last_response_e2e = None
        self.last_result = None
        if self.interface_generation == INTERFACE_GEN_V1_0:
            return self._refuse_legacy()
        if len(body) > MAX_REQUEST_BODY:
            return self._fail(ERR_TOO_LARGE)
        if self._sock is None:
            if not (self._reconnect and self.connect()):
                return self._fail(ERR_TRANSPORT)
            if self.interface_generation == INTERFACE_GEN_V1_0:
                return self._refuse_legacy()

        out = self._roundtrip(method_id, body)
        if isinstance(out, int):
            return self._fail(out)
        h, raw = out
        data_id = (SERVICE_ID << 16) | method_id
        raw = self._strip_response_e2e(method_id, data_id, raw)
        try:
            resp = json.loads(raw.decode("utf-8")) if raw else {}
        except (ValueError, UnicodeDecodeError):
            _log.warning("Config API 0x%04X: response body is not JSON", method_id)
            return self._fail(ERR_PROTOCOL)

        rc = h[8]
        if isinstance(resp, dict) and isinstance(resp.get("return_code"), int):
            rc = resp["return_code"]
        data = resp.get("data") if isinstance(resp, dict) else None
        if data is None:
            data = resp
        name = resp.get("return_name") if isinstance(resp, dict) else None
        self.last_result = {
            "return_code": rc,
            "return_name": name or RETURN_CODE_NAMES.get(rc, "UNKNOWN"),
            "error_message": (data.get("error_message")
                              if isinstance(data, dict) else None),
            "data": data,
            "e2e_ok": bool(self.last_response_e2e and self.last_response_e2e["ok"]),
        }
        return (rc, data)

    def _roundtrip(self, method_id: int, body: bytes, probe: bool = False):
        """Send one framed request and read its response.

        Returns ``(header, body)``, or an ``ERR_*`` code after dropping the
        connection. With ``probe`` a response without the E2E header (an
        interface v1.0.x body) is accepted.
        """
        self._session_id = (self._session_id % 0xFFFF) + 1
        self._e2e_counter = (self._e2e_counter + 1) & 0xFFFFFFFF
        data_id = (SERVICE_ID << 16) | method_id
        header = struct.pack(
            SOMEIP_HEADER_FMT,
            SERVICE_ID, method_id, 8 + E2E_HEADER_SIZE + len(body),
            CLIENT_ID, self._session_id,
            PROTO_VER, IFACE_VER, MSG_TYPE_REQUEST, 0x00
        )
        e2e = struct.pack(E2E_HEADER_FMT, 0, zlib.crc32(body) & 0xFFFFFFFF,
                          len(body), self._e2e_counter, data_id)
        min_length = 8 + (1 if probe else E2E_HEADER_SIZE)

        try:
            self._sock.sendall(header + e2e + body)
            resp_hdr = self._recv_exact(SOMEIP_HEADER_SIZE)
            h = struct.unpack(SOMEIP_HEADER_FMT, resp_hdr)
            if not (h[0] == SERVICE_ID and h[1] == method_id
                    and h[3] == CLIENT_ID and h[4] == self._session_id
                    and h[5] == PROTO_VER and h[6] == IFACE_VER
                    and h[7] in (MSG_TYPE_RESPONSE, MSG_TYPE_ERROR)
                    and min_length <= h[2] <= 8 + MAX_RESPONSE_PAYLOAD):
                _log.warning("Config API 0x%04X: response header %s does not "
                             "answer session %d", method_id, h, self._session_id)
                self._drop()
                return ERR_PROTOCOL
            return h, self._recv_exact(h[2] - 8)
        except socket.timeout:
            self._drop()
            return ERR_TIMEOUT
        except OSError:
            self._drop()
            return ERR_TRANSPORT

    def _refuse_legacy(self) -> Tuple[int, None]:
        self.last_result = {
            "return_code": RC_NOT_SUPPORTED, "return_name": "NOT_SUPPORTED",
            "error_message": "interface v1.0.x sensor: Config API request not sent",
            "data": None, "e2e_ok": False}
        return (RC_NOT_SUPPORTED, None)

    def _fail(self, code: int) -> Tuple[int, None]:
        self.last_result = {"return_code": code,
                            "return_name": TRANSPORT_ERROR_NAMES[code],
                            "error_message": None, "data": None, "e2e_ok": False}
        return (code, None)

    def _strip_response_e2e(self, method_id: int, data_id: int,
                            raw: bytes) -> bytes:
        """Verify and remove the response E2E header (warning only)."""
        crc_high, crc, length, counter, did = struct.unpack(
            E2E_HEADER_FMT, raw[:E2E_HEADER_SIZE])
        body = raw[E2E_HEADER_SIZE:]
        ok = (crc_high == 0 and crc != 0 and length == len(body)
              and did == data_id and (zlib.crc32(body) & 0xFFFFFFFF) == crc)
        self.last_response_e2e = {"crc": crc, "length": length,
                                  "counter": counter, "data_id": did, "ok": ok}
        if not ok:
            _log.warning("Config API 0x%04X: response E2E check failed "
                         "(crc=0x%08X length=%d counter=%d data_id=0x%08X)",
                         method_id, crc, length, counter, did)
        return body

    def _recv_exact(self, size: int) -> bytes:
        buf = b""
        while len(buf) < size:
            chunk = self._sock.recv(size - len(buf))
            if not chunk:
                raise ConnectionError("peer closed the connection")
            buf += chunk
        return buf

    @property
    def connected(self) -> bool:
        return self._sock is not None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.close()


def discovery_generation(info: dict) -> int:
    """Interface generation from Discovery keys: driver_hash exists only on
    v1.0.x, the m72_* keys only on v1.1.0 (INTERFACE_GEN_*)."""
    if "driver_hash" in info:
        return INTERFACE_GEN_V1_0
    if any(k.startswith("m72_") for k in info):
        return INTERFACE_GEN_V1_1
    return INTERFACE_GEN_UNKNOWN


def discover(timeout_s: float = 3.0,
             broadcast_ip: str = "255.255.255.255",
             port: int = DEFAULT_DISCOVERY_PORT,
             bind_ip: str = "") -> List[dict]:
    """Discover AFI920 sensors on the network via UDP broadcast.

    Sensors reply with both a unicast and a limited-broadcast response
    (wrong-subnet recovery); the pair is deduplicated by (source IP,
    serial_number). A response can occasionally be missed; scan again before
    treating a sensor as absent.

    Args:
        timeout_s: Discovery timeout in seconds
        broadcast_ip: Broadcast address
        port: Discovery UDP port
        bind_ip: Local IP to bind for broadcast (empty = OS default)

    Returns:
        List of sensor info dicts
    """
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(timeout_s)
        if bind_ip:
            sock.bind((bind_ip, 0))

        body = json.dumps({}).encode("utf-8")
        length = 8 + len(body)
        header = struct.pack(
            SOMEIP_HEADER_FMT,
            SERVICE_ID, METHOD_DISCOVERY_REQUEST, length,
            0x0001, 0x0001,
            PROTO_VER, IFACE_VER, MSG_TYPE_REQUEST, 0x00
        )

        sock.sendto(header + body, (broadcast_ip, port))

        sensors = []
        seen = set()
        # One collection window (Discovery §Publisher Usage: 3 s), not a
        # per-datagram idle timeout that a chatty network could extend.
        deadline = time.monotonic() + timeout_s
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(remaining)
            try:
                data, addr = sock.recvfrom(4096)
                if len(data) < SOMEIP_HEADER_SIZE:
                    continue

                h = struct.unpack(SOMEIP_HEADER_FMT, data[:SOMEIP_HEADER_SIZE])
                if h[1] != METHOD_DISCOVERY_RESPONSE:
                    continue

                payload = data[SOMEIP_HEADER_SIZE:]
                if payload:
                    try:
                        info = json.loads(payload.decode("utf-8"))
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        continue
                    if "data" in info:
                        info = info["data"]

                    # Dedup the unicast + limited-broadcast pair by
                    # (source_ip, serial_number) (Discovery §DiscoverResponse)
                    key = (addr[0], info.get("serial_number", ""))
                    if key in seen:
                        continue
                    seen.add(key)

                    info["_source_ip"] = addr[0]
                    info["_source_port"] = addr[1]
                    info["_interface_generation"] = discovery_generation(info)
                    sensors.append(info)

            except socket.timeout:
                break
            except ConnectionResetError:
                # Windows reports an ICMP port-unreachable for the request
                # (e.g. a unicast to a host without a radar) on the next
                # receive; other replies may still arrive.
                continue

    return sensors
