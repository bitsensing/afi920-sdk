# AFI920 Python 3D Viewer — User Manual

> Tool: `bench_test/python_viewer` · Recording format:
> [`afi-radar-jsonl`](../python_recording_920_cli/JSONL_FORMAT.md) (`.jsonl`)

---

## English

### What it does
A real-time 3D point-cloud viewer (PyQt5 + pyqtgraph) for up to several AFI920
radars. It fuses every sensor's RDI detections into one world-frame scene using
each sensor's mounting pose (read from the Config API), shows a right-hand panel
with per-sensor **health (SHII)** and **performance (SPI)** decoded to readable
enum labels, and can **record** the live session and **replay** any `.jsonl`
recording (from this viewer or either CLI recorder).

### Prerequisites
- Python 3.8+ with `numpy`, `PyQt5`, `pyqtgraph`, `PyOpenGL`
  (`pip install -r requirements.txt`).
- For live mode, radars reachable on the network.

### Run
```bash
# launcher (recommended)
./run.sh 192.168.10.150 192.168.10.151        # live, these sensors
./run.sh --sim                                 # synthetic demo, no hardware
./run.sh --replay recordings/rec_XXXX.jsonl    # replay a recording

# or directly
python3 radar_viewer.py [options]
```

### Options
| Option | Meaning |
|--------|---------|
| `SENSOR_IP ...` | sensor IPs to view live (default: .150 .151 .152) |
| `--streams rdi shii spi` | streams to subscribe (default: all) |
| `--transport tcp\|udp` | transport (default `tcp`) |
| `--host-ip IP` | host IP for UDP push (required with `--transport udp`) |
| `--sim` | synthetic data, no hardware |
| `--replay FILE` | open a `.jsonl` recording instead of going live |
| `--no-configure` | don't reconfigure sensors |
| `--outdir DIR` | where recordings are written (default `./recordings`) |

### On-screen controls
- **● Record / ■ Stop rec** — start/stop recording the current live (or sim)
  session to `recordings/rec_<timestamp>.jsonl`.
- **▶ Replay…** — open a `.jsonl` file (records the sensor set + poses from its
  header). **⏸ Pause / ▶ Resume**, **⏹ Stop replay**, and the **speed** slider
  (0.25×–4×) control playback.
- **colour**: `sensor` / `velocity` / `snr`; **size**: point size; **reset view**.
- Right panel: connection, fps, RDI count, and SHII/SPI values shown as named
  enums (green = nominal, red = out of range).

### Recording & cross-tool replay
Recording uses the unified format, so a file made here also opens in the C++
viewer, and files from the Python/C++ CLI recorders open here:
```bash
./run.sh --replay ../cpp_recording_920_cli/recordings/rec_XXXX.jsonl
```

### Troubleshooting
- **`QWidget: Must construct a QApplication before a QWidget`** — caused by mixed
  PyQt5/PyQt6; this viewer forces PyQt5 (`PYQTGRAPH_QT_LIB=PyQt5`). Use the
  provided `requirements.txt`.
- **Live shows nothing / "no health yet"** — the radar isn't streaming to you. If
  in TCP mode and another program (or CLI recorder) holds the radar's single TCP
  slot, close it. Recording is only available in live/sim mode (not during replay).

---

## 한국어

### 기능
여러 대의 AFI920 레이더를 위한 실시간 3D 포인트 클라우드 뷰어(PyQt5 + pyqtgraph)
입니다. 각 센서의 장착 pose(Config API로 읽음)를 이용해 모든 RDI 검출을 하나의
월드좌표 장면으로 융합하고, 오른쪽 패널에 센서별 **상태(SHII)** 와 **성능(SPI)** 을
읽기 쉬운 enum 라벨로 표시합니다. 라이브 세션을 **녹화**하고, 어떤 `.jsonl`
녹화 파일이든(이 뷰어 또는 두 CLI 레코더의 파일) **재생**할 수 있습니다.

### 사전 준비
- Python 3.8+ 와 `numpy`, `PyQt5`, `pyqtgraph`, `PyOpenGL`
  (`pip install -r requirements.txt`).
- 라이브 모드는 레이더가 네트워크에 연결돼 있어야 합니다.

### 실행
```bash
# 런처(권장)
./run.sh 192.168.10.150 192.168.10.151        # 라이브
./run.sh --sim                                 # 합성 데모(하드웨어 불필요)
./run.sh --replay recordings/rec_XXXX.jsonl    # 녹화 재생

# 또는 직접 실행
python3 radar_viewer.py [옵션]
```

### 옵션
| 옵션 | 의미 |
|------|------|
| `SENSOR_IP ...` | 라이브로 볼 센서 IP(기본: .150 .151 .152) |
| `--streams rdi shii spi` | 구독할 스트림(기본: 전부) |
| `--transport tcp\|udp` | 전송 방식(기본 `tcp`) |
| `--host-ip IP` | UDP push용 호스트 IP(`--transport udp`일 때 필수) |
| `--sim` | 합성 데이터(하드웨어 불필요) |
| `--replay FILE` | 라이브 대신 `.jsonl` 녹화 파일 열기 |
| `--no-configure` | 센서 재설정 안 함 |
| `--outdir DIR` | 녹화 저장 위치(기본 `./recordings`) |

### 화면 컨트롤
- **● Record / ■ Stop rec** — 현재 라이브(또는 sim) 세션을
  `recordings/rec_<시각>.jsonl` 로 녹화 시작/중지.
- **▶ Replay…** — `.jsonl` 파일 열기(헤더의 센서 구성·pose를 그대로 사용).
  **⏸ Pause / ▶ Resume**, **⏹ Stop replay**, **speed** 슬라이더(0.25×–4×)로 재생 제어.
- **colour**: `sensor` / `velocity` / `snr`, **size**: 점 크기, **reset view**.
- 오른쪽 패널: 연결/ fps / RDI 개수, SHII·SPI 값을 enum 이름으로 표시(녹색=정상,
  빨강=범위 벗어남).

### 녹화 & 도구 간 재생
녹화는 통합 포맷을 쓰므로 여기서 만든 파일이 C++ 뷰어에서도 열리고, Python/C++
CLI 레코더가 만든 파일도 여기서 열립니다:
```bash
./run.sh --replay ../cpp_recording_920_cli/recordings/rec_XXXX.jsonl
```

### 문제 해결
- **`QWidget: Must construct a QApplication before a QWidget`** — PyQt5/PyQt6 혼용
  때문이며, 이 뷰어는 PyQt5를 강제합니다(`PYQTGRAPH_QT_LIB=PyQt5`). 제공된
  `requirements.txt`를 사용하세요.
- **라이브에 아무것도 안 나옴 / "no health yet"** — 레이더가 데이터를 안 보내는
  상태입니다. TCP 모드인데 다른 프로그램(또는 CLI 레코더)이 레이더의 TCP 슬롯(1개)을
  점유 중이면 닫으세요. 녹화는 라이브/sim 모드에서만 가능합니다(재생 중에는 불가).
