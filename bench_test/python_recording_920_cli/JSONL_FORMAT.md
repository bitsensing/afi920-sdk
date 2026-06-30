# AFI920 unified recording format — `afi-radar-jsonl` (v1)

A single line-delimited JSON (**JSONL**) container shared by every recording / replay
tool in this repo:

| program | folder | records | replays |
|---|---|---|---|
| Python CLI recorder | `python_recording_920_cli/` | ✅ | — |
| C++ CLI recorder | `cpp_recording_920_cli/` | ✅ | — |
| Python 3D viewer | `viewer/` | ✅ | ✅ |
| C++ 3D viewer | `cpp_viewer/` | ✅ | ✅ |

Because the format is plain UTF-8 JSON text and stores **parsed sensor-frame fields**
(not raw ISO bytes and not world-frame XYZ), a file recorded by *any* program — Python
or C++, CLI or viewer — replays in *either* viewer. The detection geometry is stored in
the sensor frame (`r`, `az`, `el`); replay re-applies the per-sensor mounting pose from
the header, exactly like a live capture.

## File layout

* UTF-8, one JSON object per line, `\n`-separated.
* **Line 1 = header** (`"type":"header"`).
* **Lines 2..N = data records** (`"t"` ∈ `{rdi, shii, spi}`), in capture order.
* An optional trailing **footer** line (`"type":"footer"`) may carry the final
  `record_count`. Readers ignore any line whose object has neither `t` nor a known
  `type` — so the format tolerates future additions.

### Header (line 1)

```json
{"type":"header","format":"afi-radar-jsonl","version":1,
 "kind":"live","transport":"tcp","started_unix":1782794835.667,
 "sensors":[
   {"index":0,"ip":"192.168.10.150","port":30509,
    "model":"AFI920","serial":"...",
    "pose":{"offset_x_m":0.0,"offset_y_m":0.0,"offset_z_m":0.0,
            "yaw_deg":0.0,"pitch_deg":0.0,"roll_deg":0.0}}
 ]}
```

* `kind` — `live` | `sim` | `replay`.
* `transport` — `tcp` | `udp` | `sim`.
* `sensors[].index` — 0-based; matches the `s` field of every data record.
* `sensors[].pose` — mounting transform read from the Config API (ISO 8855: X fwd,
  Y left, Z up; yaw about Z, pitch about Y, roll about X). May be all-zero if the
  Config API was unreachable.
* `port` — the sensor's RDI/detection port (SHII = port+1, SPI = port+2 by the
  AFI920 default contiguous layout). Informational only; replay does not use it.

### RDI record (point cloud)

```json
{"t":"rdi","ts":1782794835.702,"s":0,"counter":5896,"ts_ns":1716847114000034390,
 "n":5,"claimed":5,"r":[...],"az":[...],"el":[...],"v":[...],"rcs":[...],"snr":[...]}
```

* `ts` — host receive time, Unix epoch **seconds** (float). Replay timing uses
  `ts - ts_of_first_record`.
* `s` — sensor index (into the header `sensors` list).
* `ts_ns` — sensor frame timestamp, **nanoseconds** (integer; full uint64 precision).
* `n` — detection count = length of each parallel array.
* `claimed` — the sensor header's reported detection count. Equals `n` normally;
  `claimed > n` marks a truncated frame (a lost UDP TP segment). Optional —
  readers default it to `n` when absent.
* Parallel arrays, all length `n`, in the **sensor frame**:
  * `r` — radial distance (m)
  * `az` — azimuth (rad), + from X toward Y
  * `el` — elevation (rad), + up
  * `v` — radial velocity (m/s)
  * `rcs` — radar cross section (dBsm)
  * `snr` — signal-to-noise ratio (dB)

### SHII record (sensor health)

```json
{"t":"shii","ts":...,"s":0,"counter":N,
 "defect":0,"reason":0,"supply":2,"temp":2,"time_sync":0,
 "modes":[0],"cal":[0,0,0]}
```

Raw ISO 23150 SHII enum values (decoded to labels at display time):

* `defect` — sensor_defect_recognised (0=fully functional)
* `reason` — sensor_defect_reason (0=none)
* `supply` — supply_voltage_status (2=within limits)
* `temp` — sensor_temperature_status (2=in limits)
* `time_sync` — sensor_time_sync (0=within limits)
* `modes[]` — sensor_operation_modes
* `cal[]` — sensor_calibration_statuses (0=calibrated)

### SPI record (sensor performance)

```json
{"t":"spi","ts":...,"s":0,"counter":N,
 "pose":[ox,oy,oz,yaw,pitch,roll,yaw_err,pitch_err],
 "fov":[{"az0":-1.0,"az1":1.0,"el0":-0.3,"el1":0.3,"blockage":0}]}
```

* `pose` — 8 floats: origin x/y/z (m), orientation yaw/pitch/roll (rad),
  orientation error yaw/pitch (rad).
* `fov[]` — FoV segments: azimuth/elevation begin/end (rad) + `blockage`
  (0=none, 1=partial, 2=full, 3=unknown).

## Notes for implementers

* Integers (`counter`, `ts_ns`, enum values) are written as JSON integer literals so
  Python (`int`) and nlohmann/json (`int64`/`uint64`) preserve full precision; only a
  JS `Number` would lose `ts_ns`.
* Floats are written with round-trip precision by both `json` (Python) and
  `nlohmann::json::dump` (C++).
* The writer must be thread-safe: in TCP mode each (sensor, stream) is received on its
  own thread and they share one recorder.
* The authoritative writer/reader implementations are
  [`afi_jsonl.py`](afi_jsonl.py) (Python) and `cpp_recording_920_cli/jsonl_writer.*`
  / `cpp_viewer/jsonl_recording.*` (C++).
