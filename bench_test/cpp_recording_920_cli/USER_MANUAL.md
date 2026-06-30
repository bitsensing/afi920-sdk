# AFI920 C++ CLI Recorder — User Manual

> Tool: `bench_test/cpp_recording_920_cli` · Output format:
> [`afi-radar-jsonl`](../python_recording_920_cli/JSONL_FORMAT.md) (`.jsonl`)

---

## English

### What it does
The C++ build of the CLI recorder. Connects to up to **4** AFI920 radars via the
C++ SDK and records RDI/SHII/SPI to a single `.jsonl` file. No GUI. The output
replays in **either** 3D viewer (Python or C++) — identical format.

### Prerequisites
- A C++17 compiler + CMake (no Qt needed for the recorder).
- The repo's `afi_sdk` (built automatically) and vendored `nlohmann/json`.
- Radars reachable on the network.

### Build & run
```bash
# build-on-demand launcher (recommended): builds the first time, then runs
./run.sh record 192.168.10.150 --duration 30

# or build manually
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/afi_recorder record 192.168.10.150 --duration 30
```
> Linux produces `build/afi_recorder`; Windows/MinGW produces
> `build/afi_recorder.exe` (statically linked — no extra DLLs needed).

### Command & arguments
`record` is the command. A **target** is `IP` or `IP:PORT` (`PORT` = RDI port,
default `30509`; SHII/SPI = `PORT+1`/`PORT+2`). Up to **4** radars.

| Option | Meaning |
|--------|---------|
| `IP[:PORT] ...` / `--radar IP[:PORT]` | radar target(s), up to 4 |
| `--config FILE` | JSON config; used only when no target is on the CLI |
| `--streams rdi shii spi` | streams to record (default: all three) |
| `--transport tcp\|udp` | transport (default `tcp`) |
| `--host-ip IP` | host IP the radars push to (required for `udp`) |
| `--duration N` | record N seconds (`0` = until Ctrl+C) |
| `--outdir DIR` | output directory (default `./recordings`, auto-created) |
| `--out NAME` | output filename (default `rec_<timestamp>.jsonl`) |
| `--no-configure` | passive receive; do not use the Config API (pose = 0) |

`radars.json` has the same schema as the Python recorder.

### Examples
```bash
./run.sh record 192.168.10.150 192.168.10.151:30509 --streams rdi shii spi
./run.sh record --radar 192.168.10.150 --duration 60
./run.sh record --config radars.json
./run.sh record 192.168.10.150 --transport udp --host-ip 192.168.10.10
```

### Output & replay
`recordings/rec_<timestamp>.jsonl` — header line + one record per line. Replay:
```bash
../cpp_viewer/run.sh    --replay recordings/rec_XXXX.jsonl
../python_viewer/run.sh --replay recordings/rec_XXXX.jsonl
```

### Troubleshooting
- **0 frames over TCP** — another program holds the radar's single TCP client slot
  (e.g. a viewer). Close it. Not a reason to switch to UDP.
- **TCP refuses after a UDP run** — a prior `--transport udp` switched the radar to
  UDP push; run once in TCP mode to flip it back.
- **`cannot open recording file`** — the recorder now auto-creates `--outdir`; if it
  still fails, check directory permissions.

---

## 한국어

### 기능
CLI 레코더의 C++ 빌드입니다. C++ SDK로 최대 **4대**의 AFI920 레이더에 접속해
RDI/SHII/SPI를 하나의 `.jsonl` 파일로 녹화합니다. GUI 없음. 출력은 Python·C++
**두 viewer 모두에서** 재생됩니다(포맷 동일).

### 사전 준비
- C++17 컴파일러 + CMake (레코더는 Qt 불필요).
- 레포의 `afi_sdk`(자동 빌드)와 vendored `nlohmann/json`.
- 레이더 네트워크 연결.

### 빌드 & 실행
```bash
# 빌드-온-디맨드 런처(권장): 처음엔 빌드, 이후엔 실행만
./run.sh record 192.168.10.150 --duration 30

# 또는 수동 빌드
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/afi_recorder record 192.168.10.150 --duration 30
```
> 리눅스는 `build/afi_recorder`, Windows/MinGW는 `build/afi_recorder.exe`(정적
> 링크 — 추가 DLL 불필요)를 생성합니다.

### 명령어 & 인자
`record` 가 명령어입니다. **타깃**은 `IP` 또는 `IP:PORT`(`PORT` = RDI 포트, 기본
`30509`; SHII/SPI = `PORT+1`/`PORT+2`). 최대 **4대**.

| 옵션 | 의미 |
|------|------|
| `IP[:PORT] ...` / `--radar IP[:PORT]` | 레이더 타깃(최대 4대) |
| `--config FILE` | JSON 설정. CLI에 타깃이 없을 때만 사용 |
| `--streams rdi shii spi` | 녹화 스트림(기본: 3종 전부) |
| `--transport tcp\|udp` | 전송 방식(기본 `tcp`) |
| `--host-ip IP` | 레이더가 UDP를 쏘는 호스트 IP(`udp`일 때 필수) |
| `--duration N` | N초 녹화(`0` = Ctrl+C까지) |
| `--outdir DIR` | 출력 폴더(기본 `./recordings`, 자동 생성) |
| `--out NAME` | 출력 파일명(기본 `rec_<시각>.jsonl`) |
| `--no-configure` | 수동 수신; Config API 미사용(pose = 0) |

`radars.json` 스키마는 Python 레코더와 동일합니다.

### 예시
```bash
./run.sh record 192.168.10.150 192.168.10.151:30509 --streams rdi shii spi
./run.sh record --radar 192.168.10.150 --duration 60
./run.sh record --config radars.json
./run.sh record 192.168.10.150 --transport udp --host-ip 192.168.10.10
```

### 출력 & 재생
`recordings/rec_<시각>.jsonl` — 헤더 줄 + 한 줄당 레코드 1개. 재생:
```bash
../cpp_viewer/run.sh    --replay recordings/rec_XXXX.jsonl
../python_viewer/run.sh --replay recordings/rec_XXXX.jsonl
```

### 문제 해결
- **TCP인데 0 프레임** — 다른 프로그램이 레이더의 TCP 클라이언트 슬롯(1개)을 점유
  중(예: viewer). 닫으세요. UDP로 바꿀 이유가 아닙니다.
- **UDP 실행 후 TCP 거부** — 이전 `--transport udp`가 레이더를 UDP push로 바꿔놓은
  것. TCP 모드로 한 번 실행하면 되돌아갑니다.
- **`cannot open recording file`** — 레코더가 `--outdir`을 자동 생성합니다. 그래도
  실패하면 폴더 권한을 확인하세요.
