"""
Live interface-conformance verification against real AFI920 sensors.

Validates the protocol v3.0 spec on the wire, per sensor:
  [D] Discovery     — required response fields, value sanity, dedup
  [C] Config API    — GET keys/types/ranges per category, non-destructive
                      same-value SETs, removed-legacy-ID rejection
  [S] Data streams  — SOME/IP header, E2E Profile 7 (CRC-64/XZ, counter,
                      data_id), ISO payload conformance (RDI/SHII/SPI),
                      ~100 ms cycle, counter continuity
  [V] CSII input    — send vehicle input, observe SHII input signal status

Multi-sensor: pass any number of IPs (or discover). Streams are TCP
(host connects to each sensor's own ports), so sensors never collide
on host ports.

Usage:
    python test/test_live_conformance.py                       # discover all
    python test/test_live_conformance.py 192.168.10.150 [...]  # explicit IPs
    python test/test_live_conformance.py --bind 192.168.10.139 # discovery NIC
    python test/test_live_conformance.py --duration 8 --skip-csii

Exit code 0 = all hard checks passed on every sensor.
"""

import argparse
import math
import socket
import struct
import sys
import time
from pathlib import Path

# Korean Windows consoles default to CP949 — force UTF-8 for report glyphs
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))

from afi_sdk.someip import (  # noqa: E402
    ConfigClient, discover,
    SOMEIP_HEADER_FMT, SOMEIP_HEADER_SIZE, SERVICE_ID,
    EVENT_RDI, EVENT_SHII, EVENT_SPI,
    RANGE_MODE_NAMES,
    METHOD_GET_DEVICE_INFO, METHOD_GET_SENSOR_STATUS,
    METHOD_GET_DIAGNOSTIC_INFO, METHOD_GET_COMMAND_HISTORY,
    METHOD_GET_NETWORK_CONFIG, METHOD_GET_RANGE_MODE, METHOD_SET_RANGE_MODE,
    METHOD_GET_DETECTION_THRESHOLD, METHOD_SET_DETECTION_THRESHOLD,
    METHOD_GET_DETECTION_FILTERS,
    METHOD_SET_DETECTION_FILTERS, METHOD_GET_DETECTION_DENSITY,
    METHOD_SET_DETECTION_DENSITY, METHOD_GET_PTP_CONFIG,
    METHOD_GET_MOUNTING_POSITION, METHOD_GET_ROTATION,
    METHOD_GET_SENSOR_POSITION,
    METHOD_GET_CONFIG_STATUS,
)
from afi_sdk import streams  # noqa: E402

PI = math.pi


class Section:
    """Collects named checks for one report section."""

    def __init__(self, tag: str, title: str):
        self.tag = tag
        self.title = title
        self.results = []     # (ok, name, detail)
        self.infos = []       # informational lines

    def check(self, ok: bool, name: str, detail: str = ""):
        self.results.append((bool(ok), name, detail))
        mark = "PASS" if ok else "FAIL"
        print(f"  [{self.tag}] {mark}: {name}" + (f" — {detail}" if detail else ""))
        return ok

    def info(self, line: str):
        self.infos.append(line)
        print(f"  [{self.tag}] info: {line}")

    @property
    def failed(self):
        return [r for r in self.results if not r[0]]


# ─── [D] Discovery ───

DISCOVERY_REQUIRED_FIELDS = [
    "serial_number", "mac_address", "model_name",
    "hw_version", "software_version",
    "bootloader_hash", "driver_hash", "fw_hash",
    "ip_address", "subnet_mask", "gateway",
    "api_port", "detection_port", "health_port", "performance_port",
    "detection_protocol", "health_protocol", "performance_protocol",
    "position_code", "offset_x_m", "offset_y_m", "offset_z_m",
    "yaw_deg", "pitch_deg", "roll_deg",
    "range_mode", "range_mode_name",
]


def run_discovery_section(ip: str, info: dict) -> Section:
    s = Section("D", "Discovery")
    missing = [f for f in DISCOVERY_REQUIRED_FIELDS if f not in info]
    s.check(not missing, "required fields present",
            f"missing: {missing}" if missing else f"{len(DISCOVERY_REQUIRED_FIELDS)} fields")
    s.check(info.get("ip_address") == ip, "ip_address matches target",
            f"{info.get('ip_address')} vs {ip}")

    rm = info.get("range_mode", -1)
    s.check(rm in RANGE_MODE_NAMES, "range_mode in registry (0..5)", f"value={rm}")
    if rm in RANGE_MODE_NAMES:
        s.check(info.get("range_mode_name") == RANGE_MODE_NAMES[rm],
                "range_mode_name matches code",
                f"{info.get('range_mode_name')} vs {RANGE_MODE_NAMES[rm]}")

    for proto_key in ("detection_protocol", "health_protocol", "performance_protocol"):
        v = info.get(proto_key, "")
        s.check(v in ("tcp", "udp"), f"{proto_key} valid", v)

    ss = info.get("scan_status")
    s.check(isinstance(ss, dict) and "scan_index" in ss and "uptime_seconds" in ss,
            "scan_status object (scan_index, uptime_seconds)", str(ss))
    s.info(f"serial={info.get('serial_number')} sw={info.get('software_version')} "
           f"hashes(bl/drv/fw)={info.get('bootloader_hash')}/{info.get('driver_hash')}"
           f"/{info.get('fw_hash')} ptp={info.get('ptp_state')} "
           f"radar_active={info.get('radar_active')} mode={info.get('radar_mode')}")
    return s


# ─── [C] Config API ───

def run_config_section(ip: str) -> Section:
    s = Section("C", "Config API")
    client = ConfigClient(ip)
    if not client.connect():
        s.check(False, "TCP 30500 connect", "connection failed")
        return s
    s.check(True, "TCP 30500 connect")

    def get(mid, name):
        rc, data = client.request(mid)
        ok = (rc == 0 and isinstance(data, dict))
        s.check(ok, f"{name} (0x{mid:04X}) returns data", f"rc={rc}")
        return data if ok else None

    # Device Info & Status (0x001x)
    dev = get(METHOD_GET_DEVICE_INFO, "GetDeviceInfo")
    if dev:
        keys = ["serial_number", "mac_address", "model_name", "hw_version",
                "software_version", "bootloader_hash", "driver_hash", "fw_hash"]
        missing = [k for k in keys if k not in dev]
        s.check(not missing, "GetDeviceInfo v3.0 keys", f"missing: {missing}" if missing else "")

    status = get(METHOD_GET_SENSOR_STATUS, "GetSensorStatus")
    if status:
        code = status.get("operation_state_code", -1)
        s.check(0 <= code <= 3, "operation_state_code in 0..3", f"{code}")
        s.check(status.get("uptime_sec", 0) > 0, "uptime_sec > 0",
                f"{status.get('uptime_sec')}")

    diag = get(METHOD_GET_DIAGNOSTIC_INFO, "GetDiagnosticInfo")
    if diag:
        s.check("combined_state" in diag and "active_fault_count" in diag,
                "GetDiagnosticInfo flat fault schema",
                f"state={diag.get('combined_state')} faults={diag.get('active_fault_count')}")
        if isinstance(diag.get("faults"), list) and diag["faults"]:
            f0 = diag["faults"][0]
            s.check(all(k in f0 for k in ("id", "name", "source", "level_code")),
                    "fault entry keys", str(sorted(f0.keys())))

    get(METHOD_GET_COMMAND_HISTORY, "GetCommandHistoryInfo")

    # Network (0x002x) — unified payload
    net = get(METHOD_GET_NETWORK_CONFIG, "GetNetworkConfig")
    if net:
        radar_keys = ["ip_address", "subnet_mask", "gateway", "api_port", "vlan_id"]
        missing = [k for k in radar_keys if k not in net]
        s.check(not missing, "NetworkConfig radar fields", f"missing: {missing}" if missing else "")
        stream_keys = [f"{p}_{f}" for p in ("detection", "health", "performance")
                       for f in ("ip", "port", "protocol")]
        present = [k for k in stream_keys if k in net]
        s.check(len(present) == len(stream_keys), "NetworkConfig stream destinations",
                f"{len(present)}/{len(stream_keys)} present")

    # Range Mode (0x003x) + same-value write-back
    rm = get(METHOD_GET_RANGE_MODE, "GetRangeMode")
    if rm:
        mode = rm.get("range_mode", -1)
        s.check(mode in RANGE_MODE_NAMES, "range_mode in 0..5", f"{mode}")
        rc, _ = client.request(METHOD_SET_RANGE_MODE, {"range_mode": mode})
        s.check(rc == 0, "SetRangeMode same-value accepted", f"rc={rc}")

    # Detection (0x004x)
    th = get(METHOD_GET_DETECTION_THRESHOLD, "GetDetectionThreshold")
    if th:
        snr = th.get("snr_threshold_db", -1)
        snr_ok = isinstance(snr, (int, float)) and 0.0 <= snr <= 30.0
        s.check(snr_ok, "snr_threshold_db plausible", f"snr={snr}")

        def valid_cfar_table(value):
            return (
                isinstance(value, list)
                and len(value) == 19
                and all(isinstance(v, int) and not isinstance(v, bool) and 1 <= v <= 9
                        for v in value)
            )

        cfar_first = th.get("cfar_table_first_frame")
        cfar_second = th.get("cfar_table_second_frame")
        cfar_first_ok = valid_cfar_table(cfar_first)
        cfar_second_ok = valid_cfar_table(cfar_second)
        s.check(cfar_first_ok, "cfar_table_first_frame has 19 levels 1..9",
                str(cfar_first))
        s.check(cfar_second_ok, "cfar_table_second_frame has 19 levels 1..9",
                str(cfar_second))

        if snr_ok and cfar_first_ok and cfar_second_ok:
            rc, _ = client.request(METHOD_SET_DETECTION_THRESHOLD, {
                "snr_threshold_db": snr,
                "cfar_table_first_frame": cfar_first,
                "cfar_table_second_frame": cfar_second,
            })
            s.check(rc == 0, "SetDetectionThreshold same-value accepted", f"rc={rc}")

    FILTERS = ["filter_fov", "filter_aps_noise", "filter_ego_specular",
               "filter_ground_sidelobe", "filter_upper_structure",
               "filter_multibounce_2x", "filter_structure_multipath",
               "filter_specular_mirror", "filter_wheel_microdoppler"]
    filt = get(METHOD_GET_DETECTION_FILTERS, "GetDetectionFilters")
    if filt:
        missing = [k for k in FILTERS if k not in filt]
        s.check(not missing, "9 detection filters incl. APS",
                f"missing: {missing}" if missing else "")
        bad_types = [k for k in FILTERS if k in filt and not isinstance(filt[k], bool)]
        s.check(not bad_types, "filter values are bool",
                f"non-bool: {bad_types}" if bad_types else "")
        rc, _ = client.request(METHOD_SET_DETECTION_FILTERS,
                               {"filter_fov": filt.get("filter_fov", True)})
        s.check(rc == 0, "SetDetectionFilters partial same-value accepted", f"rc={rc}")

    dens = get(METHOD_GET_DETECTION_DENSITY, "GetDetectionDensity")
    if dens:
        s.check("range_density" in dens and "angle_density" in dens,
                "density toggles present",
                f"range={dens.get('range_density')} angle={dens.get('angle_density')}")
        rc, _ = client.request(METHOD_SET_DETECTION_DENSITY,
                               {"range_density": dens.get("range_density", True)})
        s.check(rc == 0, "SetDetectionDensity same-value accepted", f"rc={rc}")

    # Time Sync (0x005x) — combined config + status
    ptp = get(METHOD_GET_PTP_CONFIG, "GetPTPConfig")
    if ptp:
        s.check("profile" in ptp and "state" in ptp,
                "PTP combined config+status",
                f"profile={ptp.get('profile')} state={ptp.get('state')} "
                f"offset={ptp.get('offset_ns')}ns")

    # Mounting (0x007x)
    get(METHOD_GET_MOUNTING_POSITION, "GetMountingPosition")
    get(METHOD_GET_ROTATION, "GetRotation")
    get(METHOD_GET_SENSOR_POSITION, "GetSensorPosition")

    # Persistence status reads
    get(METHOD_GET_CONFIG_STATUS, "GetConfigStatus")

    # Removed legacy method IDs must be rejected (registry renumber proof)
    for legacy_id, name in ((0x0032, "old GetStandbyMode"),
                            (0x00E0, "old SaveConfig")):
        rc, _ = client.request(legacy_id)
        s.check(rc != 0, f"legacy 0x{legacy_id:04X} ({name}) rejected", f"rc={rc}")

    client.close()
    return s


# ─── [S] Data streams ───

E2E_DATA_IDS = {EVENT_RDI: streams.E2E_DATA_ID_RDI,
                EVENT_SHII: streams.E2E_DATA_ID_SHII,
                EVENT_SPI: streams.E2E_DATA_ID_SPI}


class StreamStats:
    def __init__(self):
        self.frames = 0
        self.someip_bad = 0
        self.e2e_crc_bad = 0
        self.e2e_id_bad = 0
        self.e2e_len_bad = 0
        self.e2e_counter_gaps = 0
        self.e2e_counter_nonmono = 0
        self.parse_fail = 0
        self.payload_bad = 0
        self.payload_bad_detail = ""
        self.msg_counter_gaps = 0
        self.first_ts = None
        self.last_ts = None
        self.recv_t0 = None
        self.recv_t1 = None
        self.last_e2e_counter = None
        self.last_msg_counter = None
        self.detections_total = 0
        self.sample = None


def _recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf


def check_rdi_payload(payload, st: StreamStats):
    hdr = streams.parse_rdi_frame(payload)
    if hdr is None:
        st.parse_fail += 1
        return
    n = hdr["num_detections"]
    problems = []
    if hdr["interface_id"] != 0x08:
        problems.append(f"interface_id=0x{hdr['interface_id']:02X}")
    if len(payload) != streams.RDI_HEADER_SIZE + n * streams.RDI_DETECTION_SIZE:
        problems.append(f"size {len(payload)} != 36+51*{n}")
    if n > streams.RDI_MAX_DETECTIONS:
        problems.append(f"num_detections {n}")
    st.detections_total += n
    if n > 0:
        d = streams.parse_rdi_detection(payload, hdr["det_start"], 0)
        if d:
            if not (0 <= d["existence_probability"] <= 100):
                problems.append(f"existence {d['existence_probability']}")
            if not (0.0 <= d["radial_distance"] <= 1000.0):
                problems.append(f"range {d['radial_distance']}")
            if not (-PI <= d["azimuth"] <= PI):
                problems.append(f"azimuth {d['azimuth']}")
            if st.sample is None:
                st.sample = (f"det[0]: r={d['radial_distance']:.2f}m "
                             f"az={d['azimuth']:.3f} el={d['elevation']:.3f} "
                             f"v={d['radial_velocity']:.2f}m/s rcs={d['rcs']:.1f} "
                             f"snr={d['snr']:.1f}")
    if problems:
        st.payload_bad += 1
        st.payload_bad_detail = "; ".join(problems)
    return hdr


VALID_OP_MODES = {0, 1, 2, 3, 4, 5, 10, 50, 100, 200, 201}


def check_shi_payload(payload, st: StreamStats):
    msg = streams.parse_shi(payload)
    if msg is None:
        st.parse_fail += 1
        return
    problems = []
    if msg["interface_id"] != 0x0D:
        problems.append(f"interface_id=0x{msg['interface_id']:02X}")
    bad_modes = [m for m in msg["operation_modes"] if m not in VALID_OP_MODES]
    if bad_modes:
        problems.append(f"op_modes {bad_modes}")
    if len(msg["operation_modes"]) > 11 or len(msg["calibration_components"]) > 3:
        problems.append("array counts exceed max")
    if st.sample is None:
        st.sample = (f"modes={msg['operation_modes']} defect={msg['defect_recognised']}"
                     f"/{msg['defect_reason']} volt={msg['supply_voltage_status']} "
                     f"temp={msg['temperature_status']} sync={msg['time_sync']} "
                     f"calib={list(zip(msg['calibration_components'], msg['calibration_statuses'], msg['calibration_states']))} "
                     f"inputs={list(zip(msg['input_signal_types'], msg['input_signal_statuses']))}")
    if problems:
        st.payload_bad += 1
        st.payload_bad_detail = "; ".join(problems)
    return msg


def check_spi_payload(payload, st: StreamStats):
    msg = streams.parse_spi(payload)
    if msg is None:
        st.parse_fail += 1
        return
    problems = []
    if msg["interface_id"] != 0x0C:
        problems.append(f"interface_id=0x{msg['interface_id']:02X}")
    if msg["vehicle_coord_system"] not in (0, 1):
        problems.append(f"vcs={msg['vehicle_coord_system']}")
    if st.sample is None:
        p = msg["pose"]
        st.sample = (f"pose origin=({p['origin_x']:.2f},{p['origin_y']:.2f},"
                     f"{p['origin_z']:.2f}) ypr=({p['yaw']:.3f},{p['pitch']:.3f},"
                     f"{p['roll']:.3f}) fov_segs={len(msg.get('fov_segments', []))}")
    if problems:
        st.payload_bad += 1
        st.payload_bad_detail = "; ".join(problems)
    return msg


PAYLOAD_CHECKS = {EVENT_RDI: check_rdi_payload,
                  EVENT_SHII: check_shi_payload,
                  EVENT_SPI: check_spi_payload}


def receive_stream(ip: str, port: int, event_id: int, duration: float,
                   shi_sink: list = None) -> StreamStats:
    st = StreamStats()
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        sock.settimeout(3.0)
        sock.connect((ip, port))
    except OSError:
        return st

    st.recv_t0 = time.monotonic()
    deadline = st.recv_t0 + duration
    while time.monotonic() < deadline:
        try:
            pre = _recv_exact(sock, 8)
            if len(pre) < 8:
                break
            length = struct.unpack(">I", pre[4:8])[0]
            rest = _recv_exact(sock, length)
            if len(rest) < length:
                break
        except socket.timeout:
            continue
        except OSError:
            break

        data = pre + rest
        h = struct.unpack(SOMEIP_HEADER_FMT, data[:SOMEIP_HEADER_SIZE])
        svc, mid, _, _, _, proto, iface, mtype, ret = h
        if svc != SERVICE_ID or mid != event_id or mtype != 0x02 \
                or proto != 0x01 or ret != 0x00:
            st.someip_bad += 1
            continue

        frame = data[SOMEIP_HEADER_SIZE:]
        e2e, iso = streams.strip_e2e(frame)
        if e2e is None:
            st.someip_bad += 1
            continue
        if not e2e["crc_ok"]:
            st.e2e_crc_bad += 1
        if e2e["data_id"] != E2E_DATA_IDS[event_id]:
            st.e2e_id_bad += 1
        if e2e["length"] != len(iso):
            st.e2e_len_bad += 1
        if st.last_e2e_counter is not None:
            delta = (e2e["counter"] - st.last_e2e_counter) & 0xFFFFFFFF
            if delta == 0 or delta > 0x80000000:
                st.e2e_counter_nonmono += 1
            elif delta > 1:
                st.e2e_counter_gaps += delta - 1
        st.last_e2e_counter = e2e["counter"]

        st.frames += 1
        msg = PAYLOAD_CHECKS[event_id](iso, st)
        if msg:
            mc = msg["message_counter"]
            if st.last_msg_counter is not None:
                d = (mc - st.last_msg_counter) & 0xFFFFFFFF
                if 1 < d < 1000:
                    st.msg_counter_gaps += d - 1
            st.last_msg_counter = mc
            if st.first_ts is None:
                st.first_ts = msg["timestamp"]
            st.last_ts = msg["timestamp"]
            if shi_sink is not None and event_id == EVENT_SHII:
                shi_sink.append(msg)
    st.recv_t1 = time.monotonic()
    sock.close()
    return st


def run_stream_section(ip: str, info: dict, duration: float) -> Section:
    s = Section("S", "Data Streams")
    specs = [("RDI", info.get("detection_port", 30509), EVENT_RDI),
             ("SHII", info.get("health_port", 30510), EVENT_SHII),
             ("SPI", info.get("performance_port", 30511), EVENT_SPI)]

    import threading
    stats = {}
    threads = []
    for name, port, eid in specs:
        def worker(n=name, p=port, e=eid):
            stats[n] = receive_stream(ip, p, e, duration)
        t = threading.Thread(target=worker, daemon=True)
        t.start()
        threads.append(t)
    for t in threads:
        t.join(timeout=duration + 10)

    min_frames = int(duration * 5)  # expect ~10 Hz, require half
    for name, port, eid in specs:
        st = stats.get(name)
        if st is None or st.frames == 0:
            s.check(False, f"{name} frames received", "0 frames")
            continue
        elapsed = (st.recv_t1 - st.recv_t0) if st.recv_t1 else duration
        rate = st.frames / elapsed if elapsed > 0 else 0
        s.check(st.frames >= min_frames, f"{name} frames received",
                f"{st.frames} in {elapsed:.1f}s ({rate:.1f} Hz)")
        s.check(st.someip_bad == 0, f"{name} SOME/IP header conformance",
                f"{st.someip_bad} bad")
        s.check(st.e2e_crc_bad == 0, f"{name} E2E CRC-64 all valid",
                f"{st.e2e_crc_bad}/{st.frames} bad")
        s.check(st.e2e_id_bad == 0, f"{name} E2E data_id == 0x{E2E_DATA_IDS[eid]:08X}",
                f"{st.e2e_id_bad} bad")
        s.check(st.e2e_len_bad == 0, f"{name} E2E length == ISO size",
                f"{st.e2e_len_bad} bad")
        s.check(st.e2e_counter_nonmono == 0, f"{name} E2E counter monotonic",
                f"{st.e2e_counter_nonmono} non-monotonic, {st.e2e_counter_gaps} gaps")
        s.check(st.parse_fail == 0 and st.payload_bad == 0,
                f"{name} ISO payload conformance",
                f"parse_fail={st.parse_fail} bad={st.payload_bad} {st.payload_bad_detail}")
        s.check(7.0 <= rate <= 13.0, f"{name} cycle ≈ 100 ms",
                f"{rate:.1f} Hz")
        if st.first_ts and st.last_ts and st.frames > 1:
            span_s = (st.last_ts - st.first_ts) / 1e9
            drift = abs(span_s - (st.frames - 1) * 0.1)
            s.info(f"{name} sensor-timestamp span {span_s:.2f}s over "
                   f"{st.frames} frames (drift vs 100ms grid: {drift:.2f}s), "
                   f"msg_counter gaps={st.msg_counter_gaps}")
        if name == "RDI":
            s.info(f"RDI detections total={st.detections_total} "
                   f"avg={st.detections_total / max(st.frames, 1):.1f}/frame")
        if st.sample:
            s.info(f"{name} {st.sample}")
    return s


# ─── [V] CSII vehicle input ───

def run_csii_section(ip: str, info: dict, duration: float) -> Section:
    s = Section("V", "CSII Vehicle Input")
    csii_port = 30501

    # Baseline SHII input signal statuses
    pre = []
    receive_stream(ip, info.get("health_port", 30510), EVENT_SHII, 1.5, shi_sink=pre)

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        sock.connect((ip, csii_port))
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError as e:
        s.check(False, f"TCP {csii_port} connect", str(e))
        return s
    s.check(True, f"TCP {csii_port} connect")

    # Publish ~2s of CSII at 100 ms while sampling SHII concurrently
    import threading
    post = []
    rx = threading.Thread(
        target=receive_stream,
        args=(ip, info.get("health_port", 30510), EVENT_SHII, duration),
        kwargs={"shi_sink": post}, daemon=True)
    rx.start()

    sent = 0
    try:
        n_frames = max(int(duration / 0.1), 10)
        for i in range(n_frames):
            payload = streams.build_csii_payload(
                somc=streams.SOMC_NORMAL_MODE,
                message_counter=i,
                velocity_mps=(5.0, 0.0, 0.0),
                gear_position=streams.GEAR_FORWARD,
                global_timestamp_utc=time.time())
            frame = streams.build_csii_frame(payload, counter=i, session_id=i + 1)
            sock.sendall(frame)
            sent += 1
            time.sleep(0.1)
    except OSError as e:
        s.check(False, "CSII frames sent", f"send failed after {sent}: {e}")
        sock.close()
        return s
    s.check(sent == n_frames, "CSII frames sent",
            f"{sent} frames @100ms (115B each, E2E CRC)")
    sock.close()
    rx.join(timeout=duration + 5)

    def status_summary(msgs):
        if not msgs:
            return None
        last = msgs[-1]
        return list(zip(last["input_signal_types"], last["input_signal_statuses"]))

    before, after = status_summary(pre), status_summary(post)
    s.info(f"SHII input signals before CSII: {before}")
    s.info(f"SHII input signals during CSII: {after}")
    # Informational: v1.1 firmware stores CSII but feedback wiring may be partial.
    if after is not None:
        improved = before != after
        s.info("input signal status changed during CSII"
               if improved else "input signal status unchanged (FW v1.1: store-only)")
    return s


# ─── main ───

def verify_sensor(info: dict, duration: float, skip_csii: bool) -> bool:
    ip = info.get("ip_address") or info.get("_source_ip")
    print(f"\n{'=' * 64}\n Sensor {info.get('serial_number', '?')} @ {ip} "
          f"(sw {info.get('software_version', '?')})\n{'=' * 64}")
    sections = [
        run_discovery_section(ip, info),
        run_config_section(ip),
        run_stream_section(ip, info, duration),
    ]
    if not skip_csii:
        sections.append(run_csii_section(ip, info, min(duration, 3.0)))

    total = sum(len(sec.results) for sec in sections)
    failed = [f"[{sec.tag}] {name} ({detail})"
              for sec in sections for ok, name, detail in sec.results if not ok]
    print(f"\n--- {ip}: {total - len(failed)}/{total} checks passed ---")
    for f in failed:
        print(f"  FAILED: {f}")
    return not failed


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ips", nargs="*", help="sensor IPs (default: discover)")
    ap.add_argument("--bind", default="", help="local IP for discovery broadcast")
    ap.add_argument("--duration", type=float, default=8.0,
                    help="stream capture seconds per sensor (default 8)")
    ap.add_argument("--skip-csii", action="store_true",
                    help="skip CSII vehicle-input send test")
    args = ap.parse_args()

    print(f"Discovering sensors{' via ' + args.bind if args.bind else ''}...")
    found = discover(timeout_s=3.0, bind_ip=args.bind)
    print(f"  {len(found)} sensor(s) discovered")

    if args.ips:
        targets = []
        by_ip = {f.get("ip_address"): f for f in found}
        for ip in args.ips:
            if ip in by_ip:
                targets.append(by_ip[ip])
            else:
                print(f"  note: {ip} not in discovery results — probing directly")
                targets.append({"ip_address": ip})
    else:
        targets = found

    if not targets:
        print("No sensors to verify.")
        return 1

    results = {}
    for info in targets:
        ip = info.get("ip_address")
        results[ip] = verify_sensor(info, args.duration, args.skip_csii)

    print(f"\n{'=' * 64}\n Summary ({len(results)} sensor(s))\n{'=' * 64}")
    for ip, ok in results.items():
        print(f"  {ip}: {'PASS' if ok else 'FAIL'}")
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
