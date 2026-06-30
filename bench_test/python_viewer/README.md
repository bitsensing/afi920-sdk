# AFI920 Multi-Sensor 3D Radar Viewer

Real-time 3D point-cloud viewer for several bitsensing AFI920 imaging radars at
once. Each radar's mounting pose (offset + yaw/pitch/roll) is read from the
Config API and used to fuse every sensor's RDI detections into one shared world
frame. A right-hand panel shows each radar's health (SHII) and performance
(SPI), and you can record live data to disk and replay it.

```
+-----------------------------------+------------------+
|                                   |  ● 192.168.10.150 |
|        3D point cloud             |   conn / fps      |
|   radar .150 = red                |   Health (SHII)   |
|   radar .151 = green              |   Performance(SPI)|
|   radar .152 = blue               +------------------+
|   (grid + X/Y/Z axes)             |  ● 192.168.10.151 |
|                                   |   ...             |
+-----------------------------------+------------------+
[● Record] [▶ Replay…] [⏸ Pause] [⏹ Stop]  speed[──] colour[sensor▾] size[─]
```

## Install

```bash
pip install -r viewer/requirements.txt        # numpy, PyQt5, pyqtgraph, PyOpenGL
# afi_sdk is used from ../python/src automatically, or: pip install -e ./python
```

## Run

```bash
cd viewer

# Live: 3 default sensors (.150/.151/.152), TCP, all streams
python radar_viewer.py

# Choose sensors / streams / transport
python radar_viewer.py 192.168.10.150 192.168.10.151
python radar_viewer.py --streams rdi shii spi
python radar_viewer.py --transport udp --host-ip 192.168.10.10

# Synthetic demo — no hardware needed (great for trying record/replay)
python radar_viewer.py --sim

# Open a recording directly
python radar_viewer.py --replay recordings/rec_20260625_143000.btsrec
```

> **Qt binding note.** If PyQt6 or PySide6 is also installed, pyqtgraph could
> otherwise bind to it and clash with this app's PyQt5 `QApplication`
> (`QWidget: Must construct a QApplication before a QWidget`). The viewer pins
> `PYQTGRAPH_QT_LIB=PyQt5` and imports PyQt5 first, so this is handled
> automatically — just make sure PyQt5 is installed (it's in requirements.txt).

## Coordinate frame

World frame is **ISO 8855** (automotive): **X = forward, Y = left, Z = up**,
right-handed. A detection's sensor-frame position is

```
x = r·cos(el)·cos(az)
y = r·cos(el)·sin(az)
z = r·sin(el)
```

then rotated by `R = Rz(yaw)·Ry(pitch)·Rx(roll)` and translated by the mounting
offset to reach the world frame. The maths lives in `geometry.py` — change the
sign conventions there if your fleet uses a different frame.

Each sensor's origin is drawn as a large marker with a short line showing its
boresight (+X) direction. The red/green/blue axis lines at the origin are
world X/Y/Z.

## Controls

| Control | Effect |
|---------|--------|
| Mouse drag / wheel | Orbit / zoom the 3D view |
| **● Record** | Start/stop recording the live (or sim) streams to `recordings/rec_<time>.btsrec` |
| **▶ Replay…** | Open a `.btsrec` file; the scene resizes to the recorded sensor set |
| **⏸ Pause / ▶ Resume** | Pause/resume replay |
| **⏹ Stop replay** | Stop replay and return to the live/sim source |
| **speed** | Replay speed 0.25×–4× |
| **colour** | Colour points by `sensor`, `velocity`, or `snr` |
| **size** | Point size |

## Recording format

A recording is two files sharing a stem:

- `rec_<time>.btsrec` — binary log of raw ISO 23150 payloads (post-E2E),
  length-prefixed with a host timestamp, stream type, and sensor index. Replay
  re-parses these through the **exact same code path** as live capture.
- `rec_<time>.json` — sidecar metadata: sensor IPs, per-sensor mounting pose
  (so replay places points identically), transport, start time, record count.

## How it works

- **Receive** reuses the hardened receive layer from
  [../logger/radar_logger.py](../logger/radar_logger.py) (`StreamWorker`:
  TCP/UDP framing, SOME/IP-TP reassembly, E2E CRC verification, auto-reconnect).
- **`radar_source.py`** parses each RDI payload with a vectorised numpy
  structured dtype, converts to world points via `geometry.py`, and stores the
  latest per-sensor state in a thread-safe `FrameStore`.
- **`radar_viewer.py`** is the PyQt5 + pyqtgraph GL window. A 30 Hz timer
  snapshots the store and updates the scatter plots and status panels — Qt is
  only ever touched from the GUI thread; receiver threads only write to the
  store.

`geometry.py`, `recording.py`, and `radar_source.py` are GUI-free and unit
tested headless; only `radar_viewer.py` needs PyQt5/PyOpenGL.

## Modules

| File | Role |
|------|------|
| `radar_viewer.py` | PyQt5 + pyqtgraph GUI (entry point) |
| `radar_source.py` | FrameStore, payload→world packing, live/replay/sim sources |
| `geometry.py` | ISO 8855 pose math and spherical→Cartesian transform |
| `recording.py` | `.btsrec` recorder and replay reader |
