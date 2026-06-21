# AFI920 Python SDK

Python SDK for bitsensing AFI920 4D Imaging Radar.

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

Data stream parsing helpers (E2E Profile 7 + ISO 23150 v3.0) live in
`afi_sdk.streams`; see `examples/sample_pointcloud.py`.

## Examples

| File | Description |
|------|-------------|
| `examples/sample_discovery.py` | Discover sensors on the network via UDP broadcast |
| `examples/sample_pointcloud.py` | Receive RDI point cloud data with detection details |
| `examples/sample_data_stream.py` | Receive RDI/SHII/SPI data streams simultaneously |
| `examples/sample_config.py` | Read/write all sensor configuration categories |
| `examples/sample_multi_sensor.py` | Multi-sensor setup with per-sensor port assignment |

For full documentation, see [README.md](../README.md) | [API Reference](../docs/api_reference.md)

## License

BSD-3-Clause
