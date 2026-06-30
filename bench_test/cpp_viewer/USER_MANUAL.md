# AFI920 C++ 3D Viewer — User Manual

> Tool: `bench_test/cpp_viewer` · Recording format:
> [`afi-radar-jsonl`](../python_recording_920_cli/JSONL_FORMAT.md) (`.jsonl`)

---

## English

### What it does
The C++/Qt build of the 3D radar viewer (Qt Widgets + OpenGL). Same features as
the Python viewer: real-time fused point cloud from up to several AFI920 radars,
a per-sensor SHII/SPI status panel with decoded enum labels, **record** the live
session, and **replay** any `.jsonl` recording (from this viewer or either CLI
recorder).

### Prerequisites
- A C++17 compiler + CMake, and **Qt 5.15+ or Qt 6** (Widgets + OpenGL +
  OpenGLWidgets) dev packages. OpenGL 3.3 core profile capable GPU/driver.
- The repo's `afi_sdk` (built automatically) and vendored `nlohmann/json`.

### Build & run
```bash
# build-on-demand launcher (recommended)
./run.sh 192.168.10.150 192.168.10.151
./run.sh --sim
./run.sh --replay recordings/rec_XXXX.jsonl

# or build manually
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/afi_radar_viewer [options]
```
> Linux: `build/afi_radar_viewer`. Windows/MinGW: `build/afi_radar_viewer.exe`
> (run `windeployqt` once to copy the Qt DLLs next to the exe).

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
| `--outdir DIR` | where recordings are written (default `recordings`) |

### On-screen controls
Same as the Python viewer: **● Record / ■ Stop rec**, **▶ Replay…**,
**⏸ Pause/Resume**, **⏹ Stop replay**, **speed** slider, **colour**
(sensor/velocity/snr), **size**, **reset view**. Right panel shows connection,
fps, RDI count and decoded SHII/SPI values (green = nominal, red = out of range).
Mouse: drag to orbit, wheel to zoom.

### Cross-tool replay
The format is shared, so:
```bash
./run.sh --replay ../python_recording_920_cli/recordings/rec_XXXX.jsonl
```
opens a Python-CLI recording, and files made here open in the Python viewer.

### Troubleshooting
- **Build can't find Qt** — pass `-DCMAKE_PREFIX_PATH=/path/to/Qt/<ver>/<arch>`.
- **Blank window / GL errors** — ensure an OpenGL 3.3 core-profile capable driver.
- **Live shows nothing** — radar not streaming to you; if TCP and another program
  holds the radar's single TCP slot, close it. Recording is live/sim only.

---

## 한국어

### 기능
3D 레이더 뷰어의 C++/Qt 빌드(Qt Widgets + OpenGL)입니다. Python 뷰어와 기능 동일:
여러 대의 AFI920 레이더를 융합한 실시간 포인트 클라우드, 센서별 SHII/SPI 상태 패널
(enum 라벨 디코딩), 라이브 세션 **녹화**, 어떤 `.jsonl` 녹화든(이 뷰어 또는 두 CLI
레코더의 파일) **재생**.

### 사전 준비
- C++17 컴파일러 + CMake, 그리고 **Qt 5.15+ 또는 Qt 6**(Widgets + OpenGL +
  OpenGLWidgets) 개발 패키지. OpenGL 3.3 core profile 지원 GPU/드라이버.
- 레포의 `afi_sdk`(자동 빌드)와 vendored `nlohmann/json`.

### 빌드 & 실행
```bash
# 빌드-온-디맨드 런처(권장)
./run.sh 192.168.10.150 192.168.10.151
./run.sh --sim
./run.sh --replay recordings/rec_XXXX.jsonl

# 또는 수동 빌드
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/afi_radar_viewer [옵션]
```
> 리눅스: `build/afi_radar_viewer`. Windows/MinGW: `build/afi_radar_viewer.exe`
> (한 번 `windeployqt`로 Qt DLL을 exe 옆에 복사).

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
| `--outdir DIR` | 녹화 저장 위치(기본 `recordings`) |

### 화면 컨트롤
Python 뷰어와 동일: **● Record / ■ Stop rec**, **▶ Replay…**, **⏸ Pause/Resume**,
**⏹ Stop replay**, **speed** 슬라이더, **colour**(sensor/velocity/snr), **size**,
**reset view**. 오른쪽 패널에 연결/fps/RDI 개수와 SHII·SPI 값(녹색=정상, 빨강=범위
벗어남). 마우스: 드래그로 회전, 휠로 줌.

### 도구 간 재생
포맷이 공유되므로:
```bash
./run.sh --replay ../python_recording_920_cli/recordings/rec_XXXX.jsonl
```
로 Python CLI 녹화를 열 수 있고, 여기서 만든 파일도 Python 뷰어에서 열립니다.

### 문제 해결
- **빌드가 Qt를 못 찾음** — `-DCMAKE_PREFIX_PATH=/path/to/Qt/<버전>/<아키텍처>` 전달.
- **빈 창 / GL 에러** — OpenGL 3.3 core profile 지원 드라이버 확인.
- **라이브에 아무것도 안 나옴** — 레이더 미송출. TCP인데 다른 프로그램이 TCP 슬롯(1개)
  점유 중이면 닫으세요. 녹화는 라이브/sim에서만 가능합니다.
