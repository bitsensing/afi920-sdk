# AFI920 Multi-Sensor 3D Radar Viewer (C++)

C++/Qt port of the Python [`python_viewer/`](../python_viewer/) app. Real-time 3D point-cloud
viewer for several bitsensing AFI920 imaging radars at once. Each radar's
mounting pose (offset + yaw/pitch/roll) is read from the Config API and used to
fuse every sensor's RDI detections into one shared ISO 8855 world frame
(X forward, Y left, Z up). A right-hand panel shows each radar's health (SHII)
and performance (SPI); live data can be recorded to disk and replayed.

```
+-----------------------------------+------------------+
|                                   |  ● 192.168.10.150 |
|   3D point cloud (OpenGL)         |   conn / fps      |
|   radar .150 = red                |   Health (SHII)   |
|   radar .151 = green              |   Performance(SPI)|
|   radar .152 = blue               +------------------+
|   grid + X/Y/Z axes + markers     |  ● 192.168.10.151 |
+-----------------------------------+------------------+
[● Record] [▶ Replay…] [⏸ Pause] [⏹ Stop]  speed[──] colour[▾] size[─] [reset]
```

Built on the repo's **C++ SDK** (`afi::AfiSdk`): one SDK instance per sensor,
`RegRecvCallback` for RDI/SHII/SPI, `GetMountingPosition` / `GetRotation` for
pose. The SDK handles SOME/IP, E2E CRC, and TP reassembly internally.

## Prerequisites

- A C++17 compiler (MSVC 2019+, GCC 9+, or Clang)
- CMake ≥ 3.16
- **Qt 6** (Widgets, OpenGL, OpenGLWidgets) — or **Qt 5** (Widgets)
- An OpenGL 3.3-capable GPU/driver

The AFI SDK and `nlohmann/json` are built from this repository — no extra
install needed.

## Build

```bash
cd cpp_viewer
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<compiler>
cmake --build build --config Release
```

`CMAKE_PREFIX_PATH` should point at your Qt install (the dir containing
`lib/cmake/Qt6`). On Windows with the Qt installer, e.g.
`-DCMAKE_PREFIX_PATH=C:/Qt/6.7.0/msvc2019_64`.

The build reuses the repo's `afi_sdk` target (it configures the parent project
with examples/tests off). You can also add this folder to the top-level build
with `add_subdirectory(cpp_viewer)` — the CMake detects an existing `afi_sdk`
target and links it instead of rebuilding.

## Run

```bash
./build/afi_radar_viewer                                  # 3 default sensors, live, TCP
./build/afi_radar_viewer 192.168.10.150 192.168.10.151
./build/afi_radar_viewer --streams rdi shii spi
./build/afi_radar_viewer --transport udp --host-ip 192.168.10.10
./build/afi_radar_viewer --sim                            # synthetic demo, no hardware
./build/afi_radar_viewer --replay recordings/rec_XXXX.btsrec3
```

| Option | Default | Description |
|--------|---------|-------------|
| `SENSOR_IP ...` | `.150 .151 .152` | Sensor IPs (positional) |
| `--streams` | `rdi shii spi` | Streams to view |
| `--transport` | `tcp` | `tcp` or `udp` (udp needs `--host-ip`) |
| `--host-ip` | — | Host IP the sensors push UDP to |
| `--sim` | off | Synthetic demo source (no hardware) |
| `--replay FILE` | — | Open a `.btsrec3` recording |
| `--no-configure` | off | Don't touch sensor config |
| `--outdir DIR` | `recordings` | Where recordings are written |

## Controls

| Control | Effect |
|---------|--------|
| Left-drag | Orbit camera |
| Right/Middle-drag | Pan |
| Wheel | Zoom |
| **● Record** | Start/stop recording live/sim to `recordings/rec_<time>.btsrec3` |
| **▶ Replay…** | Open a `.btsrec3`; scene resizes to the recorded sensor set |
| **⏸ / ▶** | Pause / resume replay |
| **⏹ Stop replay** | Return to the live/sim source |
| **speed** | Replay speed 0.25×–4× |
| **colour** | `sensor` / `velocity` / `snr` |
| **size** / **reset view** | Point size / reset camera |

## Coordinate frame

World is **ISO 8855**: X forward, Y left, Z up (right-handed), identical to the
Python viewer. A detection's sensor-frame position is
`x=r·cos(el)·cos(az), y=r·cos(el)·sin(az), z=r·sin(el)`, then rotated by
`R = Rz(yaw)·Ry(pitch)·Rx(roll)` and translated by the mounting offset. See
[`geometry.hpp`](geometry.hpp).

## Recording format

Two files share a stem:

- `rec_<time>.btsrec3` — binary log of parsed frames in **host byte order**.
  Detections are stored in the sensor frame (r/az/el + v/rcs/snr) so replay
  re-applies the recorded pose. (This is a C++-native format and is **not**
  interchangeable with the Python viewer's `.btsrec`, which stores raw ISO
  payloads.)
- `rec_<time>.json` — sidecar: per-sensor pose, ip, transport, record count.

## Files

| File | Role |
|------|------|
| `main.cpp` / `main_window.*` | Qt window, panels, toolbar, 30 Hz update |
| `point_cloud_view.*` | OpenGL 3.3 point-cloud widget (orbit camera) |
| `radar_source.*` | LiveSource (SDK), ReplaySource, SimSource, RecordController |
| `frame_store.hpp` | Thread-safe latest-state per sensor |
| `geometry.hpp` | ISO 8855 pose math (header-only) |
| `recording.*` | `.btsrec3` recorder + replay reader |

## Threading

Receiver (SDK callback), replay, and sim threads only ever write the
`FrameStore` (mutex-guarded). The Qt GUI thread polls `snapshot()` on a 30 Hz
timer and is the only thread that touches Qt/OpenGL — the same model as the
Python viewer.
