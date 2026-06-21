"""
AFI920 Python SDK — AfiSdk class

High-level Python API for AFI920 4D Imaging Radar.
Mirrors the C++ AfiSdk API for consistent developer experience.

Protocol v3.0 method ID registry.

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import socket
from dataclasses import dataclass
from typing import List, Optional, Tuple

from afi_sdk.someip import (
    ConfigClient,
    discover as _discover,
    DEFAULT_CSII_PORT,
    RANGE_MODE_NAMES,
    METHOD_GET_DEVICE_INFO,
    METHOD_GET_SENSOR_STATUS,
    METHOD_GET_DIAGNOSTIC_INFO,
    METHOD_CLEAR_FAULT_LOG,
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
    METHOD_GET_DETECTION_DENSITY,
    METHOD_SET_DETECTION_DENSITY,
    METHOD_GET_PTP_CONFIG,
    METHOD_SET_PTP_CONFIG,
    METHOD_GET_MOUNTING_POSITION,
    METHOD_SET_MOUNTING_POSITION,
    METHOD_GET_ROTATION,
    METHOD_SET_ROTATION,
    METHOD_GET_SENSOR_POSITION,
    METHOD_SET_SENSOR_POSITION,
    METHOD_SAVE_CONFIG,
    METHOD_GET_CONFIG_STATUS,
    METHOD_RESET_CONFIG_DEFAULTS,
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


# ── Enums ──

class RangeMode:
    """Radar range mode (mirrors C++ AfiRangeMode)."""
    DUAL_RANGE = 0        # DR
    LONG_RANGE = 1        # LR
    MID_RANGE = 2         # MR
    SHORT_RANGE = 3       # SR
    ULTRA_LONG_RANGE = 4  # ULR
    ULTRA_SHORT_RANGE = 5 # USR

    @staticmethod
    def name(mode: int) -> str:
        return RANGE_MODE_NAMES.get(mode, "Unknown")


# ── Error Codes ──

OK = 0
DISCONNECTED = -1
TIMEOUT = -2
PROTOCOL_ERROR = -3
SENSOR_ERROR = -8
NOT_SUPPORTED = -9

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
        self._csii_counter = 0
        self._csii_session = 0

    def init(self, config: AfiSdkConfig) -> int:
        """Initialize and connect to sensor.

        Args:
            config: SDK configuration with sensor IP and ports

        Returns:
            OK (0) on success, negative error code on failure
        """
        self._config = config
        self._client = ConfigClient(config.sensor_ip, config.config_port,
                                    config.timeout)
        if not self._client.connect():
            return DISCONNECTED
        return OK

    def close(self):
        """Disconnect from sensor."""
        if self._client:
            self._client.close()
            self._client = None
        if self._csii_sock:
            try:
                self._csii_sock.close()
            except OSError:
                pass
            self._csii_sock = None

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
                           sensor_id: int = 0,
                           timestamp_ns: int = 0) -> int:
        """Send one CSII vehicle-input message (TCP 30501, Event 0x8001).

        Call cyclically (100 ms recommended). Connects lazily on first use;
        the SDK maintains the message/E2E counters.
        """
        if not self._config:
            return DISCONNECTED

        if self._csii_sock is None:
            try:
                self._csii_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self._csii_sock.settimeout(self._config.timeout)
                self._csii_sock.connect((self._config.sensor_ip,
                                         self._config.csii_port))
                self._csii_sock.setsockopt(socket.IPPROTO_TCP,
                                           socket.TCP_NODELAY, 1)
            except OSError:
                self._csii_sock = None
                return DISCONNECTED

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
            try:
                self._csii_sock.close()
            except OSError:
                pass
            self._csii_sock = None
            return DISCONNECTED
        return OK

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

    def clear_fault_log(self) -> int:
        """Clear fault log."""
        return self._action(METHOD_CLEAR_FAULT_LOG)

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
        {detection,health,performance}_{ip,port,protocol}.

        Returns the response data (contains restart_required) or None on error.
        """
        return self._get_with_payload(METHOD_SET_NETWORK_CONFIG, fields)

    def set_stream_destination(self, stream: str, ip: str, port: int,
                               protocol: str = "tcp") -> int:
        """Set one stream destination (applies immediately, no restart).

        Args:
            stream: "detection", "health", or "performance"
        """
        if stream not in ("detection", "health", "performance"):
            return PROTOCOL_ERROR
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
        """Set range mode (RangeMode.LONG_RANGE, etc.)."""
        return self._set(METHOD_SET_RANGE_MODE, {"range_mode": mode})

    # ── Detection (0x004x) ──

    def get_detection_threshold(self) -> Optional[dict]:
        """Get detection thresholds (snr_threshold_db, cfar_table_*)."""
        return self._get(METHOD_GET_DETECTION_THRESHOLD)

    def set_detection_threshold(
        self,
        snr_db: Optional[float] = None,
        cfar_table_first_frame: Optional[List[int]] = None,
        cfar_table_second_frame: Optional[List[int]] = None,
    ) -> int:
        """Set SNR threshold and/or 19-level CFAR tables."""
        payload = {}
        if snr_db is not None:
            payload["snr_threshold_db"] = snr_db

        if cfar_table_first_frame is not None:
            levels = _normalize_cfar_table(cfar_table_first_frame)
            if levels is None:
                return PROTOCOL_ERROR
            payload["cfar_table_first_frame"] = levels

        if cfar_table_second_frame is not None:
            levels = _normalize_cfar_table(cfar_table_second_frame)
            if levels is None:
                return PROTOCOL_ERROR
            payload["cfar_table_second_frame"] = levels

        return self._set(METHOD_SET_DETECTION_THRESHOLD, payload)

    def get_detection_filters(self) -> Optional[dict]:
        """Get the nine detection filter toggles (filter_fov, filter_aps_noise, ...)."""
        return self._get(METHOD_GET_DETECTION_FILTERS)

    def set_detection_filters(self, **filters) -> int:
        """Set detection filter toggles (any subset).

        Keyword fields: filter_fov, filter_aps_noise, filter_ego_specular,
        filter_ground_sidelobe, filter_upper_structure, filter_multibounce_2x,
        filter_structure_multipath, filter_specular_mirror,
        filter_wheel_microdoppler.
        """
        return self._set(METHOD_SET_DETECTION_FILTERS, filters)

    def get_detection_density(self) -> Optional[dict]:
        """Get measurement density toggles (range_density, angle_density)."""
        return self._get(METHOD_GET_DETECTION_DENSITY)

    def set_detection_density(self, range_density: Optional[bool] = None,
                              angle_density: Optional[bool] = None) -> int:
        """Set measurement density toggles (any subset)."""
        payload = {}
        if range_density is not None:
            payload["range_density"] = range_density
        if angle_density is not None:
            payload["angle_density"] = angle_density
        return self._set(METHOD_SET_DETECTION_DENSITY, payload)

    # ── Time Sync (0x005x) ──

    def get_ptp_config(self) -> Optional[dict]:
        """Get PTP profile + live sync status (state, offset_ns, master_clock_id)."""
        return self._get(METHOD_GET_PTP_CONFIG)

    def set_ptp_config(self, profile: int = 0) -> int:
        """Set PTP profile (currently only profile 0 is supported)."""
        return self._set(METHOD_SET_PTP_CONFIG, {"profile": profile})

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

    # ── Config Persistence (0x009x) ──

    def save_config(self) -> int:
        """Save current configuration to NVM."""
        return self._action(METHOD_SAVE_CONFIG)

    def get_config_status(self) -> Optional[dict]:
        """Get NVM load status (load_status, load_status_name)."""
        return self._get(METHOD_GET_CONFIG_STATUS)

    def reset_config_defaults(self, keys: List[str]) -> Optional[dict]:
        """Reset listed config keys to factory defaults in RAM."""
        if not isinstance(keys, (list, tuple)) or not keys:
            return None
        if any(not isinstance(key, str) or not key for key in keys):
            return None
        return self._get_with_payload(METHOD_RESET_CONFIG_DEFAULTS,
                                      {"keys": list(keys)})

    # ── Internal ──

    def _get(self, method_id: int) -> Optional[dict]:
        if not self._client:
            return None
        rc, data = self._client.request(method_id)
        if rc != 0 or data is None:
            return None
        return data

    def _set(self, method_id: int, payload: dict) -> int:
        if not self._client:
            return DISCONNECTED
        rc, _ = self._client.request(method_id, payload)
        return OK if rc == 0 else SENSOR_ERROR

    def _get_with_payload(self, method_id: int,
                          payload: dict) -> Optional[dict]:
        """Request that includes a payload and returns response data."""
        if not self._client:
            return None
        rc, data = self._client.request(method_id, payload)
        if rc != 0:
            return None
        return data if data is not None else {}

    def _action(self, method_id: int) -> int:
        return self._set(method_id, {})

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
