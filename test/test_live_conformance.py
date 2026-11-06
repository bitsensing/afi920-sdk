"""
Live interface-conformance verification against real AFI920 sensors
(interface v1.1.0).

Per sensor:
  [D] Discovery     — required response fields, value sanity
  [C] Config API    — GET methods only, response E2E (CRC-32, Data ID =
                      0x6000 << 16 | Method ID)
  [S] Data streams  — RDI / SHII / SPI over the transport the radar reports
                      (TCP: host connects; UDP: host binds the stream port),
                      SOME/IP header, E2E (20B, CRC-32 over the body, zero
                      CRC never passes), ~100 ms cycle, E2E Counter and
                      MessageCounter continuity, DataQualifier 0/1/6,
                      shared E2E Counter per send slot across the 3 streams
  [V] CSII input    — UDP 30501 vehicle input, SHII input signal status

The script sends no SET, Restart, firmware, or access mode request. A failed E2E check is counted and reported; frames are kept.

Usage:
    python test/test_live_conformance.py 192.168.10.150 --duration 30
    python test/test_live_conformance.py --bind 192.168.10.100     # discover

Exit code 0 = all hard checks passed on every sensor.
"""

import argparse
import math
import socket
import struct
import sys
import threading
import time
from collections import Counter
from pathlib import Path

# Korean Windows consoles default to CP949 — force UTF-8 for report glyphs
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))

from afi_sdk.someip import (  # noqa: E402
    ConfigClient, discover,
    SOMEIP_HEADER_FMT, SOMEIP_HEADER_SIZE, SERVICE_ID, PROTO_VER, IFACE_VER,
    MSG_TYPE_NOTIFICATION, MSG_TYPE_TP,
    EVENT_RDI, EVENT_SHII, EVENT_SPI,
    RANGE_MODE_NAMES,
    METHOD_GET_DEVICE_INFO, METHOD_GET_SENSOR_STATUS,
    METHOD_GET_DIAGNOSTIC_INFO, METHOD_GET_COMMAND_HISTORY,
    METHOD_GET_NETWORK_CONFIG, METHOD_GET_RANGE_MODE,
    METHOD_GET_DETECTION_THRESHOLD, METHOD_GET_DETECTION_FILTERS,
    METHOD_GET_PTP_CONFIG, METHOD_GET_MOUNTING_POSITION, METHOD_GET_ROTATION,
    METHOD_GET_SENSOR_POSITION, METHOD_GET_BOOT_INFO, METHOD_GET_CONFIG_STATUS,
    METHOD_GET_MODE,
)
from afi_sdk import streams  # noqa: E402

PI = math.pi
EXPECTED_IFACE_VERSION = (1, 1, 0)
VALID_DQ = {0, 1, 6}


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


def local_ipv4_addresses(peer: str = ""):
    """Host IPv4 addresses: the hostname's, plus the source address the
    routing table picks toward ``peer``. The hostname alone can resolve to
    127.0.1.1 only (Debian/WSL /etc/hosts)."""
    addrs = set()
    try:
        for res in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addrs.add(res[4][0])
    except OSError:
        pass
    if peer:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect((peer, 9))    # no packet is sent for a UDP connect
                addrs.add(s.getsockname()[0])
        except OSError:
            pass
    return addrs


# ─── [D] Discovery ───

DISCOVERY_REQUIRED_FIELDS = [
    "serial_number", "mac_address", "model_name",
    "hw_version", "software_version", "bootloader_hash", "fw_hash",
    "ip_address", "subnet_mask", "gateway",
    "api_port", "detection_port", "health_port", "performance_port",
    "detection_protocol", "health_protocol", "performance_protocol",
    "position_code", "offset_x_m", "offset_y_m", "offset_z_m",
    "yaw_deg", "pitch_deg", "roll_deg",
    "range_mode", "range_mode_name",
]


def run_discovery_section(ip: str, info: dict) -> Section:
    s = Section("D", "Discovery")
    s.check(bool(info.get("serial_number")), "Discovery response received",
            f"source={info.get('_source_ip')}")
    missing = [f for f in DISCOVERY_REQUIRED_FIELDS if f not in info]
    s.check(not missing, "required fields present",
            f"missing: {missing}" if missing else f"{len(DISCOVERY_REQUIRED_FIELDS)} fields")
    s.check(info.get("ip_address") == ip, "ip_address matches target",
            f"{info.get('ip_address')} vs {ip}")

    rm = info.get("range_mode", -1)
    s.check(rm in RANGE_MODE_NAMES, "range_mode in registry (0..6)", f"value={rm}")

    for proto_key in ("detection_protocol", "health_protocol", "performance_protocol"):
        v = info.get(proto_key, "")
        s.check(v in ("tcp", "udp"), f"{proto_key} valid", v)

    ss = info.get("scan_status")
    s.check(isinstance(ss, dict) and "scan_index" in ss and "uptime_seconds" in ss,
            "scan_status object (scan_index, uptime_seconds)", str(ss))
    s.info(f"serial={info.get('serial_number')} sw={info.get('software_version')} "
           f"bl/fw hash={info.get('bootloader_hash')}/{info.get('fw_hash')} "
           f"ptp={info.get('ptp_state')} radar_active={info.get('radar_active')} "
           f"dst(det/hea/per)={info.get('detection_ip')}/{info.get('health_ip')}"
           f"/{info.get('performance_ip')}")
    return s


# ─── [C] Config API ───

DETECTION_FILTERS_V110 = [
    "filter_ego_specular", "filter_ground_reflection", "filter_ground_sidelobe",
    "filter_multibounce_2x", "filter_specular_mirror",
    "filter_structure_multipath", "filter_upper_structure",
    "filter_wheel_microdoppler",
]


def run_config_section(ip: str, ctx: dict) -> Section:
    s = Section("C", "Config API")
    client = ConfigClient(ip)
    if not client.connect():
        s.check(False, "TCP 30500 connect", "connection failed")
        return s
    s.check(True, "TCP 30500 connect")

    e2e_results = []

    def get(mid, name, required=True):
        rc, data = client.request(mid)
        ok = (rc == 0 and isinstance(data, dict))
        if client.last_response_e2e is not None:
            e2e_results.append((name, client.last_response_e2e))
        if required:
            s.check(ok, f"{name} (0x{mid:04X}) returns data", f"rc={rc}")
        return data if ok else None

    dev = get(METHOD_GET_DEVICE_INFO, "GetDeviceInfo")
    if dev:
        keys = ["serial_number", "mac_address", "model_name", "hw_version",
                "software_version", "bootloader_hash", "fw_hash"]
        missing = [k for k in keys if k not in dev]
        s.check(not missing, "GetDeviceInfo v1.1.0 keys",
                f"missing: {missing}" if missing else f"sw={dev.get('software_version')}")

    status = get(METHOD_GET_SENSOR_STATUS, "GetSensorStatus")
    if status:
        code = status.get("operation_state_code", -1)
        s.check(0 <= code <= 3, "operation_state_code in 0..3", f"{code}")

    get(METHOD_GET_DIAGNOSTIC_INFO, "GetDiagnosticInfo")
    get(METHOD_GET_COMMAND_HISTORY, "GetCommandHistoryInfo")

    net = get(METHOD_GET_NETWORK_CONFIG, "GetNetworkConfig")
    if net:
        ctx["net"] = net
        stream_keys = [f"{p}_{f}" for p in ("detection", "health", "performance")
                       for f in ("ip", "port", "protocol")]
        missing = [k for k in stream_keys if k not in net]
        s.check(not missing, "NetworkConfig 3-stream destinations (v1.1.0)",
                f"missing: {missing}" if missing else "9/9 keys")
        s.check(not any(k.startswith("objects_") for k in net),
                "NetworkConfig has no objects_* keys", "")
        s.info("destinations: " + ", ".join(
            f"{p}={net.get(p + '_protocol')}:{net.get(p + '_ip')}:{net.get(p + '_port')}"
            for p in ("detection", "health", "performance")))

    rm = get(METHOD_GET_RANGE_MODE, "GetRangeMode")
    if rm:
        s.check(rm.get("range_mode", -1) in RANGE_MODE_NAMES, "range_mode in 0..6",
                f"{rm.get('range_mode')} ({rm.get('range_mode_name')})")

    th = get(METHOD_GET_DETECTION_THRESHOLD, "GetDetectionThreshold")
    if th:
        cf = th.get("cfar_table_first_frame")
        s.check(isinstance(cf, list) and len(cf) == 19,
                "cfar_table_first_frame has 19 levels", str(cf))

    filt = get(METHOD_GET_DETECTION_FILTERS, "GetDetectionFilters")
    if filt:
        missing = [k for k in DETECTION_FILTERS_V110 if k not in filt]
        s.check(not missing, "8 detection filters (v1.1.0)",
                f"missing: {missing}" if missing else "")

    ptp = get(METHOD_GET_PTP_CONFIG, "GetPTPConfig")
    if ptp:
        s.info(f"PTP profile={ptp.get('profile')} state={ptp.get('state')} "
               f"offset={ptp.get('offset_ns')}ns grid={ptp.get('grid_state')}")

    get(METHOD_GET_MOUNTING_POSITION, "GetMountingPosition")
    get(METHOD_GET_ROTATION, "GetRotation")
    get(METHOD_GET_SENSOR_POSITION, "GetSensorPosition")
    get(METHOD_GET_BOOT_INFO, "GetBootInfo")
    get(METHOD_GET_CONFIG_STATUS, "GetConfigStatus")

    mode = get(METHOD_GET_MODE, "GetMode")
    if mode:
        ctx["access_mode"] = mode
        s.check(mode.get("streams_halted") == 0, "GetMode streams_halted == 0",
                f"mode={mode.get('access_mode_name')} halted={mode.get('streams_halted')}")

    # Reserved IDs on v1.1.0 (no side effect: the radar answers NOT_SUPPORTED)
    reserved = [(0x0044, "GetDetectionDensity (reserved)")]
    for rid, name in reserved:
        rc, _ = client.request(rid)
        s.check(rc != 0, f"0x{rid:04X} {name} rejected", f"rc={rc}")

    bad = [n for n, e in e2e_results if not e["ok"]]
    s.check(e2e_results and not bad, "response E2E (CRC-32, Data ID, Length) valid",
            f"{len(e2e_results) - len(bad)}/{len(e2e_results)} ok"
            + (f", failed: {bad}" if bad else ""))
    counters = [e["counter"] for _, e in e2e_results]
    mono = all((b - a) & 0xFFFFFFFF == 1 for a, b in zip(counters, counters[1:]))
    s.check(mono, "response E2E Counter +1 per response (System Spec 7.3)",
            f"{counters[:3]}..{counters[-3:]}")
    client.close()
    return s


# ─── [S] Data streams ───

E2E_DATA_IDS = {EVENT_RDI: streams.E2E_DATA_ID_RDI,
                EVENT_SHII: streams.E2E_DATA_ID_SHII,
                EVENT_SPI: streams.E2E_DATA_ID_SPI}
IFACE_IDS = {EVENT_RDI: 0x08, EVENT_SHII: 0x0D, EVENT_SPI: 0x0C}
MAX_FRAME = SOMEIP_HEADER_SIZE + streams.E2E_HEADER_SIZE + streams.RDI_MAX_PAYLOAD_SIZE


class StreamStats:
    def __init__(self, name):
        self.name = name
        self.transport = ""
        self.error = ""
        self.frames = 0
        self.someip_bad = 0
        self.session_gaps = 0
        self.e2e_ok = 0
        self.e2e_crc_bad = 0
        self.e2e_crc_zero = 0
        self.e2e_id_bad = 0
        self.e2e_len_bad = 0
        self.e2e_counter_gaps = 0
        self.e2e_counter_nonmono = 0
        self.msg_counter_decrease = 0
        self.dq = Counter()
        self.iface_bad = 0
        self.parse_fail = 0
        self.recv_t0 = None
        self.recv_t1 = None
        self.arrivals = []
        self.e2e_counters = []
        self.last_session = None
        self.last_e2e_counter = None
        self.last_dq0_mc = None
        self.detections_total = 0
        self.sample = None
        self.shi_msgs = []


def _recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf


def _iso_common(iso):
    if len(iso) < 24:
        return None
    return {"version": tuple(iso[0:3]), "interface_id": iso[3],
            "timestamp": struct.unpack_from("<Q", iso, 6)[0],
            "message_counter": struct.unpack_from("<I", iso, 14)[0],
            "dq": iso[23]}


def process_stream_message(data: bytes, event_id: int, st: StreamStats):
    h = struct.unpack(SOMEIP_HEADER_FMT, data[:SOMEIP_HEADER_SIZE])
    svc, mid, length, _, session, proto, iface, mtype, ret = h
    if (svc != SERVICE_ID or mid != event_id or proto != PROTO_VER
            or iface != IFACE_VER or (mtype & ~MSG_TYPE_TP) != MSG_TYPE_NOTIFICATION
            or ret != 0 or length + 8 != len(data)):
        st.someip_bad += 1
        return
    if st.last_session is not None and ((session - st.last_session) & 0xFFFF) != 1:
        st.session_gaps += 1
    st.last_session = session

    e2e, iso = streams.strip_e2e(data[SOMEIP_HEADER_SIZE:],
                                 expected_data_id=E2E_DATA_IDS[event_id])
    if e2e is None:
        st.someip_bad += 1
        return
    st.frames += 1
    st.arrivals.append(time.monotonic())
    st.e2e_ok += int(e2e["ok"])
    st.e2e_crc_bad += int(not e2e["crc_ok"] and not e2e["crc_zero"])
    st.e2e_crc_zero += int(e2e["crc_zero"])
    st.e2e_id_bad += int(not e2e["data_id_ok"])
    st.e2e_len_bad += int(not e2e["length_ok"])
    c = e2e["counter"]
    st.e2e_counters.append(c)
    if st.last_e2e_counter is not None:
        delta = (c - st.last_e2e_counter) & 0xFFFFFFFF
        if delta == 0 or delta > 0x80000000:
            st.e2e_counter_nonmono += 1
        elif delta > 1:
            st.e2e_counter_gaps += delta - 1
    st.last_e2e_counter = c

    common = _iso_common(iso)
    if common is None:
        st.parse_fail += 1
        return
    if common["version"] != EXPECTED_IFACE_VERSION or common["interface_id"] != IFACE_IDS[event_id]:
        st.iface_bad += 1
    st.dq[common["dq"]] += 1
    if common["dq"] == 0:
        mc = common["message_counter"]
        if st.last_dq0_mc is not None and mc < st.last_dq0_mc:
            st.msg_counter_decrease += 1
        st.last_dq0_mc = mc

    if event_id == EVENT_RDI:
        hdr = streams.parse_rdi_frame(iso)
        if hdr is None:
            st.parse_fail += 1
            return
        st.detections_total += hdr["num_detections"]
        if st.sample is None and hdr["num_detections"] > 0:
            d = streams.parse_rdi_detection(iso, hdr["det_start"], 0)
            if d:
                st.sample = (f"det[0] r={d['radial_distance']:.2f}m az={d['azimuth']:.3f} "
                             f"v={d['radial_velocity']:.2f} snr={d['snr']:.1f} "
                             f"cap={hdr['detections_capability']}")
    elif event_id == EVENT_SHII:
        msg = streams.parse_shi(iso)
        if msg is None:
            st.parse_fail += 1
            return
        st.shi_msgs.append((time.monotonic(), msg))
        if st.sample is None:
            st.sample = (f"modes={msg['operation_modes']} sync={msg['time_sync']} "
                         f"inputs={list(zip(msg['input_signal_types'], msg['input_signal_statuses']))}")
    elif event_id == EVENT_SPI:
        if streams.parse_spi(iso) is None:
            st.parse_fail += 1


def receive_stream(ip, port, event_id, transport, duration, st: StreamStats,
                   start_evt: threading.Event = None):
    st.transport = transport
    try:
        if transport == "tcp":
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
            sock.settimeout(3.0)
            sock.connect((ip, port))
        else:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 16 * 1024 * 1024)
            sock.bind(("0.0.0.0", port))
            sock.settimeout(0.5)
    except OSError as exc:
        st.error = f"{transport} open {port}: {exc}"
        return st
    if start_evt is not None:
        start_evt.wait()

    tp = streams.SomeipTpReassembler(MAX_FRAME, max_sessions=2)
    st.recv_t0 = time.monotonic()
    deadline = st.recv_t0 + duration
    while time.monotonic() < deadline:
        try:
            if transport == "tcp":
                pre = _recv_exact(sock, 8)
                if len(pre) < 8:
                    st.error = "TCP closed by peer"
                    break
                length = struct.unpack(">I", pre[4:8])[0]
                if length < 8 or length + 8 > MAX_FRAME:
                    st.someip_bad += 1
                    break
                rest = _recv_exact(sock, length)
                if len(rest) < length:
                    break
                process_stream_message(pre + rest, event_id, st)
            else:
                data, src = sock.recvfrom(65536)
                if src[0] != ip or len(data) < SOMEIP_HEADER_SIZE:
                    continue
                mtype = data[14]
                if mtype & MSG_TYPE_TP:
                    session = struct.unpack_from(">H", data, 10)[0]
                    full = tp.feed(data[16:20], data[20:], session)
                    if not full:
                        continue
                    hdr = bytearray(data[:16])
                    struct.pack_into(">I", hdr, 4, 8 + len(full))
                    hdr[14] = MSG_TYPE_NOTIFICATION
                    process_stream_message(bytes(hdr) + full, event_id, st)
                else:
                    process_stream_message(data, event_id, st)
        except socket.timeout:
            continue
        except OSError as exc:
            st.error = str(exc)
            break
    st.recv_t1 = time.monotonic()
    sock.close()
    return st


def stream_plan(info: dict, ctx: dict):
    net = ctx.get("net") or {}
    plan = []
    for name, prefix, eid, dport in (("RDI", "detection", EVENT_RDI, 30509),
                                     ("SHII", "health", EVENT_SHII, 30510),
                                     ("SPI", "performance", EVENT_SPI, 30511)):
        proto = net.get(f"{prefix}_protocol") or info.get(f"{prefix}_protocol") or "tcp"
        port = net.get(f"{prefix}_port") or info.get(f"{prefix}_port") or dport
        dst = net.get(f"{prefix}_ip") or info.get(f"{prefix}_ip") or ""
        plan.append((name, prefix, eid, int(port), proto, dst))
    return plan


def run_stream_section(ip: str, info: dict, ctx: dict, duration: float) -> Section:
    s = Section("S", "Data Streams")
    plan = stream_plan(info, ctx)
    local = local_ipv4_addresses(ip)
    for name, _, _, port, proto, dst in plan:
        if proto == "udp":
            s.check(dst in local or dst in ("0.0.0.0", ""),
                    f"{name} UDP destination {dst} is a host address",
                    f"host IPv4: {sorted(local)}")

    stats = {name: StreamStats(name) for name, *_ in plan}
    start = threading.Event()
    threads = []
    for name, _, eid, port, proto, _ in plan:
        t = threading.Thread(target=receive_stream, daemon=True,
                             args=(ip, port, eid, proto, duration, stats[name], start))
        t.start()
        threads.append(t)
    time.sleep(0.3)
    start.set()
    for t in threads:
        t.join(timeout=duration + 10)

    for name, _, eid, port, proto, _ in plan:
        st = stats[name]
        label = f"{name} ({proto.upper()} {port})"
        if st.error and st.frames == 0:
            s.check(False, f"{label} frames received", st.error)
            continue
        elapsed = (st.recv_t1 - st.recv_t0) if st.recv_t1 else duration
        rate = st.frames / elapsed if elapsed > 0 else 0
        s.check(st.frames >= int(duration * 8), f"{label} frames received",
                f"{st.frames} in {elapsed:.1f}s ({rate:.2f} Hz)")
        if st.frames == 0:
            continue
        s.check(st.someip_bad == 0, f"{name} SOME/IP header conformance",
                f"{st.someip_bad} bad, session gaps={st.session_gaps}")
        pct = 100.0 * st.e2e_ok / st.frames
        s.check(st.e2e_ok == st.frames, f"{name} E2E pass rate",
                f"{st.e2e_ok}/{st.frames} ({pct:.2f}%) crc_bad={st.e2e_crc_bad} "
                f"crc_zero={st.e2e_crc_zero} id_bad={st.e2e_id_bad} len_bad={st.e2e_len_bad}")
        s.check(st.e2e_counter_nonmono == 0, f"{name} E2E Counter no repeat/decrease",
                f"{st.e2e_counter_nonmono} non-monotonic, {st.e2e_counter_gaps} skipped slots")
        s.check(st.msg_counter_decrease == 0, f"{name} DQ0 MessageCounter non-decreasing",
                f"{st.msg_counter_decrease} decreases")
        s.check(set(st.dq) <= VALID_DQ, f"{name} DataQualifier in {{0,1,6}}",
                f"{dict(st.dq)}")
        s.check(st.iface_bad == 0, f"{name} InterfaceVersion 1.1.0 / InterfaceID",
                f"{st.iface_bad} bad")
        s.check(st.parse_fail == 0, f"{name} ISO payload parse", f"{st.parse_fail} failed")
        iv = [b - a for a, b in zip(st.arrivals, st.arrivals[1:])]
        if iv:
            mean = sum(iv) / len(iv) * 1000
            s.check(90.0 <= mean <= 110.0, f"{name} cycle ≈ 100 ms",
                    f"mean={mean:.1f}ms min={min(iv)*1000:.1f} max={max(iv)*1000:.1f}")
        if st.sample:
            s.info(f"{name} {st.sample}")
        if name == "RDI":
            s.info(f"RDI detections avg={st.detections_total / st.frames:.1f}/frame")

    # RDI, SHII, SPI of one send slot carry the same E2E Counter (System Spec 8.2).
    sets = [set(stats[n].e2e_counters) for n in ("RDI", "SHII", "SPI") if stats[n].frames]
    if len(sets) == 3:
        common = sets[0] & sets[1] & sets[2]
        union = sets[0] | sets[1] | sets[2]
        s.check(len(common) >= 0.9 * len(union) - 3, "RDI/SHII/SPI share E2E Counter per slot",
                f"common={len(common)} union={len(union)}")
    ctx["shii_stats"] = stats.get("SHII")
    return s


# ─── [V] CSII vehicle input ───

def run_csii_section(ip: str, info: dict, ctx: dict, duration: float) -> Section:
    s = Section("V", "CSII Vehicle Input")
    plan = {p[0]: p for p in stream_plan(info, ctx)}
    _, _, eid, port, proto, _ = plan["SHII"]

    pre = StreamStats("SHII")
    receive_stream(ip, port, eid, proto, 2.0, pre)

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect((ip, 30501))
    except OSError as exc:
        s.check(False, "UDP 30501 socket", str(exc))
        return s

    post = StreamStats("SHII")
    rx = threading.Thread(target=receive_stream, daemon=True,
                          args=(ip, port, eid, proto, duration, post))
    rx.start()
    sent = 0
    t_first = time.monotonic()
    n_frames = max(int(duration / 0.1), 10)
    for i in range(n_frames):
        payload = streams.build_csii_payload(
            somc=streams.SOMC_NORMAL_MODE, message_counter=i,
            velocity_mps=(5.0, 0.0, 0.0), gear_position=streams.GEAR_FORWARD,
            global_timestamp_utc=time.time())
        frame = streams.build_csii_frame(payload, counter=i, session_id=i + 1)
        try:
            sock.send(frame)
            sent += 1
        except OSError:
            pass
        time.sleep(0.1)
    sock.close()
    rx.join(timeout=duration + 5)
    s.check(sent == n_frames, "CSII datagrams sent (UDP 30501, 115B, E2E CRC-32)",
            f"{sent}/{n_frames}")

    def statuses(msgs, after=None):
        vals = [m["input_signal_statuses"] for t, m in msgs if after is None or t >= after]
        return vals[-1] if vals else None

    before = statuses(pre.shi_msgs)
    during = statuses(post.shi_msgs, after=t_first + 0.5)
    s.info(f"SHII input signal statuses before CSII: {before} "
           f"(0=Valid 2=Missing 4=Timeout)")
    s.info(f"SHII input signal statuses during CSII: {during}")
    s.check(during is not None and len(during) == 4 and all(v == 0 for v in during),
            "SHII input signal status Valid(0) x4 while CSII is sent", str(during))
    return s


# ─── main ───

def verify_sensor(info: dict, duration: float, skip_csii: bool) -> bool:
    ip = info.get("ip_address") or info.get("_source_ip")
    print(f"\n{'=' * 64}\n Sensor {info.get('serial_number', '?')} @ {ip} "
          f"(sw {info.get('software_version', '?')})\n{'=' * 64}")
    ctx = {}
    sections = [run_discovery_section(ip, info), run_config_section(ip, ctx)]
    sections.append(run_stream_section(ip, info, ctx, duration))
    if not skip_csii:
        sections.append(run_csii_section(ip, info, ctx, min(duration, 4.0)))

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
    ap.add_argument("--duration", type=float, default=10.0,
                    help="stream capture seconds per sensor (default 10)")
    ap.add_argument("--skip-csii", action="store_true", help="skip the CSII section")
    args = ap.parse_args()

    print(f"Discovering sensors{' via ' + args.bind if args.bind else ''}...")
    found = discover(timeout_s=3.0, bind_ip=args.bind)
    print(f"  {len(found)} sensor(s) discovered")

    if args.ips:
        targets = []
        by_ip = {f.get("ip_address"): f for f in found}
        for ip in args.ips:
            # A response can be missed; ask the sensor directly before
            # treating it as absent.
            for _ in range(3):
                if ip in by_ip:
                    break
                for f in discover(timeout_s=1.5, broadcast_ip=ip):
                    by_ip.setdefault(f.get("ip_address"), f)
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
