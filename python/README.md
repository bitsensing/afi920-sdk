# AFI920 Python SDK

Python SDK for bitsensing AFI920 4D Imaging Radar (interface v1.1.0).
Python 3.8+, standard library only.

## Installation

```bash
pip install .
```

For development (editable install):

```bash
pip install -e .
```

## Basic Usage

```python
from afi_sdk import AfiSdk, AfiSdkConfig

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))
info = sdk.get_device_info()
print(info["serial_number"], info["software_version"])
sdk.close()
```

`AfiSdk.discover(timeout_s=3.0, bind_ip="")` returns one dict per sensor, with the
responding address in `_source_ip`. Discovery runs over UDP and a response can be
missed; run it again if a sensor does not appear.

## Changing a Setting

Every config SET requires the diagnostic access mode. The radar stores the value in NVM
before it answers, and the value takes effect after `restart()`.

```python
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, RangeMode, OK

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))

if sdk.enter_mode(AccessMode.DIAGNOSTIC) != OK:
    print(sdk.last_result)
elif sdk.set_range_mode(RangeMode.LONG_RANGE) == OK:
    sdk.restart()
    sdk.reconnect()      # waits until the radar accepts the connection again
```

Entry is one way (`NORMAL` → `DIAGNOSTIC` → `PROGRAMMING`); a reboot (or the radar's own
communication-stack recovery) returns the radar to `NORMAL`. The programming mode halts the data streams until the reboot.

## Errors

Methods that return `int` return an error code; methods that return data return `None`
on failure. The codes have the same values as the C++ `afi::AfiError`:

| Constant | Value | Constant | Value |
|----------|-------|----------|-------|
| `OK` | 0 | `SENSOR_ERROR` | -8 |
| `NOT_INITIALIZED` | -1 | `INTERNAL_ERROR` | -9 |
| `CONNECT_FAILED` | -3 | `NOT_SUPPORTED` | -10 |
| `TIMEOUT` | -4 | `BUSY` | -11 |
| `DISCONNECTED` | -5 | `SESSION_REQUIRED` | -12 |
| `INVALID_PARAM` | -6 | `ACCESS_DENIED` | -13 |
| `PROTOCOL_ERROR` | -7 | `INVALID_STATE` | -14 |

`sdk.last_error` holds the code of the last call, and `sdk.last_result` holds the radar's
`return_code`, `return_name`, `error_message` (firmware update methods), `data`, and the
response E2E verdict `e2e_ok`.
A timeout, a mismatched response, or a lost connection closes the Config API socket, and
the next call reconnects.

## Firmware Update

`afi_sdk.firmware` runs the whole update sequence on a connected `AfiSdk`: it enters the
programming mode, uploads each component (resuming at `received_bytes` when the radar
accepts a chunk in part), switches the slot face, and reboots. The `"firmware"` domain
needs all four components (`m7_0`, `m7_1`, `m7_2`, `a53`); the bootloader image is
optional.

```python
from afi_sdk import AfiSdk, AfiSdkConfig, firmware

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))
uptime = firmware.radar_uptime("192.168.10.150")  # Discovery uptime before the update

images = {}
for name in firmware.FIRMWARE_COMPONENTS:
    with open(f"{name}.bin", "rb") as f:
        images[name] = f.read()

result = firmware.update(sdk, firmware=images)
if not result.ok:
    print(result.error)
else:
    firmware.wait_for_restart("192.168.10.150", uptime)
    sdk.reconnect()
    print(firmware.verify(sdk, result.target_face) or "verified")
```

`upload_component()` transfers one component for callers that build their own sequence.

## Data Streams

Data stream parsing helpers (20B E2E header with CRC-32, bounded SOME/IP-TP, and
ISO 23150 RDI/SHII/SPI) live in `afi_sdk.streams`. `parse_shi()` and `parse_spi()`
return the whole Common Header, including `data_qualifier`. See
`examples/sample_data_stream.py`.

## Examples

| File | Description |
|------|-------------|
| `examples/sample_discovery.py` | Discover sensors on the network via UDP broadcast |
| `examples/sample_pointcloud.py` | Receive RDI point cloud data with detection details |
| `examples/sample_data_stream.py` | Receive RDI/SHII/SPI data streams simultaneously |
| `examples/sample_config.py` | Read all sensor configuration categories and write values back |
| `examples/sample_multi_sensor.py` | Multi-sensor setup with per-sensor port assignment |
| `examples/set_yaw_90.py` | Write the mounting yaw in the diagnostic mode and restart to apply |

For full documentation, see [README.md](../README.md) | [API Reference](../docs/api_reference.md)

## License

BSD-3-Clause
