# bench_test — AFI920 viewer & recording tools

Bench tooling built on top of the AFI920 SDK (this repo). All four tools share one
unified recording format, [`afi-radar-jsonl`](python_recording_920_cli/JSONL_FORMAT.md)
(`.jsonl`), so a file recorded by **any** tool replays in **either** viewer.

| Folder | What | Run (Linux) | Manual |
|--------|------|-------------|--------|
| [`python_viewer/`](python_viewer/) | Python 3D viewer (live/record/replay/sim) | `python_viewer/run.sh` | [USER_MANUAL](python_viewer/USER_MANUAL.md) |
| [`cpp_viewer/`](cpp_viewer/) | C++/Qt 3D viewer | `cpp_viewer/run.sh` | [USER_MANUAL](cpp_viewer/USER_MANUAL.md) |
| [`python_recording_920_cli/`](python_recording_920_cli/) | Python CLI recorder (no GUI) | `python_recording_920_cli/run.sh` | [USER_MANUAL](python_recording_920_cli/USER_MANUAL.md) |
| [`cpp_recording_920_cli/`](cpp_recording_920_cli/) | C++ CLI recorder (no GUI) | `cpp_recording_920_cli/run.sh` | [USER_MANUAL](cpp_recording_920_cli/USER_MANUAL.md) |

Each `run.sh` `cd`s to its own folder, so you can launch it from anywhere:

```bash
# record 30 s from one radar, then replay it in either viewer
bench_test/python_recording_920_cli/run.sh record 192.168.10.150 --duration 30
bench_test/cpp_viewer/run.sh --replay python_recording_920_cli/recordings/rec_XXXX.jsonl
```

The C++ `run.sh` scripts build the binary on first use (need CMake + a C++17
compiler; the viewer also needs Qt 5.15+/6). The Python tools need Python 3.8+
(viewer also needs `numpy`, `PyQt5`, `pyqtgraph`, `PyOpenGL`).

> Note: the shared receive layer lives at `bench_test/logger/` (alongside the
> tools); the SDK (`python/`, `include/`, `src/`, `third_party/`) lives at the
> **repo root**. The tools locate both automatically.
