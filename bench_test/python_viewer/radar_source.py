"""
Data sources and shared state for the AFI920 3D viewer.

This module is GUI-free (numpy + stdlib only) so it can be unit-tested
headless. It provides:

  FrameStore       thread-safe latest-state per sensor (point clouds, health,
                   performance, poses) that the GUI polls on a timer.
  process_payload  parse one ISO 23150 payload and push it into the store.
  CallbackSink     adapter so the logger's hardened StreamWorker can deliver
                   frames into the store (and the active recorder).
  LiveSource       reads each sensor's mounting pose via the Config API and
                   receives the live RDI/SHII/SPI streams.
  ReplaySource     replays a .btsrec recording into the store with
                   play/pause/speed control.
  SimSource        synthetic generator (no hardware) for demo and testing.

Copyright (c) 2026 bitsensing Inc.
"""

import math
import struct
import sys
import threading
import time
from collections import deque
from pathlib import Path
from typing import Dict, List, Optional

import numpy as np

# ── Make sibling packages importable: afi_sdk from the SDK's python/src (at the
#    repo root); the logger receive layer and afi_jsonl from sibling tools under
#    bench_test/ (logger/ and python_recording_920_cli/) ──
_HERE = Path(__file__).resolve().parent


def _find_repo_root(start: Path) -> Path:
    """Walk up until the SDK tree (python/src/afi_sdk) is found."""
    for base in (start, *start.parents):
        if (base / "python" / "src" / "afi_sdk").is_dir():
            return base
    return start.parent.parent


_ROOT = _find_repo_root(_HERE)
for _p in (_ROOT / "python" / "src", _HERE.parent / "logger",
           _HERE.parent / "python_recording_920_cli"):
    if _p.is_dir() and str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

from afi_sdk import AfiSdk, AfiSdkConfig                       # noqa: E402
from afi_sdk.streams import (                                  # noqa: E402
    strip_e2e, parse_rdi_frame, parse_shi, parse_spi,
    build_e2e_header, RDI_HEADER_SIZE, RDI_DETECTION_SIZE,
    E2E_DATA_ID_RDI, E2E_DATA_ID_SHII, E2E_DATA_ID_SPI,
)
# Reuse the logger's proven, reviewed receive layer.
from radar_logger import (                                     # noqa: E402
    StreamWorker, StreamStats, STREAM_SPECS, configure_sensor, udp_port_for,
)

from geometry import SensorPose, spherical_to_cartesian        # noqa: E402


# ── Vectorised RDI detection layout (51 B, little-endian, packed) ──

RDI_DET_DTYPE = np.dtype([
    ("existence_probability", "u1"),
    ("detection_id", "<u2"),
    ("object_id_reference", "<u2"),
    ("timestamp_difference", "<f4"),
    ("rcs", "<f4"),
    ("snr", "<f4"),
    ("ambiguity_grouping_id", "<u2"),
    ("radial_distance", "<f4"),
    ("azimuth", "<f4"),
    ("elevation", "<f4"),
    ("radial_distance_error", "<f4"),
    ("azimuth_error", "<f4"),
    ("elevation_error", "<f4"),
    ("radial_velocity", "<f4"),
    ("radial_velocity_error", "<f4"),
])
assert RDI_DET_DTYPE.itemsize == RDI_DETECTION_SIZE, (
    f"RDI dtype itemsize {RDI_DET_DTYPE.itemsize} != {RDI_DETECTION_SIZE}")


def pack_rdi(payload: bytes, pose: SensorPose) -> Optional[dict]:
    """Parse an RDI ISO payload into world-frame points and attributes."""
    hdr = parse_rdi_frame(payload)
    if not hdr:
        return None
    det_start = hdr["det_start"]
    claimed = hdr["num_detections"]
    available = max(0, (len(payload) - det_start) // RDI_DETECTION_SIZE)
    n = min(claimed, available)

    if n > 0:
        arr = np.frombuffer(payload, dtype=RDI_DET_DTYPE, count=n,
                            offset=det_start)
        pts_sensor = spherical_to_cartesian(
            arr["radial_distance"], arr["azimuth"], arr["elevation"])
        points = pose.transform(pts_sensor).astype(np.float32)
        velocity = arr["radial_velocity"].astype(np.float32)
        rcs = arr["rcs"].astype(np.float32)
        snr = arr["snr"].astype(np.float32)
    else:
        points = np.zeros((0, 3), np.float32)
        velocity = np.zeros((0,), np.float32)
        rcs = np.zeros((0,), np.float32)
        snr = np.zeros((0,), np.float32)

    return {
        "points": points, "velocity": velocity, "rcs": rcs, "snr": snr,
        "counter": hdr["message_counter"], "ts_ns": hdr["timestamp"],
        "n": n, "claimed": claimed,
    }


# ── Shared per-sensor state ──

class SensorState:
    def __init__(self, index: int):
        self.index = index
        self.ip = ""
        self.pose = SensorPose()
        self.device_info: dict = {}
        # latest RDI
        self.points = np.zeros((0, 3), np.float32)
        self.velocity = np.zeros((0,), np.float32)
        self.rcs = np.zeros((0,), np.float32)
        self.snr = np.zeros((0,), np.float32)
        self.rdi_counter = -1
        self.rdi_ts_ns = 0
        self.rdi_n = 0
        self.rdi_claimed = 0
        self.rdi_frames = 0
        self.truncated = False
        # latest health / perf
        self.health: Optional[dict] = None
        self.perf: Optional[dict] = None
        self.health_frames = 0
        self.perf_frames = 0
        # connection / rate
        self.connected = False
        self._fps_times = deque(maxlen=20)
        self.fps = 0.0


class FrameStore:
    """Thread-safe latest-state container polled by the GUI timer."""

    def __init__(self, n_sensors: int):
        self.n = n_sensors
        self._lock = threading.Lock()
        self.states = [SensorState(i) for i in range(n_sensors)]

    def _in_range(self, idx: int) -> bool:
        # Recordings carry an 8-bit sensor index; a corrupt/sparse/mismatched
        # recording can hand us idx >= n. Skip rather than crash a worker
        # thread or the GUI with IndexError.
        return 0 <= idx < self.n

    def set_pose(self, idx: int, pose: SensorPose, ip: str,
                 info: Optional[dict] = None) -> None:
        if not self._in_range(idx):
            return
        with self._lock:
            s = self.states[idx]
            s.pose = pose
            s.ip = ip
            if info:
                s.device_info = info

    def set_connected(self, idx: int, value: bool) -> None:
        if not self._in_range(idx):
            return
        with self._lock:
            self.states[idx].connected = value

    def set_rdi(self, idx: int, points, velocity, rcs, snr, counter, ts_ns,
                n, claimed) -> None:
        if not self._in_range(idx):
            return
        now = time.monotonic()
        with self._lock:
            s = self.states[idx]
            s.points = points
            s.velocity = velocity
            s.rcs = rcs
            s.snr = snr
            s.rdi_counter = counter
            s.rdi_ts_ns = ts_ns
            s.rdi_n = n
            s.rdi_claimed = claimed
            s.truncated = n < claimed
            s.rdi_frames += 1
            s._fps_times.append(now)
            if len(s._fps_times) >= 2:
                span = s._fps_times[-1] - s._fps_times[0]
                s.fps = (len(s._fps_times) - 1) / span if span > 0 else 0.0

    def set_health(self, idx: int, msg: dict) -> None:
        if not self._in_range(idx):
            return
        with self._lock:
            s = self.states[idx]
            s.health = msg
            s.health_frames += 1

    def set_perf(self, idx: int, msg: dict) -> None:
        if not self._in_range(idx):
            return
        with self._lock:
            s = self.states[idx]
            s.perf = msg
            s.perf_frames += 1

    def snapshot(self) -> List[dict]:
        """Return a per-sensor list of the current state (reference copy).

        Arrays are swapped wholesale (never mutated in place), so the GUI may
        hold the returned references safely until the next snapshot.
        """
        with self._lock:
            out = []
            for s in self.states:
                out.append({
                    "index": s.index, "ip": s.ip, "pose": s.pose,
                    "device_info": s.device_info, "connected": s.connected,
                    "points": s.points, "velocity": s.velocity,
                    "rcs": s.rcs, "snr": s.snr,
                    "rdi_counter": s.rdi_counter, "rdi_ts_ns": s.rdi_ts_ns,
                    "rdi_n": s.rdi_n, "rdi_claimed": s.rdi_claimed,
                    "rdi_frames": s.rdi_frames, "truncated": s.truncated,
                    "health": s.health, "perf": s.perf,
                    "health_frames": s.health_frames,
                    "perf_frames": s.perf_frames, "fps": s.fps,
                })
            return out


# ── Parse one payload into the store ──

def process_payload(stream: str, payload: bytes, pose: SensorPose,
                    store: FrameStore, idx: int,
                    stats: Optional[StreamStats] = None) -> None:
    if stream == "rdi":
        packed = pack_rdi(payload, pose)
        if packed is None:
            if stats:
                stats.parse_errors += 1
            return
        store.set_rdi(idx, packed["points"], packed["velocity"],
                      packed["rcs"], packed["snr"], packed["counter"],
                      packed["ts_ns"], packed["n"], packed["claimed"])
        if stats:
            stats.frames += 1
            stats.detections += packed["n"]
    elif stream == "shii":
        msg = parse_shi(payload)
        if msg is None:
            if stats:
                stats.parse_errors += 1
            return
        store.set_health(idx, msg)
        if stats:
            stats.frames += 1
    elif stream == "spi":
        msg = parse_spi(payload)
        if msg is None:
            if stats:
                stats.parse_errors += 1
            return
        store.set_perf(idx, msg)
        if stats:
            stats.frames += 1


def _ingest(stream: str, idx: int, payload: bytes, pose: SensorPose,
            store: FrameStore, stats: Optional[StreamStats],
            rec_ctrl: Optional["RecordController"],
            host_ts: Optional[float] = None) -> None:
    """Record (if active) then push a payload into the store."""
    if host_ts is None:
        host_ts = time.time()
    if rec_ctrl is not None:
        rec = rec_ctrl.recorder
        if rec is not None:
            rec.write(host_ts, stream, idx, payload)
    process_payload(stream, payload, pose, store, idx, stats)


# ── Recording control (shared by every sink) ──

class RecordController:
    def __init__(self):
        self._lock = threading.Lock()
        self.recorder = None  # plain attr read on the hot path (atomic ref)

    def start(self, path, meta) -> "object":
        from afi_jsonl import JsonlRecorder
        with self._lock:
            if self.recorder is not None:
                self.recorder.close()
            self.recorder = JsonlRecorder(path, meta)
            return self.recorder

    def stop(self):
        with self._lock:
            r = self.recorder
            self.recorder = None
        if r is not None:
            r.close()
        return r

    @property
    def active(self) -> bool:
        return self.recorder is not None


# ── Sink adapter: lets StreamWorker deliver into the store ──

class CallbackSink:
    """Mimics the logger sink interface (`deliver` + `stats`) for the viewer."""

    def __init__(self, idx: int, ip: str, stream: str, store: FrameStore,
                 pose: SensorPose, rec_ctrl: Optional[RecordController],
                 stats: StreamStats):
        self.idx = idx
        self.ip = ip
        self.stream = stream
        self.store = store
        self.pose = pose
        self.rec_ctrl = rec_ctrl
        self.stats = stats

    def deliver(self, frame: bytes) -> None:
        self.stats.bytes_recv += len(frame)
        e2e, payload = strip_e2e(frame)
        if e2e is None:
            self.stats.parse_errors += 1
            return
        if not e2e.get("crc_ok", True):
            self.stats.e2e_errors += 1
        # Reflect connection liveness onto the sensor (any stream counts).
        self.store.set_connected(self.idx, self.stats.connected)
        _ingest(self.stream, self.idx, payload, self.pose, self.store,
                self.stats, self.rec_ctrl)


def read_pose(ip: str) -> "tuple":
    """Read one sensor's mounting pose + device info via the Config API."""
    pose = SensorPose()
    info: dict = {}
    sdk = AfiSdk()
    try:
        if sdk.init(AfiSdkConfig(sensor_ip=ip)) == 0:
            mp = sdk.get_mounting_position() or {}
            rot = sdk.get_rotation() or {}
            info = sdk.get_device_info() or {}
            pose = SensorPose(
                offset=(mp.get("offset_x_m", 0.0), mp.get("offset_y_m", 0.0),
                        mp.get("offset_z_m", 0.0)),
                yaw_deg=rot.get("yaw_deg", 0.0),
                pitch_deg=rot.get("pitch_deg", 0.0),
                roll_deg=rot.get("roll_deg", 0.0))
    except Exception:
        pass
    finally:
        sdk.close()  # always release the Config API socket
    return pose, info


# ── Live source: pose read + stream receive ──

class LiveSource:
    def __init__(self, sensors: List[str], streams: List[str], transport: str,
                 host_ip: Optional[str], store: FrameStore,
                 rec_ctrl: RecordController, no_configure: bool = False,
                 on_log=None):
        self.sensors = sensors
        self.streams = streams
        self.transport = transport
        self.host_ip = host_ip
        self.store = store
        self.rec_ctrl = rec_ctrl
        self.no_configure = no_configure
        self.on_log = on_log or (lambda m: None)
        self.kind = "live"
        self._stop_event: Optional[threading.Event] = None
        self.workers: List[StreamWorker] = []
        self.poses: List[SensorPose] = []
        self.device_info: List[dict] = []

    def start(self) -> None:
        self._stop_event = threading.Event()
        for idx, ip in enumerate(self.sensors):
            if not self.no_configure:
                try:
                    configure_sensor(ip, self.streams, self.transport,
                                     self.host_ip, idx)
                except Exception as e:
                    self.on_log(f"[{ip}] configure skipped: {e}")
            pose, info = read_pose(ip)
            self.poses.append(pose)
            self.device_info.append(info)
            self.store.set_pose(idx, pose, ip, info)
            self.on_log(f"[{ip}] pose {pose}")

        for idx, ip in enumerate(self.sensors):
            for stream in self.streams:
                _eid, default_port, uses_tp, _key = STREAM_SPECS[stream]
                port = (default_port if self.transport == "tcp"
                        else udp_port_for(stream, idx))
                stats = StreamStats(sensor=ip, stream=stream)
                sink = CallbackSink(idx, ip, stream, self.store,
                                    self.poses[idx], self.rec_ctrl, stats)
                w = StreamWorker(ip, stream, port, uses_tp, self.transport,
                                 sink, self._stop_event)
                self.workers.append(w)
                w.start()

    def stop(self) -> None:
        # Called from the GUI thread. Signal the workers, then join them on a
        # throwaway daemon thread so a slow socket teardown can't freeze the
        # event loop (the workers are daemons; the process won't hang on them).
        if self._stop_event:
            self._stop_event.set()
        workers, self.workers = self.workers, []
        if workers:
            threading.Thread(target=lambda: [w.join(timeout=5.0)
                                             for w in workers],
                             daemon=True).start()

    def recording_meta(self) -> dict:
        return {
            "kind": "live", "transport": self.transport,
            "started_unix": time.time(),
            "sensors": [
                {"index": i, "ip": ip, "pose": self.poses[i].to_dict(),
                 "device_info": self.device_info[i]}
                for i, ip in enumerate(self.sensors)
            ],
        }


# ── Replay source ──

class ReplaySource(threading.Thread):
    """Replays an ``afi-radar-jsonl`` recording (parsed records) into the store
    with play / pause / speed control. Sensor-frame detections are re-projected
    to the world frame using the per-sensor pose from the recording header."""

    def __init__(self, reader, store: FrameStore, speed: float = 1.0,
                 on_log=None):
        super().__init__(name="replay", daemon=True)
        self.reader = reader
        self.store = store
        self.kind = "replay"
        self.on_log = on_log or (lambda m: None)
        self._stop_evt = threading.Event()
        self._paused = threading.Event()
        self._speed = max(0.05, speed)
        self._speed_lock = threading.Lock()
        self.progress = 0.0
        self.finished = False
        # Build poses indexed by sensor_index from the recording header.
        self.poses: Dict[int, SensorPose] = {}
        self.ips: Dict[int, str] = {}
        for s in (reader.meta or {}).get("sensors", []):
            i = s.get("index", 0)
            self.poses[i] = SensorPose.from_dict(s.get("pose"))
            self.ips[i] = s.get("ip", "")
            self.store.set_pose(i, self.poses[i], self.ips[i],
                                s.get("device_info"))

    def set_speed(self, speed: float) -> None:
        with self._speed_lock:
            self._speed = max(0.05, speed)

    def pause(self) -> None:
        self._paused.set()

    def resume(self) -> None:
        self._paused.clear()

    @property
    def paused(self) -> bool:
        return self._paused.is_set()

    def stop(self) -> None:
        self._stop_evt.set()
        self._paused.clear()
        # Join so a freshly-created store can't be written by the old thread
        # after a source switch (mirrors SimSource.stop()).
        if self.is_alive() and threading.current_thread() is not self:
            self.join(timeout=2.0)

    def run(self) -> None:
        records = list(self.reader.records())
        if not records:
            self.finished = True
            return
        t0 = records[0].get("ts", 0.0)
        total = len(records)
        play_clock = 0.0          # seconds of recording time elapsed
        last_wall = time.monotonic()
        i = 0
        while i < total and not self._stop_evt.is_set():
            now = time.monotonic()
            if not self._paused.is_set():
                with self._speed_lock:
                    speed = self._speed
                play_clock += (now - last_wall) * speed
            last_wall = now

            rec = records[i]
            ts = rec.get("ts", t0)
            if (ts - t0) <= play_clock:
                self._apply(rec)
                i += 1
                self.progress = i / total
            else:
                time.sleep(0.005)
        self.finished = True
        self.on_log(f"Replay {'stopped' if self._stop_evt.is_set() else 'finished'} "
                    f"({i}/{total} records)")

    def _apply(self, rec: dict) -> None:
        """Push one parsed JSONL record into the store."""
        t = rec.get("t")
        idx = rec.get("s", 0)
        if t == "rdi":
            pose = self.poses.get(idx, SensorPose())
            r = np.asarray(rec.get("r", ()), dtype=np.float64)
            az = np.asarray(rec.get("az", ()), dtype=np.float64)
            el = np.asarray(rec.get("el", ()), dtype=np.float64)
            pts_sensor = spherical_to_cartesian(r, az, el)
            points = pose.transform(pts_sensor).astype(np.float32)
            velocity = np.asarray(rec.get("v", ()), dtype=np.float32)
            rcs = np.asarray(rec.get("rcs", ()), dtype=np.float32)
            snr = np.asarray(rec.get("snr", ()), dtype=np.float32)
            n = int(rec.get("n", len(r)))
            # `claimed` (the sensor's header count) is preserved so a truncated
            # frame still flags as truncated on replay; older files default it.
            claimed = int(rec.get("claimed", n))
            self.store.set_rdi(idx, points, velocity, rcs, snr,
                               rec.get("counter", -1), rec.get("ts_ns", 0),
                               n, claimed)
        elif t == "shii":
            self.store.set_health(idx, {
                "defect_recognised": rec.get("defect", 0),
                "defect_reason": rec.get("reason", 0),
                "supply_voltage_status": rec.get("supply", 0),
                "temperature_status": rec.get("temp", 0),
                "time_sync": rec.get("time_sync", 0),
                "operation_modes": rec.get("modes", []),
                "calibration_statuses": rec.get("cal", []),
                "message_counter": rec.get("counter", 0),
            })
        elif t == "spi":
            pose = rec.get("pose", [0.0] * 8)
            pose = (list(pose) + [0.0] * 8)[:8]
            self.store.set_perf(idx, {
                "pose": {"origin_x": pose[0], "origin_y": pose[1],
                         "origin_z": pose[2], "yaw": pose[3],
                         "pitch": pose[4], "roll": pose[5]},
                "fov_segments": [
                    {"azimuth_begin": s.get("az0", 0.0),
                     "azimuth_end": s.get("az1", 0.0),
                     "elevation_begin": s.get("el0", 0.0),
                     "elevation_end": s.get("el1", 0.0),
                     "blockage_status": s.get("blockage", 0)}
                    for s in rec.get("fov", [])],
                "message_counter": rec.get("counter", 0),
            })


# ── Synthetic source (no hardware) ──

def _build_rdi_payload(counter: int, pts_sensor: np.ndarray,
                       vel: np.ndarray, rcs: np.ndarray,
                       snr: np.ndarray, sensor_id: int = 0) -> bytes:
    """Serialise synthetic detections into an RDI ISO payload (LE)."""
    n = len(pts_sensor)
    # spherical from sensor-frame Cartesian (inverse of the viewer transform)
    x, y, z = pts_sensor[:, 0], pts_sensor[:, 1], pts_sensor[:, 2]
    r = np.sqrt(x * x + y * y + z * z)
    az = np.arctan2(y, x)
    el = np.arctan2(z, np.sqrt(x * x + y * y))

    h = bytearray()
    h += bytes([3, 0, 0, 14, 1, sensor_id])
    h += struct.pack("<Q", time.time_ns() & ((1 << 64) - 1))
    h += struct.pack("<I", counter & 0xFFFFFFFF)
    h += struct.pack("<I", 50_000_000)
    h += bytes([0, 0])
    h += struct.pack("<f", -30.0)
    h += struct.pack("<f", 30.0)
    h += struct.pack("<H", 4096)
    h += struct.pack("<H", n)
    body = bytearray()
    for i in range(n):
        d = bytearray()
        d += bytes([100])
        d += struct.pack("<H", i & 0xFFFF)
        d += struct.pack("<H", 0)
        d += struct.pack("<f", 0.0)
        d += struct.pack("<f", float(rcs[i]))
        d += struct.pack("<f", float(snr[i]))
        d += struct.pack("<H", 0)
        d += struct.pack("<f", float(r[i]))
        d += struct.pack("<f", float(az[i]))
        d += struct.pack("<f", float(el[i]))
        d += struct.pack("<fff", 0.0, 0.0, 0.0)
        d += struct.pack("<f", float(vel[i]))
        d += struct.pack("<f", 0.0)
        body += d
    return bytes(h + body)


def _build_shii_payload(counter: int, sensor_id: int, temp_status: int) -> bytes:
    p = bytearray(24)
    p[0:3] = bytes([3, 0, 0])
    p[3] = 15
    p[5] = sensor_id
    p[6:14] = struct.pack("<Q", time.time_ns() & ((1 << 64) - 1))
    p[14:18] = struct.pack("<I", counter & 0xFFFFFFFF)
    p[23] = 0
    p += bytes([1, 0])                # n_op=1, op mode 0 = NORMAL_DUAL_RANGE (SHII enum)
    p += bytes([0, 0, 0, temp_status])  # defect, reason, supply_ok, temp_status
    p += bytes([0])                   # n_in=0
    p += bytes([1])                   # time_sync = synced
    p += bytes([0])                   # n_cal=0
    return bytes(p)


def _build_spi_payload(counter: int, sensor_id: int,
                       pose: SensorPose) -> bytes:
    p = bytearray(57)
    p[0:3] = bytes([3, 0, 0])
    p[3] = 16
    p[5] = sensor_id
    p[6:14] = struct.pack("<Q", time.time_ns() & ((1 << 64) - 1))
    p[14:18] = struct.pack("<I", counter & 0xFFFFFFFF)
    p[24] = 0
    o = pose.offset
    p[25:57] = struct.pack("<8f", o[0], o[1], o[2],
                           math.radians(pose.yaw_deg),
                           math.radians(pose.pitch_deg),
                           math.radians(pose.roll_deg), 0.0, 0.0)
    p += bytes([1])                   # one FoV segment
    p += struct.pack("<4f", -1.0, 1.0, -0.3, 0.3)
    p += bytes([0])                   # blockage_status = clear
    return bytes(p)


class SimSource(threading.Thread):
    """Generates synthetic RDI/SHII/SPI for every sensor at ~15 Hz."""

    def __init__(self, sensors: List[str], streams: List[str],
                 store: FrameStore, rec_ctrl: RecordController,
                 fps: float = 15.0, on_log=None):
        super().__init__(name="sim", daemon=True)
        self.sensors = sensors
        self.streams = streams
        self.store = store
        self.rec_ctrl = rec_ctrl
        self.kind = "sim"
        self.on_log = on_log or (lambda m: None)
        self.period = 1.0 / fps
        self._stop_evt = threading.Event()
        # Place the synthetic sensors in a fan so the scene looks 3D.
        self.poses: List[SensorPose] = []
        for i, _ip in enumerate(sensors):
            yaw = (i - (len(sensors) - 1) / 2.0) * 30.0
            self.poses.append(SensorPose(offset=(0.0, (1 - i) * 1.5, 0.6),
                                         yaw_deg=yaw))

    def start_source(self) -> None:
        for i, ip in enumerate(self.sensors):
            self.store.set_pose(i, self.poses[i], ip,
                                {"model_name": "AFI920(sim)",
                                 "serial_number": f"SIM-{i:03d}"})
        self.start()

    def stop(self) -> None:
        self._stop_evt.set()
        self.join(timeout=2.0)

    def recording_meta(self) -> dict:
        return {
            "kind": "sim", "transport": "sim", "started_unix": time.time(),
            "sensors": [
                {"index": i, "ip": ip, "pose": self.poses[i].to_dict(),
                 "device_info": {"model_name": "AFI920(sim)"}}
                for i, ip in enumerate(self.sensors)
            ],
        }

    def _gen_points(self, sensor_i: int, k: int) -> tuple:
        """A drifting target cluster plus static clutter in the sensor frame."""
        rng_phase = 0.15 * k + sensor_i
        # moving target
        m = 120
        t_r = 12.0 + 4.0 * math.sin(rng_phase * 0.4)
        t_az = 0.3 * math.sin(rng_phase * 0.2)
        t_el = 0.1 * math.cos(rng_phase * 0.3)
        ang = np.linspace(0, 2 * math.pi, m)
        rr = t_r + 0.8 * np.cos(ang)
        aaz = t_az + 0.05 * np.sin(ang)
        ael = t_el + 0.05 * np.cos(ang)
        # static clutter spread across the FoV
        c = 200
        cr = np.linspace(3.0, 40.0, c)
        caz = np.linspace(-0.7, 0.7, c) + 0.02 * np.sin(cr)
        cel = 0.02 * np.sin(cr * 0.5)
        r = np.concatenate([rr, cr])
        az = np.concatenate([aaz, caz])
        el = np.concatenate([ael, cel])
        ce = np.cos(el)
        pts = np.stack([r * ce * np.cos(az), r * ce * np.sin(az),
                        r * np.sin(el)], axis=-1)
        vel = np.concatenate([np.full(m, -3.0 * math.sin(rng_phase * 0.4)),
                              np.zeros(c)]).astype(np.float32)
        rcs = np.concatenate([np.full(m, 8.0), np.full(c, -5.0)]).astype(np.float32)
        snr = np.concatenate([np.full(m, 22.0), np.full(c, 9.0)]).astype(np.float32)
        return pts.astype(np.float64), vel, rcs, snr

    def run(self) -> None:
        k = 0
        while not self._stop_evt.is_set():
            host_ts = time.time()
            for i, _ip in enumerate(self.sensors):
                pose = self.poses[i]
                if "rdi" in self.streams:
                    pts, vel, rcs, snr = self._gen_points(i, k)
                    payload = _build_rdi_payload(k, pts, vel, rcs, snr, i)
                    _ingest("rdi", i, payload, pose, self.store, None,
                            self.rec_ctrl, host_ts)
                if "shii" in self.streams and k % 5 == 0:
                    temp = 1 if (k // 50) % 8 == i else 0  # occasional warm
                    payload = _build_shii_payload(k, i, temp)
                    _ingest("shii", i, payload, pose, self.store, None,
                            self.rec_ctrl, host_ts)
                if "spi" in self.streams and k % 5 == 0:
                    payload = _build_spi_payload(k, i, pose)
                    _ingest("spi", i, payload, pose, self.store, None,
                            self.rec_ctrl, host_ts)
                self.store.set_connected(i, True)
            k += 1
            if self._stop_evt.wait(self.period):
                break
