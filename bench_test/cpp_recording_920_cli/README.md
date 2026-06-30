# AFI920 CLI Recorder (C++)

Record one or more AFI920 imaging radars to a single unified `.jsonl` file using
the C++ SDK — **no GUI**. The output replays in either the Python (`viewer/`) or
C++ (`cpp_viewer/`) 3D viewer; every tool in this repo shares the
[`afi-radar-jsonl`](../python_recording_920_cli/JSONL_FORMAT.md) format.

## Build

Same toolchain as `cpp_viewer/` (MinGW + CMake + Ninja); no Qt needed.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
cmake --build build
# -> build/afi_recorder.exe
```

## Usage

```bash
# one radar (Config API streaming over TCP already)
afi_recorder record 192.168.10.150

# up to 4 radars, explicit ports, all streams, stop after 30 s
afi_recorder record 192.168.10.150 192.168.10.151:30509 \
    --streams rdi shii spi --duration 30

# repeatable --radar form
afi_recorder record --radar 192.168.10.150 --radar 192.168.10.151

# no target on the CLI -> read radars.json (ip + port)
afi_recorder record --config radars.json
```

`record` is the command. A target is `IP` or `IP:PORT`, where `PORT` is the
radar's **RDI port** (default `30509`); SHII and SPI use `PORT+1` / `PORT+2`.
Up to **4** radars at once.

| option | meaning |
|--------|---------|
| `IP[:PORT] ...` / `--radar IP[:PORT]` | radar target(s), up to 4 |
| `--config FILE` | JSON config; used only when no target is on the CLI |
| `--streams rdi shii spi` | which streams to record (default: all three) |
| `--transport tcp\|udp` | transport (default `tcp`) |
| `--host-ip IP` | host IP the radars push to (required for `udp`) |
| `--duration N` | record N seconds (`0` = until Ctrl+C) |
| `--outdir DIR` | output directory (default `./recordings`) |
| `--out NAME` | output filename (default `rec_<timestamp>.jsonl`) |
| `--no-configure` | don't use the Config API (passive receive; pose = 0) |

The config file (`radars.json`) has the same schema as the Python CLI recorder.

## Output

`recordings/rec_<timestamp>.jsonl` — line 1 header (sensors + poses + transport),
then one `rdi` / `shii` / `spi` record per line. Replay it:

```bash
../cpp_viewer/run.sh    --replay recordings/rec_XXXX.jsonl
# or
../python_viewer/run.sh --replay recordings/rec_XXXX.jsonl
```
