"""
AFI920 Python SDK — AfiSdk class

High-level Python API for AFI920 4D Imaging Radar.
Mirrors the C++ AfiSdk API for consistent developer experience.

Interface v1.1.0 method ID registry.

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import json
import math
import socket
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

from afi_sdk.someip import (
    ConfigClient,
    INTERFACE_GEN_UNKNOWN,
    INTERFACE_GEN_V1_0,
    ERR_PROTOCOL,
    ERR_TIMEOUT,
    ERR_TOO_LARGE,
    ERR_TRANSPORT,
    RC_ACCESS_DENIED,
    RC_BUSY,
    RC_INVALID_PARAM,
    RC_INVALID_STATE,
    RC_NOT_SUPPORTED,
    RC_SESSION_REQUIRED,
    discover as _discover,
    SOMEIP_HEADER_FMT,
    SOMEIP_HEADER_SIZE,
    MAX_RESPONSE_PAYLOAD,
    SERVICE_ID,
    PROTO_VER,
    IFACE_VER,
    MSG_TYPE_REQUEST,
    DEFAULT_CSII_PORT,
    RANGE_MODE_NAMES,
    METHOD_GET_DEVICE_INFO,
    METHOD_GET_SENSOR_STATUS,
    METHOD_GET_DIAGNOSTIC_INFO,
    METHOD_RESTART,
    METHOD_RESET_ALL_SETTINGS,
    METHOD_GET_COMMAND_HISTORY,
    METHOD_GET_NETWORK_CONFIG,
    METHOD_SET_NETWORK_CONFIG,
    METHOD_GET_RANGE_MODE,
    METHOD_SET_RANGE_MODE,
    METHOD_GET_DETECTION_THRESHOLD,
    METHOD_SET_DETECTION_THRESHOLD,
    METHOD_GET_DETECTION_FILTERS,
    METHOD_SET_DETECTION_FILTERS,
    METHOD_GET_PTP_CONFIG,
    METHOD_SET_PTP_CONFIG,
    METHOD_GET_MOUNTING_POSITION,
    METHOD_SET_MOUNTING_POSITION,
    METHOD_GET_ROTATION,
    METHOD_SET_ROTATION,
    METHOD_GET_SENSOR_POSITION,
    METHOD_SET_SENSOR_POSITION,
    METHOD_FW_UPDATE_START,
    METHOD_FW_DATA_CHUNK,
    METHOD_FW_UPDATE_COMMIT,
    METHOD_FW_UPDATE_STATUS,
    METHOD_FW_UPDATE_ABORT,
    METHOD_FW_SWITCH_SLOT,
    METHOD_FW_UPDATE_REBOOT,
    METHOD_GET_BOOT_INFO,
    METHOD_GET_CONFIG_STATUS,
    METHOD_RESET_CONFIG_DEFAULTS,
    METHOD_GET_MODE,
    METHOD_SET_MODE,
)
from afi_sdk import streams


# ── Configuration ──

@dataclass
class AfiSdkConfig:
    """SDK configuration (mirrors C++ AfiSdkConfig)."""
    sensor_ip: str = "192.168.10.150"
    config_port: int = 30500
    csii_port: int = DEFAULT_CSII_PORT
    rdi_port: int = 30509
    shi_port: int = 30510
    spi_port: int = 30511
    timeout: float = 5.0
    stream_transport: str = "tcp"
    validate_e2e: bool = True
    #: CSII transport: "" picks it from the interface generation (UDP on
    #: v1.1.0, TCP on v1.0.x, UDP while unknown); "udp" or "tcp" forces one.
    csii_transport: str = ""


# ── Enums ──

class RangeMode:
    """Radar range mode (mirrors C++ AfiRangeMode)."""
    DUAL_RANGE = 0        # DR
    LONG_RANGE = 1        # LR
    MID_RANGE = 2         # MR
    SHORT_RANGE = 3       # SR
    ULTRA_LONG_RANGE = 4  # ULR
    ULTRA_SHORT_RANGE = 5 # USR
    ULTRA_DUAL_RANGE = 6  # UDR (two subcycles, like DR)

    @staticmethod
    def name(mode: int) -> str:
        return RANGE_MODE_NAMES.get(mode, "Unknown")


class AccessMode:
    """Config API access mode (interface v1.1.0, 11_Access_Mode).

    Entry is one way, NORMAL -> DIAGNOSTIC -> PROGRAMMING; only a reboot
    returns to NORMAL. DIAGNOSTIC opens every config SET; PROGRAMMING opens
    the firmware update chain and halts RDI/SHII/SPI.
    """
    NORMAL = 0
    DIAGNOSTIC = 1
    PROGRAMMING = 2


# ── Error Codes (same values as C++ afi::AfiError) ──

OK = 0
NOT_INITIALIZED = -1
CONNECT_FAILED = -3
TIMEOUT = -4
DISCONNECTED = -5
INVALID_PARAM = -6
PROTOCOL_ERROR = -7
SENSOR_ERROR = -8
INTERNAL_ERROR = -9
NOT_SUPPORTED = -10
BUSY = -11
SESSION_REQUIRED = -12
ACCESS_DENIED = -13
INVALID_STATE = -14

_ERROR_BY_RESULT = {
    ERR_TRANSPORT: DISCONNECTED,
    ERR_TIMEOUT: TIMEOUT,
    ERR_PROTOCOL: PROTOCOL_ERROR,
    ERR_TOO_LARGE: INVALID_PARAM,
    RC_INVALID_PARAM: INVALID_PARAM,
    RC_NOT_SUPPORTED: NOT_SUPPORTED,
    RC_BUSY: BUSY,
    RC_SESSION_REQUIRED: SESSION_REQUIRED,
    RC_ACCESS_DENIED: ACCESS_DENIED,
    RC_INVALID_STATE: INVALID_STATE,
}


def error_from_result(rc: int) -> int:
    """Map a ConfigClient result (radar RC or ERR_*) to an SDK error code."""
    if rc == 0:
        return OK
    return _ERROR_BY_RESULT.get(rc, SENSOR_ERROR)

CFAR_TABLE_LENGTH = 19
CFAR_MIN_LEVEL = 1
CFAR_MAX_LEVEL = 9


def _normalize_cfar_table(table) -> Optional[List[int]]:
    if not isinstance(table, (list, tuple)) or len(table) != CFAR_TABLE_LENGTH:
        return None

    levels = []
    for value in table:
        if isinstance(value, bool) or not isinstance(value, int):
            return None
        if value < CFAR_MIN_LEVEL or value > CFAR_MAX_LEVEL:
            return None
        levels.append(value)
    return levels


# ── SDK Class ──

class AfiSdk:
    """High-level AFI920 sensor interface.

    Provides Config API (TCP) methods that mirror the C++ SDK.
    One instance per sensor.
    """

    def __init__(self):
        self._client: Optional[ConfigClient] = None
        self._config: Optional[AfiSdkConfig] = None
        self._csii_sock: Optional[socket.socket] = None
        self._csii_tcp = False
        self._csii_counter = 0
        self._csii_session = 0
        self._last_error = OK

    def init(self, config: AfiSdkConfig) -> int:
        """Initialize and connect to sensor.

        Args:
            config: SDK configuration with sensor IP and ports

        Returns:
            OK (0) on success, negative error code on failure
        """
        if config.csii_transport not in ("", "udp", "tcp"):
            return INVALID_PARAM
        self._config = config
        self._client = ConfigClient(config.sensor_ip, config.config_port,
                                    config.timeout)
        if not self._client.connect():
            return CONNECT_FAILED
        return OK

    @property
    def interface_generation(self) -> int:
        """INTERFACE_GEN_* from the GetDeviceInfo probe of the Config API
        connection. On INTERFACE_GEN_V1_0 every Config API call returns
        NOT_SUPPORTED without sending; CSII still reaches the sensor."""
        if not self._client:
            return INTERFACE_GEN_UNKNOWN
        return self._client.interface_generation

    def reconnect(self, timeout_s: float = 60.0, interval_s: float = 1.0) -> int:
        """Reconnect the Config API, retrying until ``timeout_s``.

        Use after ``restart()`` or a firmware reboot: the radar answers
        before it resets, so the old connection dies after the reply.
        """
        if not self._client:
            return NOT_INITIALIZED
        deadline = time.monotonic() + timeout_s
        while True:
            if self._client.connect():
                return OK
            if time.monotonic() >= deadline:
                return CONNECT_FAILED
            time.sleep(interval_s)

    @property
    def last_error(self) -> int:
        """SDK error code of the last Config API call (``OK`` on success)."""
        return self._last_error

    @property
    def last_result(self) -> Optional[dict]:
        """Radar outcome of the last call: return_code, return_name,
        error_message (firmware update methods), data and e2e_ok (the
        response E2E verdict; a failed check still delivers the answer)."""
        return self._client.last_result if self._client else None

    def close(self):
        """Disconnect from sensor."""
        if self._client:
            self._client.close()
            self._client = None
        self._close_csii()

    @property
    def connected(self) -> bool:
        return self._client is not None and self._client.connected

    # ── Discovery (static) ──

    @staticmethod
    def discover(timeout_s: float = 3.0, bind_ip: str = "") -> List[dict]:
        """Discover AFI920 sensors on the network.

        Args:
            timeout_s: Discovery timeout in seconds
            bind_ip: Local IP to bind for broadcast (empty = OS default)

        Returns:
            List of sensor info dicts (serial_number, software_version,
            range_mode, ports, ...). Unicast/broadcast duplicates are deduped.
        """
        return _discover(timeout_s=timeout_s, bind_ip=bind_ip)

    # ── Vehicle Input (CSII, Host → Radar) ──

    def send_vehicle_input(self,
                           somc: int = streams.SOMC_NORMAL_MODE,
                           velocity_mps: Tuple[float, float, float] = (0.0, 0.0, 0.0),
                           rotation_rate_rps: Tuple[float, float, float] = (0.0, 0.0, 0.0),
                           wheel_speeds_rps: Tuple[float, float, float, float] = (0.0, 0.0, 0.0, 0.0),
                           steering_angle_rad: float = 0.0,
                           gear_position: int = streams.GEAR_NEUTRAL,
                           global_timestamp_utc: float = 0.0,
                           coord_system: int = 0,
                           sensor_id: int = 1,
                           timestamp_ns: int = 0) -> int:
        """Send one CSII vehicle-input message (port 30501, Event 0x8001).

        Call cyclically (100 ms recommended). One call is one UDP datagram on
        interface v1.1.0 and one message on a TCP connection on v1.0.x
        (``AfiSdkConfig.csii_transport``); the socket is created on first use
        and the SDK maintains the message/E2E counters.
        """
        if not self._config:
            return NOT_INITIALIZED

        use_tcp = (self._config.csii_transport == "tcp"
                   or (self._config.csii_transport == ""
                       and self.interface_generation == INTERFACE_GEN_V1_0))
        if self._csii_sock is not None and self._csii_tcp != use_tcp:
            self._close_csii()
        if self._csii_sock is None:
            try:
                if use_tcp:
                    self._csii_sock = socket.create_connection(
                        (self._config.sensor_ip, self._config.csii_port),
                        timeout=self._config.timeout)
                else:
                    self._csii_sock = socket.socket(socket.AF_INET,
                                                    socket.SOCK_DGRAM)
                    self._csii_sock.connect((self._config.sensor_ip,
                                             self._config.csii_port))
            except OSError:
                self._csii_sock = None
                return DISCONNECTED
            self._csii_tcp = use_tcp

        payload = streams.build_csii_payload(
            somc=somc, sensor_id=sensor_id, timestamp_ns=timestamp_ns,
            message_counter=self._csii_counter,
            global_timestamp_utc=global_timestamp_utc,
            coord_system=coord_system, velocity_mps=velocity_mps,
            rotation_rate_rps=rotation_rate_rps,
            wheel_speeds_rps=wheel_speeds_rps,
            steering_angle_rad=steering_angle_rad,
            gear_position=gear_position)

        self._csii_session = (self._csii_session + 1) & 0xFFFF or 1
        frame = streams.build_csii_frame(payload, self._csii_counter,
                                         self._csii_session)
        self._csii_counter = (self._csii_counter + 1) & 0xFFFFFFFF

        try:
            self._csii_sock.sendall(frame)
        except OSError:
            # A connected UDP socket reports an earlier ICMP unreachable on the
            # next call and stays usable; a TCP connection is opened again.
            if self._csii_tcp:
                self._close_csii()
            return DISCONNECTED
        return OK

    def _close_csii(self):
        if self._csii_sock:
            try:
                self._csii_sock.close()
            except OSError:
                pass
        self._csii_sock = None

    # ── Access Mode (0x00Ex) ──

    def get_mode(self) -> Optional[dict]:
        """Current access mode: access_mode, access_mode_name, elapsed_ms,
        streams_halted."""
        return self._get(METHOD_GET_MODE)

    def set_mode(self, mode: int) -> int:
        """Enter the next access mode (SetMode 0x00E1).

        DIAGNOSTIC is accepted from NORMAL, PROGRAMMING only from DIAGNOSTIC.
        Re-entry or a reverse step answers ACCESS_DENIED; PROGRAMMING from
        NORMAL answers SESSION_REQUIRED.
        """
        if mode not in (AccessMode.DIAGNOSTIC, AccessMode.PROGRAMMING):
            self._last_error = INVALID_PARAM
            return INVALID_PARAM
        return self._set(METHOD_SET_MODE, {"access_mode": mode})

    def enter_mode(self, target: int) -> int:
        """Reach ``target`` from the current mode with GetMode and SetMode steps.

        OK when already there; ACCESS_DENIED when the radar is in PROGRAMMING
        and ``target`` is DIAGNOSTIC (reboot first). Config SETs need
        DIAGNOSTIC; the firmware update chain needs PROGRAMMING.
        """
        if target not in (AccessMode.DIAGNOSTIC, AccessMode.PROGRAMMING):
            self._last_error = INVALID_PARAM
            return INVALID_PARAM
        cur = self.get_mode()
        if cur is None:
            return self._last_error
        mode = cur.get("access_mode")
        if mode == target:
            return OK
        if mode == AccessMode.PROGRAMMING:
            self._last_error = ACCESS_DENIED
            return ACCESS_DENIED
        if mode == AccessMode.NORMAL:
            err = self.set_mode(AccessMode.DIAGNOSTIC)
            if err != OK or target == AccessMode.DIAGNOSTIC:
                return err
        return self.set_mode(AccessMode.PROGRAMMING)

    # ── Device Info & Status (0x001x) ──

    def get_device_info(self) -> Optional[dict]:
        """Get device info (serial, model, hw/software version, component hashes, MAC)."""
        return self._get(METHOD_GET_DEVICE_INFO)

    def get_sensor_status(self) -> Optional[dict]:
        """Get sensor status (operation state, temperatures, voltage, uptime)."""
        return self._get(METHOD_GET_SENSOR_STATUS)

    def get_diagnostic_info(self) -> Optional[dict]:
        """Get diagnostic info (combined state, fault levels, faults[])."""
        return self._get(METHOD_GET_DIAGNOSTIC_INFO)

    def restart(self) -> int:
        """Restart sensor. Connection will be lost."""
        return self._action(METHOD_RESTART)

    def reset_all_settings(self) -> int:
        """Factory reset. Sensor will restart."""
        return self._action(METHOD_RESET_ALL_SETTINGS)

    def get_command_history_info(self) -> Optional[dict]:
        """Get command history counters (total/success/fail)."""
        return self._get(METHOD_GET_COMMAND_HISTORY)

    # ── Network (0x002x) ──

    def get_network_config(self) -> Optional[dict]:
        """Get unified network config (radar network + stream destinations)."""
        return self._get(METHOD_GET_NETWORK_CONFIG)

    def set_network_config(self, **fields) -> Optional[dict]:
        """Set network config (partial update — only given fields change).

        Keyword fields: ip_address, subnet_mask, gateway, api_port, vlan_id,
        {detection,health,performance}_{ip,port,protocol}. The SET requires
        the diagnostic access mode, is written to NVM, and applies after a
        restart.

        Returns the response data (contains restart_required) or None on error.
        """
        return self._get_with_payload(METHOD_SET_NETWORK_CONFIG, fields)

    def set_stream_destination(self, stream: str, ip: str, port: int,
                               protocol: str = "tcp") -> int:
        """Set one stream destination (NVM write, applies after restart).

        Args:
            stream: "detection", "health", or "performance"
        """
        if stream not in ("detection", "health", "performance"):
            return self._invalid()
        return self._set(METHOD_SET_NETWORK_CONFIG, {
            f"{stream}_ip": ip,
            f"{stream}_port": port,
            f"{stream}_protocol": protocol,
        })

    # ── Range Mode (0x003x) ──

    def get_range_mode(self) -> Optional[int]:
        """Get current range mode (see RangeMode)."""
        data = self._get(METHOD_GET_RANGE_MODE)
        return data.get("range_mode") if data else None

    def set_range_mode(self, mode: int) -> int:
        """Set the range mode (RangeMode.*, 0..6). Diagnostic mode; NVM;
        applies after restart (the answer carries restart_required)."""
        if isinstance(mode, bool) or not isinstance(mode, int) or not 0 <= mode <= 6:
            return self._invalid()
        return self._set(METHOD_SET_RANGE_MODE, {"range_mode": mode})

    # ── Detection (0x004x) ──

    def get_detection_threshold(self) -> Optional[dict]:
        """Get detection thresholds (snr_threshold_db, cfar_table_*)."""
        data = self._get(METHOD_GET_DETECTION_THRESHOLD)
        if data is None:
            return None
        snr = data.get("snr_threshold_db")
        if (isinstance(snr, bool) or not isinstance(snr, (int, float)) or
                not math.isfinite(snr) or snr < 1 or snr > 9):
            self._last_error = PROTOCOL_ERROR
            return None
        first = _normalize_cfar_table(data.get("cfar_table_first_frame"))
        if first is None:
            self._last_error = PROTOCOL_ERROR
            return None
        data["cfar_table_first_frame"] = first
        if "cfar_table_second_frame" in data:
            second = _normalize_cfar_table(data["cfar_table_second_frame"])
            if second is None:
                self._last_error = PROTOCOL_ERROR
                return None
            data["cfar_table_second_frame"] = second
        return data

    def set_detection_threshold(
        self,
        snr_db: Optional[float] = None,
        cfar_table_first_frame: Optional[List[int]] = None,
        cfar_table_second_frame: Optional[List[int]] = None,
    ) -> int:
        """Set the SNR threshold level (1..9, fractions kept) and/or the
        19-level CFAR tables. The second-frame table applies in DR and UDR."""
        payload = {}
        if snr_db is not None:
            if (isinstance(snr_db, bool) or not isinstance(snr_db, (int, float)) or
                    not math.isfinite(snr_db) or snr_db < 1 or snr_db > 9):
                return self._invalid()
            payload["snr_threshold_db"] = snr_db

        if cfar_table_first_frame is not None:
            levels = _normalize_cfar_table(cfar_table_first_frame)
            if levels is None:
                return self._invalid()
            payload["cfar_table_first_frame"] = levels

        if cfar_table_second_frame is not None:
            levels = _normalize_cfar_table(cfar_table_second_frame)
            if levels is None:
                return self._invalid()
            payload["cfar_table_second_frame"] = levels

        return self._set(METHOD_SET_DETECTION_THRESHOLD, payload)

    def get_detection_filters(self) -> Optional[dict]:
        """Get the eight v1.1.0 detection filter toggles (filter_ego_specular, ...)."""
        return self._get(METHOD_GET_DETECTION_FILTERS)

    def set_detection_filters(self, **filters) -> int:
        """Set detection filter toggles (any subset).

        Keyword fields: filter_ego_specular, filter_ground_sidelobe,
        filter_upper_structure, filter_multibounce_2x,
        filter_structure_multipath, filter_specular_mirror,
        filter_wheel_microdoppler, filter_ground_reflection. The SET requires
        the diagnostic access mode, is written to NVM, and applies after a
        restart.
        """
        return self._set(METHOD_SET_DETECTION_FILTERS, filters)

    # ── Time Sync (0x005x) ──

    def get_ptp_config(self) -> Optional[dict]:
        """Get active/configured PTP profiles, restart flag, and live status."""
        return self._get(METHOD_GET_PTP_CONFIG)

    def set_ptp_config(self, profile: int = 0) -> int:
        """Set pending PTP profile (0=gPTP, 1=1588v2-L2, 2=1588v2-UDP)."""
        if isinstance(profile, bool) or not isinstance(profile, int) or not 0 <= profile <= 2:
            return self._invalid()
        return self._set(METHOD_SET_PTP_CONFIG, {"profile": profile})

    def set_ptp_config_with_result(self, profile: int = 0) -> Optional[dict]:
        """Set pending PTP profile and return profile/restart_required."""
        if isinstance(profile, bool) or not isinstance(profile, int) or not 0 <= profile <= 2:
            self._invalid()
            return None
        return self._get_with_payload(METHOD_SET_PTP_CONFIG, {"profile": profile})

    # ── Mounting (0x007x) ──

    def get_mounting_position(self) -> Optional[dict]:
        """Get mounting position (offset_x_m, offset_y_m, offset_z_m)."""
        return self._get(METHOD_GET_MOUNTING_POSITION)

    def set_mounting_position(self, x: float, y: float, z: float) -> int:
        return self._set(METHOD_SET_MOUNTING_POSITION, {
            "offset_x_m": x, "offset_y_m": y, "offset_z_m": z,
        })

    def get_rotation(self) -> Optional[dict]:
        """Get rotation (yaw_deg, pitch_deg, roll_deg)."""
        return self._get(METHOD_GET_ROTATION)

    def set_rotation(self, yaw: float, pitch: float, roll: float) -> int:
        return self._set(METHOD_SET_ROTATION, {
            "yaw_deg": yaw, "pitch_deg": pitch, "roll_deg": roll,
        })

    def get_sensor_position(self) -> Optional[int]:
        data = self._get(METHOD_GET_SENSOR_POSITION)
        return data.get("position_code") if data else None

    def set_sensor_position(self, position_code: int) -> int:
        return self._set(METHOD_SET_SENSOR_POSITION,
                         {"position_code": position_code})

    # ── Firmware Update (0x008x) ──

    def firmware_update_start(self, update_type: str, component: str,
                              total_size: int, crc32) -> Optional[dict]:
        """Open an update session (programming mode).

        Args:
            update_type: "firmware" or "bootloader"
            component: "m7_0", "m7_1", "m7_2", "a53" or "bootloader"
            total_size: image size in bytes
            crc32: image CRC-32 as an int or "0x%08X" string. The radar reads
                a string without "0x" as decimal, so 8 bare hex digits get
                the prefix; None omits the key and the radar skips the check.

        Returns response data (max_chunk_size, target_slot, state, ...) or None.
        """
        payload = {"update_type": update_type, "component": component,
                   "total_size": total_size}
        if isinstance(crc32, int) and not isinstance(crc32, bool):
            payload["crc32"] = f"0x{crc32 & 0xFFFFFFFF:08X}"
        elif isinstance(crc32, str) and crc32:
            bare = len(crc32) == 8 and all(c in "0123456789abcdefABCDEF" for c in crc32)
            payload["crc32"] = "0x" + crc32 if bare else crc32
        elif crc32 is not None:
            self._invalid()
            return None
        return self._get_with_payload(METHOD_FW_UPDATE_START, payload)

    def firmware_data_chunk(self, data: bytes) -> Optional[dict]:
        """Send one raw image chunk (at most max_chunk_size bytes).

        Returns the answer (received_bytes, total_bytes, state, ...) or None
        (see ``last_error``). A chunk may be accepted only in part:
        received_bytes is the next offset and the unaccepted tail is sent
        again (07_Firmware_Update §07.A).
        """
        err, resp = self._send_raw(METHOD_FW_DATA_CHUNK, data)
        if err != OK:
            return None
        return resp if isinstance(resp, dict) else {}

    def firmware_update_commit(self) -> int:
        """Commit (verify CRC + start flash write)."""
        return self._action(METHOD_FW_UPDATE_COMMIT)

    def firmware_update_status(self) -> Optional[dict]:
        """Get OTA update status (state, progress_pct, ...)."""
        return self._get(METHOD_FW_UPDATE_STATUS)

    def firmware_update_abort(self) -> int:
        """Abort OTA firmware update."""
        return self._action(METHOD_FW_UPDATE_ABORT)

    def firmware_switch_slot(self, update_type: str) -> Optional[dict]:
        """Switch active boot slot. Returns (previous_active, new_active)."""
        return self._get_with_payload(METHOD_FW_SWITCH_SLOT,
                                      {"update_type": update_type})

    def firmware_update_reboot(self) -> int:
        """Reboot sensor after firmware update."""
        return self._action(METHOD_FW_UPDATE_REBOOT)

    def get_boot_info(self) -> Optional[dict]:
        """Get Boot-Info v2: fw_update_active, fw_update_valid_a/b,
        fw_update_boot_count, boot_info_status."""
        return self._get(METHOD_GET_BOOT_INFO)

    # ── Config Persistence (0x009x) ──

    def get_config_status(self) -> Optional[dict]:
        """Get NVM load status (load_status, load_status_name)."""
        return self._get(METHOD_GET_CONFIG_STATUS)

    def reset_config_defaults(self, keys: List[str]) -> Optional[dict]:
        """Reset listed config keys to factory defaults (diagnostic mode; the
        result is written to NVM before the answer and applies after restart)."""
        if (not isinstance(keys, (list, tuple)) or not keys
                or any(not isinstance(key, str) or not key for key in keys)):
            self._invalid()
            return None
        return self._get_with_payload(METHOD_RESET_CONFIG_DEFAULTS,
                                      {"keys": list(keys)})

    # ── Internal ──

    def _request(self, method_id: int, payload: Optional[dict] = None
                 ) -> Tuple[int, Optional[dict]]:
        """One Config API call; returns (SDK error code, response data)."""
        if not self._client:
            self._last_error = NOT_INITIALIZED
            return NOT_INITIALIZED, None
        rc, data = self._client.request(method_id, payload)
        self._last_error = error_from_result(rc)
        return self._last_error, data

    def _invalid(self) -> int:
        """An argument the SDK rejects before sending anything."""
        self._last_error = INVALID_PARAM
        return INVALID_PARAM

    def _get(self, method_id: int) -> Optional[dict]:
        err, data = self._request(method_id)
        if err != OK or data is None:
            return None
        return data

    def _set(self, method_id: int, payload: dict) -> int:
        err, _ = self._request(method_id, payload)
        return err

    def _get_with_payload(self, method_id: int,
                          payload: dict) -> Optional[dict]:
        """Request that includes a payload and returns response data."""
        err, data = self._request(method_id, payload)
        if err != OK:
            return None
        return data if data is not None else {}

    def _action(self, method_id: int) -> int:
        return self._set(method_id, {})

    def _send_raw(self, method_id: int,
                  data: bytes) -> Tuple[int, Optional[dict]]:
        """Send a binary body (FirmwareDataChunk). Returns (SDK error, data)."""
        if not self._client:
            self._last_error = NOT_INITIALIZED
            return NOT_INITIALIZED, None
        rc, resp = self._client.request_raw(method_id, bytes(data))
        self._last_error = error_from_result(rc)
        return self._last_error, resp

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
