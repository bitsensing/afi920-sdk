# AFI920 CLI Recorder (Python)

Record one or more AFI920 imaging radars to a single unified `.jsonl` file — **no
GUI**. The output replays in either the Python (`viewer/`) or C++ (`cpp_viewer/`)
3D viewer, because every tool in this repo shares the
[`afi-radar-jsonl`](JSONL_FORMAT.md) format.

## Usage

```bash
# one radar (Config API already streaming over TCP -> use --no-configure)
python record_cli.py record 192.168.10.150 --no-configure

# up to 4 radars, explicit ports, all three streams, stop after 30 s
python record_cli.py record 192.168.10.150 192.168.10.151:30509 \
    --streams rdi shii spi --duration 30

# repeatable --radar form
python record_cli.py record --radar 192.168.10.150 --radar 192.168.10.151

# no target on the CLI -> read radars.json (ip + port)
python record_cli.py record --config radars.json
```

`record` is the command. A target is `IP` or `IP:PORT`, where `PORT` is the
radar's **RDI port** (default `30509`); SHII and SPI are `PORT+1` / `PORT+2`
(the AFI920 default contiguous layout). Up to **4** radars at once.

### Options

| option | meaning |
|---|---|
| `IP[:PORT] ...` / `--radar IP[:PORT]` | radar target(s), up to 4 |
| `--config FILE` | JSON config; used only when no target is on the CLI |
| `--streams rdi shii spi` | which streams to record (default: all three) |
| `--transport tcp\|udp` | transport (default `tcp`) |
| `--host-ip IP` | host IP the radars push to (required for `udp`) |
| `--duration N` | record N seconds (`0` = until Ctrl+C) |
| `--outdir DIR` | output directory (default `./recordings`) |
| `--out NAME` | output filename (default `rec_<timestamp>.jsonl`) |
| `--no-configure` | don't touch sensor config (assume streams are up) |

### Config file (`radars.json`)

Used when you run `record` with no target on the command line:

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

CLI flags (`--transport`, `--streams`, `--host-ip`) override the config values.
Unreachable radars simply retry connecting in the background.

## Output

`recordings/rec_<timestamp>.jsonl` — line 1 is a header (sensors, poses,
transport); each following line is one `rdi` / `shii` / `spi` record. Open it in
a viewer with `--replay`, e.g.:

```bash
../python_viewer/run.sh --replay recordings/rec_20260630_140000.jsonl
```

Reuses the hardened TCP/UDP receive layer from `../logger/radar_logger.py` and the
`afi_sdk` stream parsers. Stdlib only (no numpy).
