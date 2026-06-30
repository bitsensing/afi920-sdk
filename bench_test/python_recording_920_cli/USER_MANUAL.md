# AFI920 Python CLI Recorder — User Manual

> Tool: `bench_test/python_recording_920_cli` · Output format:
> [`afi-radar-jsonl`](JSONL_FORMAT.md) (`.jsonl`)

---

## English

### What it does
Connects to up to **4** AFI920 imaging radars and records their RDI (point cloud),
SHII (health) and SPI (performance) streams to a single line-delimited JSON
(`.jsonl`) file. No GUI. A file recorded here replays in **either** 3D viewer
(Python or C++).

### Prerequisites
- Python 3.8+ (`python3`). Standard library only — no numpy needed.
- The radars reachable on the network (default subnet `192.168.10.x`).
- Run from inside the repo (it auto-finds the SDK at `<repo>/python/src` and the
  receive layer at `<repo>/logger`).

### Run
```bash
# via the Linux launcher (recommended)
./run.sh record 192.168.10.150 --duration 30

# or directly
python3 record_cli.py record 192.168.10.150 --duration 30
```

### Command & arguments
`record` is the sub-command. A **target** is `IP` or `IP:PORT`, where `PORT` is the
radar's **RDI port** (default `30509`); SHII and SPI use `PORT+1` / `PORT+2`.

| Option | Meaning |
|--------|---------|
| `IP[:PORT] ...` / `--radar IP[:PORT]` | radar target(s), up to 4 |
| `--config FILE` | JSON config; used **only when no target is on the CLI** |
| `--streams rdi shii spi` | streams to record (default: all three) |
| `--transport tcp\|udp` | transport (default `tcp`) |
| `--host-ip IP` | host IP the radars push to (**required** for `udp`) |
| `--duration N` | record N seconds (`0` = until Ctrl+C) |
| `--outdir DIR` | output directory (default `./recordings`) |
| `--out NAME` | output filename (default `rec_<timestamp>.jsonl`) |
| `--no-configure` | do not touch sensor config (assume streams already set up) |

### Config file (`radars.json`)
Used when you run `record` with **no** target on the command line:
```json
{
  "transport": "tcp",
  "streams": ["rdi", "shii", "spi"],
  "radars": [
    { "ip": "192.168.10.150", "port": 30509 },
    { "ip": "192.168.10.151", "port": 30509 }
  ]
}
```
CLI flags override the config values.

### Examples
```bash
./run.sh record 192.168.10.150 192.168.10.151:30509 --streams rdi shii spi
./run.sh record --radar 192.168.10.150 --radar 192.168.10.151 --duration 60
./run.sh record --config radars.json
./run.sh record 192.168.10.150 --transport udp --host-ip 192.168.10.10
```

### Output
`recordings/rec_<timestamp>.jsonl` — line 1 is a header (sensors, mounting poses,
transport); each following line is one `rdi`/`shii`/`spi` record. Replay it:
```bash
../python_viewer/run.sh --replay recordings/rec_XXXX.jsonl   # Python viewer
../cpp_viewer/run.sh    --replay recordings/rec_XXXX.jsonl   # C++ viewer
```

### Troubleshooting
- **0 frames over TCP** — almost always means *another program already holds the
  radar's single TCP client slot* (e.g. a running viewer). Close it, then record.
  It does **not** mean you must use UDP.
- **TCP ports refuse after a UDP run** — `--transport udp` reconfigures the radar
  to push UDP via the Config API. Running once in TCP mode flips it back.
- **UDP needs `--host-ip`** — set it to this host's IP on the radar subnet.

---

## 한국어

### 기능
최대 **4대**의 AFI920 이미징 레이더에 접속해 RDI(포인트 클라우드) / SHII(상태) /
SPI(성능) 스트림을 하나의 줄단위 JSON(`.jsonl`) 파일로 녹화합니다. GUI 없음.
여기서 녹화한 파일은 Python·C++ **두 viewer 모두에서** 재생됩니다.

### 사전 준비
- Python 3.8+ (`python3`). 표준 라이브러리만 사용(numpy 불필요).
- 레이더가 네트워크에 연결되어 있어야 함(기본 서브넷 `192.168.10.x`).
- 레포 내부에서 실행하면 SDK(`<repo>/python/src`)와 수신 레이어(`<repo>/logger`)를
  자동으로 찾습니다.

### 실행
```bash
# 리눅스 런처(권장)
./run.sh record 192.168.10.150 --duration 30

# 또는 직접 실행
python3 record_cli.py record 192.168.10.150 --duration 30
```

### 명령어 & 인자
`record` 가 서브커맨드입니다. **타깃**은 `IP` 또는 `IP:PORT` 형식이며, `PORT`는
레이더의 **RDI 포트**(기본 `30509`)입니다. SHII/SPI는 `PORT+1`/`PORT+2`를 씁니다.

| 옵션 | 의미 |
|------|------|
| `IP[:PORT] ...` / `--radar IP[:PORT]` | 레이더 타깃(최대 4대) |
| `--config FILE` | JSON 설정 파일. **CLI에 타깃이 없을 때만** 사용 |
| `--streams rdi shii spi` | 녹화할 스트림(기본: 3종 전부) |
| `--transport tcp\|udp` | 전송 방식(기본 `tcp`) |
| `--host-ip IP` | 레이더가 UDP를 쏘아줄 호스트 IP(`udp`일 때 **필수**) |
| `--duration N` | N초 녹화(`0` = Ctrl+C까지) |
| `--outdir DIR` | 출력 폴더(기본 `./recordings`) |
| `--out NAME` | 출력 파일명(기본 `rec_<시각>.jsonl`) |
| `--no-configure` | 센서 설정을 건드리지 않음(이미 설정됐다고 가정) |

### 설정 파일 (`radars.json`)
`record` 를 CLI 타깃 **없이** 실행하면 이 파일을 읽습니다:
```json
{
  "transport": "tcp",
  "streams": ["rdi", "shii", "spi"],
  "radars": [
    { "ip": "192.168.10.150", "port": 30509 },
    { "ip": "192.168.10.151", "port": 30509 }
  ]
}
```
CLI 플래그가 설정 파일 값보다 우선합니다.

### 예시
```bash
./run.sh record 192.168.10.150 192.168.10.151:30509 --streams rdi shii spi
./run.sh record --radar 192.168.10.150 --radar 192.168.10.151 --duration 60
./run.sh record --config radars.json
./run.sh record 192.168.10.150 --transport udp --host-ip 192.168.10.10
```

### 출력
`recordings/rec_<시각>.jsonl` — 1번째 줄은 헤더(센서/장착 pose/transport), 이후
한 줄당 `rdi`/`shii`/`spi` 레코드. 재생:
```bash
../python_viewer/run.sh --replay recordings/rec_XXXX.jsonl   # Python viewer
../cpp_viewer/run.sh    --replay recordings/rec_XXXX.jsonl   # C++ viewer
```

### 문제 해결
- **TCP인데 0 프레임** — 대부분 *다른 프로그램이 레이더의 TCP 클라이언트 슬롯(1개)을
  이미 점유* 중이라는 뜻입니다(예: 켜져 있는 viewer). 그 프로그램을 닫고 녹화하세요.
  UDP로 바꿔야 한다는 뜻이 **아닙니다**.
- **UDP 실행 후 TCP 포트가 거부됨** — `--transport udp`는 Config API로 레이더를 UDP
  push로 바꿉니다. TCP 모드로 한 번 실행하면 다시 TCP로 되돌아갑니다.
- **UDP는 `--host-ip` 필요** — 레이더 서브넷에서 이 호스트의 IP를 지정하세요.
