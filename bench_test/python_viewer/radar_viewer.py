#!/usr/bin/env python3
"""
AFI920 Multi-Sensor 3D Radar Viewer
===================================

Real-time 3D point-cloud viewer for several bitsensing AFI920 imaging radars.
Each sensor's mounting pose (offset + yaw/pitch/roll) is read from the Config
API and used to place its RDI detections into one shared ISO 8855 world frame
(X forward, Y left, Z up). A right-hand panel shows each sensor's health
(SHII) and performance (SPI). Capture to disk and replay are built in.

Layout:
  +-----------------------------------+------------------+
  |                                   |  Sensor .150     |
  |        3D point cloud             |   health / perf  |
  |  (one colour per radar)           +------------------+
  |                                   |  Sensor .151 ... |
  +-----------------------------------+------------------+
  [ Record ] [ Replay ] [ Pause ] [ Stop ]  speed[--] colour[v] size[-]

Usage:
    python radar_viewer.py                       # 3 default sensors, live, TCP
    python radar_viewer.py 192.168.10.150 192.168.10.151 192.168.10.152
    python radar_viewer.py --streams rdi shii spi
    python radar_viewer.py --transport udp --host-ip 192.168.10.10
    python radar_viewer.py --sim                 # synthetic demo, no hardware
    python radar_viewer.py --replay recordings/rec_XXXX.jsonl

Dependencies: numpy, PyQt5, pyqtgraph, PyOpenGL  (see requirements.txt)

Copyright (c) 2026 bitsensing Inc.
"""

import argparse
import os
import sys
from datetime import datetime
from pathlib import Path

import numpy as np

# Pin pyqtgraph to PyQt5. Another Qt binding (e.g. PyQt6/PySide6) may also be
# installed; if pyqtgraph is imported first it auto-picks that one, and its GL
# widgets then never see our PyQt5 QApplication -> Qt aborts with
# "Must construct a QApplication before a QWidget". Setting the env var AND
# importing the PyQt5 binding before pyqtgraph guarantees a single binding.
os.environ.setdefault("PYQTGRAPH_QT_LIB", "PyQt5")

try:
    from PyQt5 import QtCore, QtWidgets  # noqa: F401  (import binding first)
    import pyqtgraph as pg
    import pyqtgraph.opengl as gl
except ImportError as exc:  # pragma: no cover - import guard
    sys.stderr.write(
        f"ERROR: GUI dependencies missing ({exc}).\n"
        "Install them with:  pip install -r viewer/requirements.txt\n"
        "  (numpy, PyQt5, pyqtgraph, PyOpenGL)\n")
    raise SystemExit(2)

from radar_source import (  # noqa: E402
    FrameStore, RecordController, LiveSource, ReplaySource, SimSource,
)
from afi_jsonl import JsonlReplayReader  # noqa: E402

DEFAULT_SENSORS = ["192.168.10.150", "192.168.10.151", "192.168.10.152"]

# RGBA (0-1) colour per sensor index.
PALETTE = [
    (1.00, 0.30, 0.30, 1.0),   # red
    (0.35, 1.00, 0.45, 1.0),   # green
    (0.35, 0.65, 1.00, 1.0),   # blue
    (1.00, 0.85, 0.25, 1.0),   # amber
    (0.90, 0.45, 1.00, 1.0),   # violet
    (0.30, 1.00, 1.00, 1.0),   # cyan
]

# Blue -> cyan -> green -> yellow -> red ramp for value colouring.
_CMAP = np.array([
    [0.0, 0.0, 0.6], [0.0, 0.4, 1.0], [0.0, 1.0, 1.0],
    [0.2, 1.0, 0.2], [1.0, 1.0, 0.0], [1.0, 0.2, 0.0],
])


def _palette(i):
    return PALETTE[i % len(PALETTE)]


def _hex(rgba):
    r, g, b = (int(c * 255) for c in rgba[:3])
    return f"#{r:02x}{g:02x}{b:02x}"


def _value_to_rgba(values, vmin, vmax, alpha=1.0):
    v = np.asarray(values, dtype=np.float64)
    if v.size == 0:
        return np.zeros((0, 4), np.float32)
    if vmax <= vmin:
        t = np.zeros_like(v)
    else:
        t = np.clip((v - vmin) / (vmax - vmin), 0.0, 1.0)
    idx = t * (len(_CMAP) - 1)
    lo = np.floor(idx).astype(int)
    hi = np.minimum(lo + 1, len(_CMAP) - 1)
    f = (idx - lo)[:, None]
    rgb = _CMAP[lo] * (1 - f) + _CMAP[hi] * f
    rgba = np.empty((v.size, 4), np.float32)
    rgba[:, :3] = rgb
    rgba[:, 3] = alpha
    return rgba


# ── ISO 23150 SHII / SPI enum decode (raw value -> human label) ──
SHII_DEFECT = {0: "fully functional", 1: "not fully functional", 2: "out of order"}
SHII_REASON = {0: "none", 1: "internal memory", 2: "HW defect", 3: "thermal",
               4: "comm error", 5: "calibration", 6: "config", 7: "mechanical",
               8: "software", 9: "computing power", 10: "out of time-sync",
               11: "ext. disturbed"}
SHII_SUPPLY = {0: "low", 1: "pre-low", 2: "within limits", 3: "pre-high", 4: "high"}
SHII_TEMP = {0: "under-temp", 1: "pre-under-temp", 2: "in limits",
             3: "pre-over-temp", 4: "over-temp"}
SHII_TIMESYNC = {0: "within limits", 1: "out of limits", 2: "timeout",
                 3: "not synchronized"}
SHII_OPMODE = {0: "DR", 1: "LR", 2: "MR", 3: "SR", 4: "ULR", 5: "USR",
               10: "degradation", 50: "evaluation", 100: "calibration",
               200: "initialising", 201: "test"}
SHII_CALSTATUS = {0: "calibrated", 1: "not calibrated", 2: "degraded"}
SPI_BLOCKAGE = {0: "none", 1: "partial", 2: "full", 3: "unknown"}


def _enum_name(table, value):
    return table.get(value, f"#{value}")


def _enum_span(label, value, table, ok_value):
    """`label: NAME` — green when value == ok_value, else red."""
    name = _enum_name(table, value)
    colour = "#6bd66b" if value == ok_value else "#ff6b6b"
    return f"{label}: <span style='color:{colour}'>{name}</span>"


class RadarViewer(QtWidgets.QMainWindow):
    def __init__(self, args):
        super().__init__()
        self.args = args
        self._orig_sensors = list(args.sensors)
        self.sensors = list(args.sensors)
        self.streams = args.streams
        self.store = FrameStore(len(self.sensors))
        self.rec_ctrl = RecordController()
        self.source = None
        self.rec_dir = Path(args.outdir)
        self._mode = ""

        self.setWindowTitle("AFI920 Multi-Sensor 3D Radar Viewer")
        self.resize(1500, 900)

        self.scatters = []
        self.boresights = []
        self._build_ui()

        if args.replay:
            self._open_replay(args.replay)
        else:
            self._start_initial()

        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self._tick)
        self.timer.start(33)  # ~30 Hz

    # ── UI construction ──
    def _build_ui(self):
        pg.setConfigOptions(antialias=True)
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        root = QtWidgets.QVBoxLayout(central)
        root.setContentsMargins(4, 4, 4, 4)

        split = QtWidgets.QSplitter(QtCore.Qt.Horizontal)
        root.addWidget(split, 1)

        # -- 3D view (created once; never torn down) --
        self.view = gl.GLViewWidget()
        self.view.setCameraPosition(distance=45, elevation=22, azimuth=-60)
        try:
            self.view.setBackgroundColor(pg.mkColor(18, 20, 24))
        except Exception:
            pass
        split.addWidget(self.view)

        grid = gl.GLGridItem()
        grid.setSize(x=100, y=100)
        grid.setSpacing(x=5, y=5)
        grid.setColor((90, 90, 100, 120))
        self.view.addItem(grid)
        for vec, col in (((6, 0, 0), (1, 0, 0, 1)),       # X forward (red)
                         ((0, 6, 0), (0, 1, 0, 1)),       # Y left (green)
                         ((0, 0, 6), (0.3, 0.5, 1, 1))):  # Z up (blue)
            self.view.addItem(gl.GLLinePlotItem(
                pos=np.array([[0, 0, 0], vec], dtype=np.float32),
                color=col, width=2, antialias=True))

        self.markers = gl.GLScatterPlotItem(pos=np.zeros((0, 3), np.float32),
                                            size=14.0, pxMode=True)
        self.view.addItem(self.markers)
        self._make_scene_items(len(self.sensors))

        # -- Right status panel --
        right = QtWidgets.QWidget()
        right.setMinimumWidth(360)
        right.setMaximumWidth(480)
        rlay = QtWidgets.QVBoxLayout(right)
        rlay.setContentsMargins(2, 2, 2, 2)
        rlay.addWidget(QtWidgets.QLabel("<b>Sensor status</b>"))

        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        inner = QtWidgets.QWidget()
        self._panel_lay = QtWidgets.QVBoxLayout(inner)
        self.sensor_boxes = []
        self._make_sensor_boxes(self.sensors)
        scroll.setWidget(inner)
        rlay.addWidget(scroll, 1)
        split.addWidget(right)
        split.setStretchFactor(0, 1)
        split.setStretchFactor(1, 0)

        # -- Bottom toolbar --
        root.addLayout(self._build_toolbar())

    def _build_toolbar(self):
        bar = QtWidgets.QHBoxLayout()
        self.btn_record = QtWidgets.QPushButton("● Record")
        self.btn_record.clicked.connect(self._toggle_record)
        self.btn_replay = QtWidgets.QPushButton("▶ Replay…")
        self.btn_replay.clicked.connect(self._pick_replay)
        self.btn_pause = QtWidgets.QPushButton("⏸ Pause")
        self.btn_pause.clicked.connect(self._toggle_pause)
        self.btn_pause.setEnabled(False)
        self.btn_stop = QtWidgets.QPushButton("⏹ Stop replay")
        self.btn_stop.clicked.connect(self._stop_replay)
        self.btn_stop.setEnabled(False)
        for b in (self.btn_record, None, self.btn_replay, self.btn_pause,
                  self.btn_stop):
            if b is None:
                bar.addSpacing(12)
            else:
                bar.addWidget(b)
        bar.addSpacing(12)

        bar.addWidget(QtWidgets.QLabel("speed"))
        self.speed = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        self.speed.setMinimum(25)      # 0.25x
        self.speed.setMaximum(400)     # 4.00x
        self.speed.setValue(100)
        self.speed.setFixedWidth(110)
        self.speed.valueChanged.connect(self._on_speed)
        self.speed_lbl = QtWidgets.QLabel("1.0x")
        bar.addWidget(self.speed)
        bar.addWidget(self.speed_lbl)
        bar.addSpacing(12)

        bar.addWidget(QtWidgets.QLabel("colour"))
        self.color_mode = QtWidgets.QComboBox()
        self.color_mode.addItems(["sensor", "velocity", "snr"])
        bar.addWidget(self.color_mode)

        bar.addWidget(QtWidgets.QLabel("size"))
        self.point_size = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        self.point_size.setMinimum(1)
        self.point_size.setMaximum(10)
        self.point_size.setValue(4)
        self.point_size.setFixedWidth(80)
        bar.addWidget(self.point_size)

        bar.addStretch(1)
        self.status = QtWidgets.QLabel("")
        bar.addWidget(self.status)
        return bar

    def _default_point_size(self):
        return float(self.point_size.value()) if hasattr(self, "point_size") \
            else 4.0

    def _make_scene_items(self, n):
        size = self._default_point_size()
        for i in range(n):
            sc = gl.GLScatterPlotItem(pos=np.zeros((0, 3), np.float32),
                                      size=size, color=_palette(i),
                                      pxMode=True)
            self.view.addItem(sc)
            self.scatters.append(sc)
            ln = gl.GLLinePlotItem(pos=np.zeros((2, 3), np.float32),
                                   color=_palette(i), width=2, antialias=True)
            self.view.addItem(ln)
            self.boresights.append(ln)

    def _clear_scene_items(self):
        for sc in self.scatters:
            self.view.removeItem(sc)
        for ln in self.boresights:
            self.view.removeItem(ln)
        self.scatters = []
        self.boresights = []

    def _make_sensor_boxes(self, ips):
        while self._panel_lay.count():
            item = self._panel_lay.takeAt(0)
            w = item.widget()
            if w is not None:
                w.deleteLater()
        self.sensor_boxes = []
        for i, ip in enumerate(ips):
            box = QtWidgets.QGroupBox(f"● {ip}")
            box.setStyleSheet(
                f"QGroupBox {{ border: 2px solid {_hex(_palette(i))};"
                f" border-radius: 5px; margin-top: 8px; font-weight: bold; }}"
                f"QGroupBox::title {{ subcontrol-origin: margin; left: 8px;"
                f" color: {_hex(_palette(i))}; }}")
            blay = QtWidgets.QVBoxLayout(box)
            health = QtWidgets.QLabel("waiting…")
            health.setTextFormat(QtCore.Qt.RichText)
            health.setWordWrap(True)
            perf = QtWidgets.QLabel("")
            perf.setTextFormat(QtCore.Qt.RichText)
            perf.setWordWrap(True)
            blay.addWidget(health)
            blay.addWidget(perf)
            self._panel_lay.addWidget(box)
            self.sensor_boxes.append((health, perf))
        self._panel_lay.addStretch(1)

    def _set_sensor_count(self, ips):
        """Resize the store, scene items, and status boxes to a sensor list."""
        self.sensors = list(ips)
        self.store = FrameStore(len(self.sensors))
        self._clear_scene_items()
        self._make_scene_items(len(self.sensors))
        self._make_sensor_boxes(self.sensors)

    # ── Source lifecycle ──
    def _start_initial(self):
        self._stop_source()
        if self.sensors != self._orig_sensors:
            self._set_sensor_count(self._orig_sensors)
        if self.args.sim:
            src = SimSource(self.sensors, self.streams, self.store,
                            self.rec_ctrl, on_log=self._log)
            src.start_source()
            self.source = src
            self._mode = "SIM"
        else:
            self.status.setText("connecting to sensors…")
            QtWidgets.QApplication.processEvents()
            src = LiveSource(self.sensors, self.streams, self.args.transport,
                             self.args.host_ip, self.store, self.rec_ctrl,
                             no_configure=self.args.no_configure,
                             on_log=self._log)
            src.start()
            self.source = src
            self._mode = "LIVE"
        self.btn_pause.setEnabled(False)
        self.btn_stop.setEnabled(False)
        self.btn_record.setEnabled(True)

    def _stop_source(self):
        if self.source is not None:
            try:
                self.source.stop()
            except Exception as e:
                self._log(f"stop source: {e}")
            self.source = None

    def _pick_replay(self):
        path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self, "Open recording", str(self.rec_dir),
            "AFI recordings (*.jsonl);;All files (*)")
        if path:
            self._open_replay(path)

    def _open_replay(self, path):
        try:
            reader = JsonlReplayReader(path)
        except Exception as e:
            QtWidgets.QMessageBox.warning(self, "Replay", f"Cannot open:\n{e}")
            return
        if self.rec_ctrl.active:
            self._toggle_record()          # stop recording before replay
        self._stop_source()

        # A recording fixes the sensor set; size the scene/store to match it.
        meta_sensors = (reader.meta or {}).get("sensors", [])
        if meta_sensors:
            ips = [s.get("ip", f"sensor{i}")
                   for i, s in enumerate(meta_sensors)]
            self._set_sensor_count(ips)

        src = ReplaySource(reader, self.store,
                           speed=self.speed.value() / 100.0, on_log=self._log)
        src.start()
        self.source = src
        self._mode = "REPLAY"
        self.btn_pause.setEnabled(True)
        self.btn_pause.setText("⏸ Pause")
        self.btn_stop.setEnabled(True)
        self.btn_record.setEnabled(False)
        self._log(f"Replaying {Path(path).name} "
                  f"({reader.meta.get('record_count', '?')} records)")

    def _stop_replay(self):
        self._start_initial()

    def _toggle_pause(self):
        if isinstance(self.source, ReplaySource):
            if self.source.paused:
                self.source.resume()
                self.btn_pause.setText("⏸ Pause")
            else:
                self.source.pause()
                self.btn_pause.setText("▶ Resume")

    def _on_speed(self, val):
        s = val / 100.0
        self.speed_lbl.setText(f"{s:.2f}x")
        if isinstance(self.source, ReplaySource):
            self.source.set_speed(s)

    # ── Recording ──
    def _toggle_record(self):
        if self.rec_ctrl.active:
            rec = self.rec_ctrl.stop()
            self.btn_record.setText("● Record")
            self.btn_record.setStyleSheet("")
            if rec:
                self._log(f"Saved {rec.record_count} records -> {rec.path}")
            return
        if not hasattr(self.source, "recording_meta"):
            QtWidgets.QMessageBox.information(
                self, "Record", "Recording is only available in live/sim mode.")
            return
        self.rec_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = self.rec_dir / f"rec_{stamp}.jsonl"
        try:
            self.rec_ctrl.start(path, self.source.recording_meta())
        except Exception as e:
            QtWidgets.QMessageBox.warning(self, "Record", f"Cannot record:\n{e}")
            return
        self.btn_record.setText("■ Stop rec")
        self.btn_record.setStyleSheet("background-color:#aa3333; color:white;")
        self._log(f"Recording -> {path}")

    # ── Per-frame GUI update ──
    def _tick(self):
        size = float(self.point_size.value())
        cmode = self.color_mode.currentText()
        snap = self.store.snapshot()

        marker_pos, marker_col = [], []
        total_pts = 0
        for i, s in enumerate(snap):
            if i >= len(self.scatters):
                break
            pts = s["points"]
            total_pts += len(pts)
            if cmode == "sensor" or len(pts) == 0:
                color = _palette(i)
            elif cmode == "velocity":
                color = _value_to_rgba(s["velocity"], -10.0, 10.0)
            else:  # snr
                color = _value_to_rgba(s["snr"], 0.0, 30.0)
            self.scatters[i].setData(pos=pts, color=color, size=size)

            pose = s["pose"]
            p0 = pose.position.astype(np.float32)
            marker_pos.append(p0)
            marker_col.append(_palette(i))
            fwd = pose.forward().astype(np.float32)
            self.boresights[i].setData(
                pos=np.array([p0, p0 + fwd * 3.0], dtype=np.float32))

        # Always update (np.array([]) -> empty) so stale markers clear when
        # the sensor list becomes empty.
        self.markers.setData(
            pos=np.array(marker_pos, dtype=np.float32).reshape(-1, 3),
            color=np.array(marker_col, dtype=np.float32).reshape(-1, 4),
            size=14.0)

        for i, s in enumerate(snap):
            if i >= len(self.sensor_boxes):
                break
            health_lbl, perf_lbl = self.sensor_boxes[i]
            health_lbl.setText(self._fmt_health(s))
            perf_lbl.setText(self._fmt_perf(s))

        extra = ""
        if isinstance(self.source, ReplaySource):
            extra = f"  replay {self.source.progress * 100:4.0f}%"
            if self.source.finished:
                extra += " (end)"
        rec = "  ●REC" if self.rec_ctrl.active else ""
        self.status.setText(f"{self._mode}{rec}   points={total_pts}{extra}")

    # ── Status formatting ──
    def _fmt_health(self, s):
        conn = ("<span style='color:#6bd66b'>connected</span>"
                if s["connected"] else "<span style='color:#888'>—</span>")
        lines = [
            f"<b>conn</b> {conn} &nbsp; <b>fps</b> {s['fps']:.1f}",
            f"<b>RDI</b> det={s['rdi_n']}/{s['rdi_claimed']} "
            f"frame#{s['rdi_counter']} (×{s['rdi_frames']})",
        ]
        if s["truncated"]:
            lines.append("<span style='color:#ff6b6b'>frame truncated</span>")
        h = s["health"]
        if h:
            lines.append("<b>Health (SHII)</b>")
            lines.append(_enum_span("defect", h.get("defect_recognised", 0),
                                    SHII_DEFECT, 0))
            lines.append(_enum_span("reason", h.get("defect_reason", 0),
                                    SHII_REASON, 0))
            lines.append(_enum_span("supply", h.get("supply_voltage_status", 0),
                                    SHII_SUPPLY, 2) + " &nbsp; " +
                         _enum_span("temp", h.get("temperature_status", 0),
                                    SHII_TEMP, 2))
            lines.append(_enum_span("time_sync", h.get("time_sync", 0),
                                    SHII_TIMESYNC, 0))
            modes = h.get("operation_modes", [])
            lines.append("modes: " +
                         (", ".join(_enum_name(SHII_OPMODE, m) for m in modes)
                          or "&mdash;"))
            cal = h.get("calibration_statuses", [])
            cal_txt = ", ".join(_enum_name(SHII_CALSTATUS, c) for c in cal) or "&mdash;"
            cal_col = "#6bd66b" if all(c == 0 for c in cal) else "#ff6b6b"
            lines.append(f"cal: <span style='color:{cal_col}'>{cal_txt}</span>"
                         f" &nbsp; msg#{h.get('message_counter', '?')}")
        else:
            lines.append("<span style='color:#888'>no health yet</span>")
        return "<br>".join(lines)

    def _fmt_perf(self, s):
        p = s["perf"]
        if not p:
            return "<span style='color:#888'>no performance yet</span>"
        pose = p.get("pose", {})
        deg = 180.0 / np.pi
        lines = ["<b>Performance (SPI)</b>",
                 f"pose xyz=({pose.get('origin_x', 0):.2f}, "
                 f"{pose.get('origin_y', 0):.2f}, "
                 f"{pose.get('origin_z', 0):.2f}) m",
                 f"ypr=({pose.get('yaw', 0) * deg:.1f}, "
                 f"{pose.get('pitch', 0) * deg:.1f}, "
                 f"{pose.get('roll', 0) * deg:.1f})°"]
        segs = p.get("fov_segments", [])
        if segs:
            blocked = sum(1 for seg in segs if seg.get("blockage_status", 0))
            colour = "#ff6b6b" if blocked else "#6bd66b"
            lines.append(f"FoV blockage: <span style='color:{colour}'>"
                         f"{blocked}/{len(segs)} blocked</span>")
        lines.append(f"msg#{p.get('message_counter', '?')}")
        return "<br>".join(lines)

    # ── misc ──
    def _log(self, msg):
        print(msg, flush=True)

    def closeEvent(self, ev):
        self.timer.stop()
        try:
            self.rec_ctrl.stop()
        except Exception:
            pass
        self._stop_source()
        super().closeEvent(ev)


def parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="AFI920 multi-sensor 3D radar viewer")
    p.add_argument("sensors", nargs="*", default=DEFAULT_SENSORS,
                   metavar="SENSOR_IP", help="Sensor IPs")
    p.add_argument("--streams", nargs="+", default=["rdi", "shii", "spi"],
                   choices=["rdi", "shii", "spi"], help="Streams to view")
    p.add_argument("--transport", choices=["tcp", "udp"], default="tcp")
    p.add_argument("--host-ip", default=None, help="Host IP for UDP push")
    p.add_argument("--sim", action="store_true",
                   help="Synthetic demo source (no hardware)")
    p.add_argument("--replay", default=None, help="Open a .jsonl recording")
    p.add_argument("--no-configure", action="store_true",
                   help="Do not touch sensor config")
    p.add_argument("--outdir",
                   default=str(Path(__file__).resolve().parent / "recordings"),
                   help="Directory for recordings")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    if args.transport == "udp" and not args.host_ip and not args.sim \
            and not args.replay:
        sys.stderr.write("ERROR: --transport udp requires --host-ip\n")
        return 2
    app = QtWidgets.QApplication(sys.argv[:1])
    win = RadarViewer(args)
    win.show()
    return app.exec_()


if __name__ == "__main__":
    raise SystemExit(main())
