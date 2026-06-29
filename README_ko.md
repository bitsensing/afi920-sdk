# AFI920 SDK

> **[English](README.md)**

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-blue)
![Python](https://img.shields.io/badge/python-3.8%2B-blue)
![License](https://img.shields.io/badge/license-BSD--3--Clause-green)

bitsensing **AFI920 4D Imaging Radar**용 C++ 및 Python SDK입니다.  
AFI920 센서를 탐색하고, RDI/SHII/SPI 데이터를 수신하고, CSII 차량 정보를 송신하며, 주요 설정 API를 사용할 수 있도록 구성했습니다.

> **Version 2.1.0** — AFI920 프로토콜 v3.0(Radar SW 3.0.0)을 지원합니다. v3.0 펌웨어 센서에 사용하세요. [CHANGELOG.md](CHANGELOG.md)를 참고하세요.

> **bitsensing** — [bitsensing.com](https://bitsensing.com)

---

## Scope

- AFI920 고객의 C++ 및 Python 통합용 공개 SDK 레포지토리입니다.
- 이 README는 센서 탐색, 데이터 수신, 주요 설정 API 사용 흐름을 빠르게 확인할 수 있도록 구성했습니다.
- 제품 매뉴얼, 인터페이스 명세, 설치/운영 상세 가이드는 별도 제공 문서에서 확인할 수 있습니다.

---

## Table of Contents

- [Supported Platforms](#supported-platforms)
- [Quick Start](#quick-start)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Building (C++ SDK)](#building-c-sdk)
- [Installation (Python SDK)](#installation-python-sdk)
- [Network Configuration](#network-configuration)
- [Examples](#examples)
- [Multi-Sensor](#multi-sensor)
- [Testing](#testing)
- [Data Types](#data-types)
- [Architecture](#architecture)
- [Troubleshooting](#troubleshooting)
- [Documentation](#documentation)
- [Security Notice](#security-notice)
- [Related Projects](#related-projects)
- [Changelog](#changelog)
- [License](#license)
- [Support](#support)

---

## Supported Platforms

| SDK | 지원 플랫폼 | 최소 요구사항 |
|-----|-------------|---------------|
| C++ SDK | Linux / Windows | C++17, CMake 3.16+ |
| Python SDK | Linux / Windows | Python 3.8+ |

---

## Quick Start

### C++

아래 예시는 센서 1대를 탐색한 뒤 연결하여 검출 개수를 확인하는 최소 예시입니다.

```cpp
#include <afi/afi_sdk.h>

int main() {
    // 센서 탐색
    auto sensors = afi::AfiSdk::Discover();
    if (sensors.empty()) return 1;

    // 연결 및 데이터 수신
    afi::AfiSdkConfig config;
    config.sensor_ip = sensors[0].ip;

    afi::AfiSdk sdk;
    if (sdk.Init(config) != 0) return 1;

    sdk.RegRecvCallback([](const afi::AfiRdiFrame& frame) {
        printf("Detections: %d\n", frame.message.num_detections);
    });

    sdk.Start();

    // ... 애플리케이션 루프 ...

    sdk.Stop();
}
```

### Python

아래 예시는 센서 1대에 연결한 뒤 기본 센서 정보를 조회하는 최소 예시입니다.

```python
from afi_sdk import AfiSdk, AfiSdkConfig

# 센서 탐색
sensors = AfiSdk.discover(timeout_s=3.0)
for s in sensors:
    print(f"{s['_source_ip']} — {s.get('serial_number', '?')}")

# 연결 및 센서 정보 조회
sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))

info = sdk.get_device_info()
if info:
    print(f"Serial: {info['serial_number']}")
    print(f"SW Version: {info['software_version']}")

sdk.close()
```

예상 출력:
```
192.168.10.150 — AFI920-001234
Serial: AFI920-001234
SW Version: 3.0.0
```

> **팁:** NIC(네트워크 인터페이스)가 여러 개인 환경에서는 Discovery 인터페이스를 명시적으로 지정하세요. C++에서는 `Discover(3000, 30520, "192.168.10.100")`, Python에서는 `discover(bind_ip="192.168.10.100")`를 사용할 수 있습니다.

---

## Features

- **Discovery**: UDP 브로드캐스트를 통해 네트워크상의 AFI920 센서를 자동 탐색
- **Data Streaming**: RDI(Point Cloud), SHII(Health), SPI(Performance) 데이터를 실시간으로 수신
- **Vehicle Input (CSII)**: 차량 동역학 정보(속도, 요레이트, 기어 등)를 TCP로 센서에 송신
- **E2E Protection**: 모든 데이터 스트림에 AUTOSAR E2E Profile 7(CRC-64/XZ) 검증 적용
- **Config API**: TCP 기반 SOME/IP + JSON으로 주요 센서 설정 조회 및 변경
- **Multi-Sensor**: 센서별 독립 인스턴스로 다중 센서 운용 가능
- **ISO 23150**: 표준 차량용 센서 데이터 인터페이스 기반
- **Cross-Platform**: Linux 및 Windows 환경 지원

---

## Prerequisites

### 컴파일러 및 도구

| 항목 | C++ SDK | Python SDK |
|------|---------|------------|
| 언어 | C++17 (GCC 7+, Clang 5+, MSVC 2017+) | Python 3.8+ |
| 빌드 도구 | CMake 3.16+ | pip |
| 의존성 | 없음. `nlohmann/json` 3.12.0이 번들로 포함됩니다. | 없음. 표준 라이브러리만 사용합니다. |

### 네트워크 준비

AFI920 센서는 Ethernet으로 통신합니다. 호스트 PC는 센서와 동일한 서브넷에 있어야 합니다.

| 항목 | 값 |
|------|----|
| 센서 기본 IP | `192.168.10.150` |
| 호스트 IP | `192.168.10.x` (예: `192.168.10.100`) |
| 서브넷 마스크 | `255.255.255.0` |

**Linux 예시**
```bash
# 센서에 연결된 NIC에 IP 할당
sudo ip addr add 192.168.10.100/24 dev eth0
```

**Windows**  
네트워크 어댑터 속성 → IPv4 → 수동 IP 설정 (`192.168.10.100`, `255.255.255.0`)

---

## Building (C++ SDK)

프로토콜 인터페이스 헤더는 이 저장소에 포함되어 있으므로(`third_party/afi920_interface/` 하위) 별도의 submodule 초기화가 필요 없습니다. 클론 후 바로 빌드하세요.

### Linux

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Windows (Visual Studio 2022)

**Developer Command Prompt**
```cmd
mkdir build && cd build
cmake -G "NMake Makefiles" .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

**Visual Studio Generator**
```cmd
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

**Ninja Generator**
```cmd
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### CMake 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `BUILD_EXAMPLES` | `ON` | 예제 프로그램 빌드 |
| `BUILD_TESTS` | `OFF` | 테스트 프로그램 빌드 |
| `CMAKE_BUILD_TYPE` | — | `Release`, `Debug`, `RelWithDebInfo` |

### 빌드 산출물

| 플랫폼 | 출력 파일 | 경로 |
|---------|-----------|------|
| Linux | `libafi_sdk.a` | `build/src/libafi_sdk.a` |
| Windows | `afi_sdk.lib` | `build/src/afi_sdk.lib` |

### CMake 통합

상위 프로젝트의 서브디렉터리로 추가하여 통합합니다.

```cmake
add_subdirectory(afi920_sdk)
target_link_libraries(your_app PRIVATE afi_sdk)
```

### 플랫폼별 참고사항

- **Windows:** `ws2_32`에 자동으로 링크되며 Winsock 초기화는 내부적으로 처리됩니다.
- **Linux:** `pthread`에 자동으로 링크됩니다.

---

## Installation (Python SDK)

```bash
cd python/
pip install .
```

개발용 설치:
```bash
pip install -e .
```

설치 확인:
```bash
python -c "import afi_sdk; print(afi_sdk.__version__)"
# 출력: 2.1.0
```

---

## Network Configuration

### 기본 포트

| 포트 | 프로토콜 | 방향 | Event ID | 설명 |
|------|----------|------|----------|------|
| 30500 | TCP | 호스트 ↔ 센서 | — | Config API (SOME/IP + JSON, Method ID `0x0010`~`0x0092`) |
| 30501 | TCP | 호스트 → 센서 | `0x8001` | CSII (Vehicle Input) |
| 30509 | TCP/UDP | 센서 → 호스트 | `0x8002` | RDI (Radar Detections / Point Cloud) |
| 30510 | TCP/UDP | 센서 → 호스트 | `0x8003` | SHII (Sensor Health Information) |
| 30511 | TCP/UDP | 센서 → 호스트 | `0x8004` | SPI (Sensor Performance Information) |
| 30520 | UDP | 호스트 ↔ 센서 | — | Discovery (Broadcast) |

> **참고:** 기본 데이터 스트림 전송 프로토콜은 TCP이며, 필요 시 `AfiSdkConfig`의 `stream_transport`로 변경할 수 있습니다.

> **E2E Protection:** 모든 RDI/SHII/SPI/CSII 페이로드는 SOME/IP 헤더와 ISO 23150 페이로드 사이에 20바이트 AUTOSAR E2E Profile 7 헤더(CRC-64/XZ)를 포함합니다. SDK는 수신 시 CRC를 자동 검증하며(`validate_e2e`, 기본 활성), `e2e_strict = true`로 설정하면 CRC가 일치하지 않는 프레임을 경고와 함께 전달하는 대신 폐기합니다.

---

## Examples

### C++ 예제

| 프로그램 | 설명 |
|----------|------|
| `sample_discovery` | 네트워크에서 센서 탐색 |
| `sample_pointcloud` | RDI Point Cloud 데이터 수신 |
| `sample_data_stream` | RDI/SHII/SPI 데이터 동시 수신 |
| `sample_config` | 센서 설정 읽기/쓰기 |
| `sample_multi_sensor` | 다중 센서 데이터 수신 |

소스 코드: [examples/cpp/](examples/cpp/)

### Python 예제

| 프로그램 | 설명 |
|----------|------|
| `sample_discovery.py` | 네트워크에서 센서 탐색 |
| `sample_data_stream.py` | RDI/SHII/SPI 데이터 동시 수신 |
| `sample_config.py` | 센서 설정 읽기/쓰기 |
| `sample_multi_sensor.py` | 다중 센서 데이터 수신 |

소스 코드: [python/examples/](python/examples/)

---

## Multi-Sensor

AFI920 SDK는 최대 **4개 센서**의 동시 운용을 지원합니다. 센서마다 하나의 `AfiSdk` 인스턴스를 생성하며, 각 인스턴스는 연결과 데이터 수신을 독립적으로 관리합니다.

### 포트 할당

호스트에서 포트 충돌을 방지하려면 각 센서가 고유한 데이터 포트를 사용해야 합니다. 센서당 기본 오프셋 `+100`으로 포트를 할당하는 방식을 권장합니다. (Config API는 각 센서의 IP, 포트 30500으로 나가는 TCP 연결이므로 오프셋이 필요 없습니다.)

| 센서 | RDI 포트 | SHII 포트 | SPI 포트 |
|------|----------|-----------|----------|
| 센서 1 | 30509 | 30510 | 30511 |
| 센서 2 | 30609 | 30610 | 30611 |
| 센서 3 | 30709 | 30710 | 30711 |
| 센서 4 | 30809 | 30810 | 30811 |

센서가 올바른 호스트 포트로 데이터를 전송하도록 `SetStreamDestination`(또는 `SetNetworkConfig`)으로 목적지 IP/포트를 설정하세요.

다중 센서 예제는 [`sample_multi_sensor.cpp`](examples/cpp/sample_multi_sensor.cpp) 및 [`sample_multi_sensor.py`](python/examples/sample_multi_sensor.py)를 참고하세요.

---

## Testing

### 단위 테스트

대표 실행 예시:

```bash
cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
ctest --output-on-failure
```

### 통합 테스트

대표 실행 예시:

```bash
./test/test_discovery
./test/test_config_api 192.168.10.150
./test/test_data_receive 192.168.10.150 60
```

> **Windows:** 실행 파일 이름에 `.exe`를 사용하세요.

---

## Data Types

프로토콜 데이터는 `third_party/afi920_interface/src/c` 헤더에 정의된 ISO 23150 C 구조체와 SDK 래퍼 타입을 사용합니다.

### 핵심 구조체

| 구조체 | 설명 |
|--------|------|
| `AfiRdiFrame` | SDK 콜백에서 전달되는 RDI 프레임 |
| `bts_rdi_detection_t` | 개별 detection(51바이트)으로, 위치, 속도, 측정 오차 정보를 포함 |
| `bts_shi_message_t` | 센서 상태 정보 (가변 길이) |
| `bts_spi_message_t` | 센서 성능 정보 |
| `AfiVehicleInput` | `SendVehicleInput`(CSII)으로 센서에 송신하는 차량 동역학 정보 |

### `bts_rdi_detection_t` 주요 필드

| 필드 | 타입 | 설명 |
|------|------|------|
| `position_radial_distance` | `float` | 거리 (m) |
| `position_azimuth` | `float` | 수평각 (rad) |
| `position_elevation` | `float` | 수직각 (rad) |
| `relative_velocity_radial` | `float` | 시선 방향 속도 (m/s) |
| `radar_cross_section` | `float` | RCS (dBsm) |
| `signal_to_noise_ratio` | `float` | SNR (dB) |
| `existence_probability` | `uint8_t` | 존재 확률 (0~100%) |
| `object_id_reference` | `uint16_t` | 연계 Object ID (예약) |
| `position_*_error`, `relative_velocity_radial_error` | `float` | 측정 오차 (거리/수평각/수직각/속도) |

프레임당 최대 **4096개 detection**(36바이트 헤더 + detection당 51바이트)을 포함합니다. 큰 RDI 프레임은 SOME/IP-TP로 분할 전송되며 SDK가 자동으로 재조립합니다.

> **중요:** 콜백으로 전달되는 `AfiRdiFrame`의 detection 데이터는 콜백 실행 중에만 유효합니다. 콜백 범위 밖에서 사용하려면 반드시 복사하세요.

전체 구조체 정의와 응답 타입은 [docs/api_reference.md](docs/api_reference.md) 및 [third_party/afi920_interface/src/c](third_party/afi920_interface/src/c) 헤더를 참고하세요.

---

## Architecture

```text
AfiSdk
  ├── Discovery
  ├── Config API Client
  ├── Data Stream Receivers (RDI / SHII / SPI)
  │     └── E2E Profile 7 Validation (CRC-64/XZ)
  ├── CSII Vehicle Input Sender (Host → Radar)
  └── Protocol Parsers
```

- 각 `AfiSdk` 인스턴스는 독립적으로 동작합니다.
- 다중 센서 운용 시 센서마다 하나의 인스턴스를 생성합니다.
- 데이터 콜백은 수신 스레드에서 호출되므로, 콜백 내부 처리는 가볍게 유지하거나 별도 처리 스레드로 넘기는 것이 좋습니다.
- `SendVehicleInput()`은 호출자 스레드에서 실행되며, 첫 호출 시 CSII 포트에 지연 연결(lazy connect)합니다.

---

## Troubleshooting

| 증상 | 원인 | 해결 방법 |
|------|------|-----------|
| `Init()` → `kConnectFailed` | 센서에 연결할 수 없음 | 센서 IP, 네트워크 케이블, 서브넷 설정 확인 |
| Config API는 동작하지만 데이터가 수신되지 않음 | 스트림 목적지 설정 불일치 | `GetNetworkConfig`로 목적지 IP/포트/프로토콜 확인 |
| 로그에 E2E CRC 불일치 경고 발생 | 프레임 손상 또는 v3.0 미만 센서 펌웨어 | 센서가 v3.0 펌웨어인지 확인, `e2e_strict = true`로 불량 프레임 폐기 가능 |
| 높은 패킷 손실률 | UDP 수신 버퍼 부족 | `AfiSdkConfig`의 `socket_buffer_size` 증가 (C++ SDK) |

전체 에러 코드와 복구 방법은 [API Reference](docs/api_reference.md#error-codes)를 참고하세요.

---

## Documentation

| 문서 | 설명 |
|------|------|
| [API Reference](docs/api_reference.md) | C++ 및 Python 공개 API와 주요 타입 레퍼런스 |
| [Python SDK](python/README.md) | Python SDK 빠른 참조 |
| [CHANGELOG](CHANGELOG.md) | 릴리스 이력 |

---

## Security Notice

AFI920 SDK는 센서와 평문 TCP/UDP 프로토콜로 통신하며, TLS/DTLS 같은 암호화 또는 인증 메커니즘은 포함하지 않습니다.

- 센서 네트워크는 전용 VLAN 또는 물리적으로 격리된 네트워크에 구성하는 것을 권장합니다.
- 센서 데이터가 흐르는 네트워크 구간에 대한 접근을 제한하세요.
- 신뢰되지 않은 네트워크 환경에서는 VPN, 방화벽 등 추가 보안 조치를 적용하세요.

---

## Related Projects

| 프로젝트 | 설명 |
|----------|------|
| [afi920-ros2-driver](https://github.com/bitsensing/afi920-ros2-driver) | AFI920 ROS 2 드라이버 패키지 |

---

## Changelog

전체 릴리스 이력은 [CHANGELOG.md](CHANGELOG.md)를 참고하세요.

---

## License

BSD-3-Clause

Copyright (c) 2025-2026 bitsensing Inc.

전체 라이선스 문구는 [LICENSE](LICENSE)를 참고하세요. 서드파티 라이선스 고지는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)에 기재되어 있습니다.

---

## Support

- **GitHub Issues**: 공개 SDK 사용 중 재현 가능한 버그, 문서 오탈자, 개선 제안 ([github.com/bitsensing/afi920-sdk/issues](https://github.com/bitsensing/afi920-sdk/issues))
- **기술 지원**: AFI920 제품 사용, 고객 환경 통합, 별도 제공 문서 관련 문의 (`tech-support@bitsensing.com`)
