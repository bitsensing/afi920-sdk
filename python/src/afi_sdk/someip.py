"""
AFI920 SOME/IP Client Library

Lightweight Python helper for AFI920 sensor communication.
Provides Config API (TCP) and Discovery (UDP) over SOME/IP protocol.

Protocol v3.0 method ID registry.

Dependencies: Python 3.8+ (stdlib only, no external packages required)

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import json
import socket
import struct
from typing import List, Optional, Tuple

# ── SOME/IP Constants ──

SERVICE_ID = 0x6000
PROTO_VER = 0x01
IFACE_VER = 0x01
MSG_TYPE_REQUEST = 0x00
MSG_TYPE_NOTIFICATION = 0x02
MSG_TYPE_RESPONSE = 0x80
MSG_TYPE_TP = 0x20

SOMEIP_HEADER_FMT = ">HHIHHBBBB"
SOMEIP_HEADER_SIZE = 16

# ── Config API Method IDs (protocol v3.0 registry) ──

# Device Info & Status (0x001x)
METHOD_GET_DEVICE_INFO          = 0x0010
METHOD_GET_SENSOR_STATUS        = 0x0011
METHOD_GET_DIAGNOSTIC_INFO      = 0x0012
METHOD_CLEAR_FAULT_LOG          = 0x0013
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
METHOD_GET_DETECTION_DENSITY    = 0x0044
METHOD_SET_DETECTION_DENSITY    = 0x0045

# Time Sync (0x005x)
METHOD_GET_PTP_CONFIG           = 0x0050
METHOD_SET_PTP_CONFIG           = 0x0051

# Mounting (0x007x)
METHOD_GET_MOUNTING_POSITION    = 0x0070
METHOD_SET_MOUNTING_POSITION    = 0x0071
METHOD_GET_ROTATION             = 0x0072
METHOD_SET_ROTATION             = 0x0073
METHOD_GET_SENSOR_POSITION      = 0x0074
METHOD_SET_SENSOR_POSITION      = 0x0075

# Config Persistence (0x009x)
METHOD_SAVE_CONFIG              = 0x0090
METHOD_GET_CONFIG_STATUS        = 0x0091
METHOD_RESET_CONFIG_DEFAULTS    = 0x0092

# Discovery Method IDs (UDP 30520, separate channel)
METHOD_DISCOVERY_REQUEST        = 0x00C0
METHOD_DISCOVERY_RESPONSE       = 0x00C1

# ── Data Stream Event IDs ──

EVENT_CSII     = 0x8001   # Host -> Radar (TCP only)
EVENT_RDI      = 0x8002
EVENT_SHII     = 0x8003
EVENT_SPI      = 0x8004

# ── Default Ports ──

DEFAULT_CONFIG_PORT    = 30500
DEFAULT_CSII_PORT      = 30501
DEFAULT_RDI_PORT       = 30509
DEFAULT_SHII_PORT      = 30510
DEFAULT_SPI_PORT       = 30511
DEFAULT_DISCOVERY_PORT = 30520

# Safety limit for response payload size (matches C++ kMaxResponsePayload)
MAX_RESPONSE_PAYLOAD   = 4 * 1024 * 1024  # 4 MB

# ── Range Mode (registry: 0=DR, 1=LR, 2=MR, 3=SR, 4=ULR, 5=USR) ──

RANGE_MODE_DR  = 0     # Dual Range
RANGE_MODE_LR  = 1     # Long Range
RANGE_MODE_MR  = 2     # Middle Range
RANGE_MODE_SR  = 3     # Short Range
RANGE_MODE_ULR = 4     # Ultra Long Range
RANGE_MODE_USR = 5     # Ultra Short Range

RANGE_MODE_NAMES = {0: "DR", 1: "LR", 2: "MR", 3: "SR", 4: "ULR", 5: "USR"}


class ConfigClient:
    """SOME/IP Config API client over TCP.

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

    def connect(self) -> bool:
        """Connect to sensor Config API. Returns True on success."""
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(self.timeout)
            self._sock.connect((self.host, self.port))
            self._session_id = 0
            return True
        except OSError as e:
            print(f"Connection failed: {self.host}:{self.port} - {e}")
            self._sock = None
            return False

    def close(self):
        """Close TCP connection."""
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def request(self, method_id: int, payload: Optional[dict] = None
                ) -> Tuple[int, Optional[dict]]:
        """Send SOME/IP request and receive response.

        Args:
            method_id: Config API method ID (e.g., METHOD_GET_DEVICE_INFO)
            payload: JSON payload dict (optional, empty object if None)

        Returns:
            Tuple of (return_code, response_data_dict_or_None).
            return_code: 0=OK, >0=SOME/IP error, <0=connection error
        """
        if not self._sock:
            return (-1, None)

        self._session_id = (self._session_id + 1) & 0xFFFF
        body = json.dumps(payload or {}).encode("utf-8")
        length = 8 + len(body)

        header = struct.pack(
            SOMEIP_HEADER_FMT,
            SERVICE_ID, method_id, length,
            0x0001, self._session_id,
            PROTO_VER, IFACE_VER, MSG_TYPE_REQUEST, 0x00
        )

        try:
            self._sock.sendall(header + body)
        except OSError:
            return (-1, None)

        try:
            resp_hdr = self._recv_exact(SOMEIP_HEADER_SIZE)
            if not resp_hdr:
                return (-1, None)
        except socket.timeout:
            return (-2, None)

        h = struct.unpack(SOMEIP_HEADER_FMT, resp_hdr)
        ret_code = h[8]
        payload_size = h[2] - 8 if h[2] > 8 else 0

        if payload_size > MAX_RESPONSE_PAYLOAD:
            return (-3, None)

        resp_data = None
        if payload_size > 0:
            try:
                raw = self._recv_exact(payload_size)
                if raw:
                    resp_data = json.loads(raw.decode("utf-8"))
            except (socket.timeout, json.JSONDecodeError, UnicodeDecodeError):
                pass

        rc = ret_code
        if resp_data and "return_code" in resp_data:
            rc = resp_data["return_code"]

        # Extract "data" field if present
        data = resp_data.get("data") if resp_data else None

        return (rc, data if data is not None else resp_data)

    def _recv_exact(self, size: int) -> Optional[bytes]:
        buf = b""
        while len(buf) < size:
            chunk = self._sock.recv(size - len(buf))
            if not chunk:
                return None
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


def discover(timeout_s: float = 3.0,
             broadcast_ip: str = "255.255.255.255",
             port: int = DEFAULT_DISCOVERY_PORT,
             bind_ip: str = "") -> List[dict]:
    """Discover AFI920 sensors on the network via UDP broadcast.

    Sensors reply with both a unicast and a limited-broadcast response
    (wrong-subnet recovery), so duplicates are deduplicated by
    (serial_number, ip_address).

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
        while True:
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

                    # Dedup unicast + broadcast duplicate responses
                    key = (info.get("serial_number", ""),
                           info.get("ip_address", addr[0]))
                    if key in seen:
                        continue
                    seen.add(key)

                    info["_source_ip"] = addr[0]
                    info["_source_port"] = addr[1]
                    sensors.append(info)

            except socket.timeout:
                break

    return sensors
