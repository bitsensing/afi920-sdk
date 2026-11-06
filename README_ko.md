# AFI920 SDK

> **[English](README.md)**

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-blue)
![Python](https://img.shields.io/badge/python-3.8%2B-blue)
![License](https://img.shields.io/badge/license-BSD--3--Clause-green)

bitsensing **AFI920 4D Imaging Radar**용 C++ 및 Python SDK이다.  
센서 탐색, RDI/SHII/SPI 데이터 수신, CSII 차량 정보 송신, 접근 모드를 포함한 Config API, 펌웨어 업데이트를 제공한다.

> **Version 2.2.0**은 AFI920 인터페이스 v1.1.0(펌웨어 v3.1.1)을 기준으로 하며, 인터페이스 v1.0.x 센서(펌웨어 3.0.0~3.1.0)의 데이터도 수신한다. 변경 이력은 [CHANGELOG.md](CHANGELOG.md)에서 확인한다.

> **bitsensing** [bitsensing.com](https://bitsensing.com)

---

## Scope

- AFI920 고객 통합을 위한 C++ 및 Python 공개 SDK 저장소이다.
- 센서 탐색, 데이터 수신, 센서 설정, 펌웨어 업데이트의 빠른 시작 절차는 이 README에서 확인한다.
- 제품 매뉴얼, 인터페이스 명세, 설치 및 운영 상세 가이드는 별도 제공 문서에서 확인한다.
- 인터페이스 v1.0.x 센서(펌웨어 3.0.0~3.1.0)는 이번 릴리스에 한해 데이터만 지원한다. 세부 범위는 [Firmware Compatibility](#firmware-compatibility)에서 확인한다.
- `third_party/afi920_interface/src/c`의 프로토콜 인터페이스 헤더는 인터페이스 v1.1.0을 vendoring한 사본이다.

---

## Table of Contents

- [Supported Platforms](#supported-platforms)
- [Quick Start](#quick-start)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Building (C++ SDK)](#building-c-sdk)
- [Installation (Python SDK)](#installation-python-sdk)
- [Network Configuration](#network-configuration)
- [Firmware Compatibility](#firmware-compatibility)
- [Configuration Workflow](#configuration-workflow)
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

아래 예시는 센서 1대를 탐색한 뒤 연결하여 검출 개수를 출력하는 최소 절차이다.

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

아래 예시는 센서를 탐색하고 연결한 뒤 기본 센서 정보를 조회하는 최소 절차이다.

```python
from afi_sdk import AfiSdk, AfiSdkConfig

# 센서 탐색
sensors = AfiSdk.discover(timeout_s=3.0)
for s in sensors:
    print(f"{s['_source_ip']} - {s.get('serial_number', '?')}")

# 연결 및 센서 정보 조회
sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))

info = sdk.get_device_info()
if info:
    print(f"Serial: {info['serial_number']}")
    print(f"SW Version: {info['software_version']}")

sdk.close()
```

예상 출력은 다음과 같다.
```
192.168.10.150 - AFI920-001234
Serial: AFI920-001234
SW Version: 3.1.1
```

> **팁** NIC(네트워크 인터페이스)가 여러 개인 환경에서는 Discovery 인터페이스를 명시적으로 지정한다. C++에서는 `Discover(3000, 30520, "192.168.10.100")`를, Python에서는 `discover(bind_ip="192.168.10.100")`를 사용한다. Discovery 응답은 UDP로 전달되므로 일부가 누락될 수 있으며, 센서가 목록에 없으면 Discovery를 다시 수행한다.

---

## Features

- **Discovery**는 UDP 브로드캐스트로 네트워크상의 AFI920 센서를 탐색한다. 응답은 timeout 구간 1회 동안 수집하며, 송신 IP와 시리얼 번호 기준으로 중복을 제거한다.
- **Data Streaming**은 RDI(Point Cloud), SHII(Health), SPI(Performance)를 TCP 또는 UDP로 실시간 수신한다(C++). Python SDK는 대응하는 파싱 함수를 `afi_sdk.streams`에서 제공한다.
- **Vehicle Input (CSII)**은 차량 동역학 정보(속도, 요레이트, 기어 등)를 센서에 송신한다. 인터페이스 v1.1.0은 UDP, v1.0.x는 TCP를 사용한다.
- **E2E Protection**은 모든 데이터 스트림 메시지와 모든 Config API 요청·응답의 20바이트 E2E 헤더(본문 대상 IEEE 802.3 CRC-32)를 검증한다. 판정 결과는 각 프레임과 함께 전달한다.
- **Config API**는 TCP 기반 SOME/IP + JSON으로 센서 설정을 조회하고 변경하며, 접근 모드 API(`GetMode`, `SetMode`, `EnterMode`)를 포함한다.
- **Firmware Update**는 C++와 Python의 Config API 펌웨어 업데이트 호출과 Python 모듈 `afi_sdk.firmware`의 전체 업데이트 절차로 구성한다.
- **Multi-Sensor**는 센서별 독립 인스턴스로 다중 센서를 운용한다.
- SDK는 **ISO 23150** 차량용 센서 데이터 인터페이스 표준을 따른다.
- **Cross-Platform** 지원 범위는 Linux와 Windows 환경이다.

---

## Prerequisites

### 컴파일러 및 도구

| 항목 | C++ SDK | Python SDK |
|------|---------|------------|
| 언어 | C++17 (GCC 7+, Clang 5+, MSVC 2017+) | Python 3.8+ |
| 빌드 도구 | CMake 3.16+ | pip |
| 의존성 | 없음. `nlohmann/json` 3.12.0을 번들로 포함한다. | 없음. 표준 라이브러리만 사용한다. |

### 네트워크 준비

AFI920 센서는 Ethernet으로 통신한다. 호스트 PC는 센서와 동일한 서브넷에 둔다.

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
네트워크 어댑터 속성의 IPv4 항목에서 수동 IP(`192.168.10.100`, `255.255.255.0`)를 설정한다.

---

## Building (C++ SDK)

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
| `CMAKE_BUILD_TYPE` | (없음) | `Release`, `Debug`, `RelWithDebInfo` |

### 빌드 산출물

| 플랫폼 | 출력 파일 | 경로 |
|---------|-----------|------|
| Linux | `libafi_sdk.a` | `build/src/libafi_sdk.a` |
| Windows | `afi_sdk.lib` | `build/src/afi_sdk.lib` |

### CMake 통합

**서브디렉터리로 사용**

```cmake
add_subdirectory(afi920_sdk)
target_link_libraries(your_app PRIVATE afi_sdk)
```

**설치 후 패키지로 사용**

```bash
cmake --install build --prefix /usr/local     # Visual Studio 생성기에서는 --config Release를 추가한다
```

```cmake
find_package(afi_sdk REQUIRED)                # 기본 경로가 아니면 CMAKE_PREFIX_PATH에 설치 경로를 지정한다
target_link_libraries(your_app PRIVATE afi::afi_sdk)
```

설치 결과물은 정적 라이브러리, `include/afi` 헤더, `include/afi920_interface` 아래의 프로토콜 인터페이스 헤더, `lib/cmake/afi_sdk` 아래의 CMake 패키지 파일로 구성된다. 서브디렉터리 빌드에서도 같은 `afi::afi_sdk` 이름을 사용할 수 있다.

### 플랫폼별 참고사항

- **Windows**에서는 `ws2_32`에 자동으로 링크하며, Winsock 초기화를 내부에서 처리한다.
- **Linux**에서는 `pthread`에 자동으로 링크한다.

---

## Installation (Python SDK)

```bash
cd python/
pip install .
```

개발용 설치 명령은 다음과 같다.
```bash
pip install -e .
```

설치 확인 명령은 다음과 같다.
```bash
python -c "import afi_sdk; print(afi_sdk.__version__)"
# 출력: 2.2.0
```

---

## Network Configuration

### 기본 포트

| 포트 | 프로토콜 | 방향 | Event ID | 설명 |
|------|----------|------|----------|------|
| 30500 | TCP | 호스트 ↔ 센서 | (없음) | Config API (SOME/IP + JSON/raw chunk, Method ID `0x0010`~`0x00E1`) |
| 30501 | UDP (v1.1.0), TCP (v1.0.x) | 호스트 → 센서 | `0x8001` | CSII (Vehicle Input) |
| 30509 | TCP/UDP | 센서 → 호스트 | `0x8002` | RDI (Radar Detections / Point Cloud) |
| 30510 | TCP/UDP | 센서 → 호스트 | `0x8003` | SHII (Sensor Health Information) |
| 30511 | TCP/UDP | 센서 → 호스트 | `0x8004` | SPI (Sensor Performance Information) |
| 30520 | UDP | 호스트 ↔ 센서 | (없음) | Discovery (Broadcast) |

> **참고** 기본 데이터 스트림 전송 방식은 TCP이며, 호스트가 센서의 스트림 포트로 접속한다. UDP에서는 호스트가 스트림 포트를 bind하고, 송신 주소가 `sensor_ip`인 datagram만 수신한다. `stream_transport`로 전체 기본값을 지정하고, `rdi_transport`, `shi_transport`, `spi_transport`로 스트림별 값을 덮어쓴다. 이 값은 센서에 설정된 송신 프로토콜(`GetNetworkConfig`)과 일치해야 한다. CSII는 인터페이스 v1.1.0에서 UDP, v1.0.x에서 TCP를 사용한다. SDK는 센서의 인터페이스 세대로 CSII 전송 방식을 정하며, `csii_transport`로 이 선택을 덮어쓴다.

> **E2E Protection** RDI/SHII/SPI/CSII 메시지와 Config API 요청·응답은 모두 20바이트 E2E 헤더(CRC 상위 0, CRC-32, Length, Counter, Data ID, Big-endian)를 포함한다. CRC는 본문만 보호하며, CRC 필드가 0인 메시지는 통과로 판정하지 않는다. 인터페이스는 검증 실패 메시지의 처분을 호스트에 맡긴다(System Spec §7.5). SDK는 기본 설정에서 경고 로그를 기록하고, 프레임을 판정 결과와 함께 전달한다. 판정 결과는 `AfiRdiFrame`, `AfiStreamMeta`, `AfiWirePayloadFrame`의 `e2e_counter`, `e2e_checked`, `e2e_valid`에 담긴다. `validate_e2e`는 데이터 스트림 검증의 사용 여부를 정하며, `e2e_strict = true`이면 검증 실패 프레임을 폐기한다. 검증에 실패한 Config API 응답도 호출자에게 반환하며, 그 판정 결과는 `GetLastSensorResult().e2e_ok`에서 확인한다. 인터페이스 v1.0.x 센서는 앞 8바이트에 Length, Counter, Data ID, 본문에 대한 CRC-64/XZ를 둔다. SDK는 수신 시 이 CRC를 허용하며, 송신 측 세대를 `generation`으로 알린다.

---

## Firmware Compatibility

이번 SDK 릴리스는 인터페이스 v1.1.0(펌웨어 3.1.1) 센서와 함께 인터페이스 v1.0.x(펌웨어 3.0.0~3.1.0) 센서의 데이터를 수신한다. Config API와 펌웨어 업데이트는 인터페이스 v1.1.0에서만 동작한다. v1.0.x 지원은 이번 릴리스로 종료하며, 해당 센서는 bitsensing이 펌웨어 3.1.1로 이전한다.

| 기능 | 인터페이스 v1.1.0 (FW 3.1.1) | 인터페이스 v1.0.x (FW 3.0.0~3.1.0) |
|------|------------------------------|-------------------------------------|
| Discovery | 지원 | 지원 |
| RDI, SHII, SPI 수신 | 지원 | 지원 (E2E CRC-64/XZ 허용) |
| CSII | UDP 30501 | TCP 30501 |
| Config API | 지원 | `kNotSupported` / `NOT_SUPPORTED`, 송신하지 않음 |
| 펌웨어 업데이트 | 지원 | `kNotSupported` / `NOT_SUPPORTED`, 송신하지 않음 |

SDK는 Config API 연결을 시작할 때마다 읽기 전용 GetDeviceInfo 요청으로 세대를 판정한다. Config API를 끈 구성에서는 E2E 검증을 통과한 스트림 프레임으로 판정한다. 판정 결과는 `GetInterfaceGeneration()`(C++)과 `interface_generation`(Python)에서 확인한다. Discovery는 센서별 세대를 `AfiSensorInfo::generation`과 Python `_interface_generation` 키로 보고한다. v1.0.x 센서에서는 이 요청 직후 Config API 연결을 닫고, 이후의 Config API 호출을 송신하지 않고 반환한다. v1.0.x 센서는 해석하지 못한 v1.1.0 요청 가운데 Restart, ResetAllSettings 같은 일부 요청을 실행하기 때문이다.

스트림 layout은 두 세대에서 같다. SDK는 모든 값을 변환하지 않고 전달하며, 아래 필드는 v1.0.x에서 의미가 다르다. 두 세대는 payload Common Header의 인터페이스 버전(`1.0.0` 또는 `1.1.0`)으로 구분한다.

| 필드 | 인터페이스 v1.0.x | 인터페이스 v1.1.0 |
|------|-------------------|-------------------|
| RDI `existence_probability` | 표지값 100, 60, 40 | 확률 × 100, 증강 detection은 95 또는 90 |
| RDI DataQualifier | 0, 5(주입 또는 시험 모드), 6 | 0, 1, 6 |
| RDI MessageCounter | 송신 카운터 | scan index, DataQualifier 6 재송신에서 반복 |
| RDI RecognisedDetectionsCapability | 4096(FW 3.0.x) 또는 8192 | 2048 |
| RDI 반경 속도 영역 | ±50 m/s(FW 3.0.0) | −111.11 m/s부터 |
| SHII 센서 운영 모드 | `150`(v1.0.x 시험 모드)이 나타날 수 있음 | `6`(UDR)이 나타날 수 있음 |
| SHII 입력 신호 상태 | 1개 | 4개 |
| E2E Counter | 스트림별, TCP 연결마다 1부터 증가 | RDI, SHII, SPI가 공유하는 송신 슬롯 |

---

## Configuration Workflow

### 접근 모드

모든 설정 SET은 진단(Diagnostic) 접근 모드를 요구하며, 펌웨어 업데이트 연쇄는 프로그래밍(Programming) 접근 모드를 요구한다. 모드 진입은 Normal, Diagnostic, Programming 순서의 단방향이며, Normal 복귀는 재부팅 또는 레이더 자체의 통신 스택 복구 재진입으로만 일어난다.

**접근 모드별 개방 범위**

| 모드 | 값 | 개방 범위 | 데이터 스트림 |
|------|----|-----------|---------------|
| Normal | 0 | GET 메서드, `Restart`, `SetMode` | 송출 |
| Diagnostic | 1 | 설정 SET 전체, `ResetAllSettings` | 송출 |
| Programming | 2 | 펌웨어 업데이트 연쇄 | 재부팅 전까지 정지 |

**접근 모드 API**

| 동작 | C++ | Python |
|------|-----|--------|
| 현재 모드 조회 | `GetMode(AfiModeStatus&)` | `get_mode()` |
| 다음 모드 진입 | `SetMode(AfiAccessMode)` | `set_mode(AccessMode.X)` |
| 현재 모드에서 목표 모드까지 진입 | `EnterMode(AfiAccessMode)` | `enter_mode(AccessMode.X)` |

진단 모드 밖에서 보낸 SET은 `kSessionRequired`를 반환한다. 재진입과 역방향 전이는 `kAccessDenied`를 반환하며, 프로그래밍 모드에서 호출한 `EnterMode(kDiagnostic)`도 `kAccessDenied`를 반환한다.

### 설정 반영

레이더는 모든 SET을 검증하고 NVM에 기록한 뒤 응답한다. 설정은 `Restart`(`0x0014`) 이후 반영되며, 별도 저장 명령은 없다. 재시작 전까지 GET은 기록된 값을 반환하므로 동작 중인 값과 다를 수 있다. `SetNetworkConfig`, `SetRangeMode`, `SetPtpConfig`는 `restart_required`를 보고할 수 있다. 스트림 목적지와 레이더 자체 네트워크 설정도 같은 규칙을 따른다. `Restart`는 레이더가 리셋되기 전에 응답하므로 Config API 연결이 끊긴다. `ReconnectConfigApi()`(C++) 또는 `reconnect()`(Python)는 레이더가 다시 연결을 수락할 때까지 대기한다.

```cpp
afi::AfiSdk sdk;
sdk.Init(config);

if (sdk.EnterMode(afi::AfiAccessMode::kDiagnostic) != afi::kOk) {
    auto r = sdk.GetLastSensorResult();
    printf("EnterMode failed: %s\n", r.return_name.c_str());
    return 1;
}
bool restart_required = false;
sdk.SetRangeMode(afi::AfiRangeMode::kLongRange, &restart_required);
sdk.Restart();
sdk.ReconnectConfigApi();   // 기본 최대 60초 대기
```

```python
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, RangeMode, OK

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))

if sdk.enter_mode(AccessMode.DIAGNOSTIC) != OK:
    print(sdk.last_result)
elif sdk.set_range_mode(RangeMode.LONG_RANGE) == OK:
    sdk.restart()
    sdk.reconnect()          # 기본 최대 60초 대기
```

### 펌웨어 업데이트

펌웨어 업데이트 연쇄는 프로그래밍 접근 모드에서 수행하며, 이 모드는 재부팅 전까지 RDI/SHII/SPI 송출을 정지한다.

**업데이트 도메인**

| `update_type` | 컴포넌트 | 기록 위치 |
|---------------|----------|-----------|
| `"firmware"` | `m7_0`, `m7_1`, `m7_2`, `a53` (4종 모두 필수) | 비활성 슬롯 면(`target_slot`), `FirmwareSwitchSlot`으로 활성화 |
| `"bootloader"` | `bootloader` | 제자리 기록, 백업 사본 유지 |

- `FirmwareUpdateStart`는 이미지 CRC-32를 `"0x%08X"` 형식으로 송신하며, 응답으로 `max_chunk_size`(4096)와 `target_slot`을 받는다.
- `FirmwareDataChunk`는 조각의 일부만 수락할 수 있다. 다음 전송은 `received_bytes` 위치에서 재개한다. C++는 `AfiOtaChunkResult` 오버로드에서, Python은 반환 dict에서 이 값을 확인한다.
- `FirmwareUpdateCommit` 이후 상태가 `idle`이 될 때까지 `FirmwareUpdateStatus`를 폴링하고, 슬롯을 전환한 뒤 `FirmwareUpdateReboot`를 송신한다.
- Python 모듈 `afi_sdk.firmware`는 전체 절차를 수행한다. `update()`는 프로그래밍 모드 진입, 컴포넌트별 업로드, 슬롯 전환, 재부팅을 수행하며, `wait_for_restart()`와 `verify()`로 결과를 확인한다. C++ SDK는 개별 호출만 제공하므로 호출자가 같은 순서를 따른다.

### 오류 보고

Config API 메서드는 `AfiError` 값을 반환하며, 레이더 반환 코드는 서로 다른 오류로 대응한다.

**레이더 반환 코드 대응**

| 레이더 반환 코드 | C++ `AfiError` | 값 |
|------------------|----------------|----|
| `INVALID_PARAM` | `kInvalidParam` | -6 |
| `NOT_SUPPORTED` | `kNotSupported` | -10 |
| `BUSY` | `kBusy` | -11 |
| `SESSION_REQUIRED` | `kSessionRequired` | -12 |
| `ACCESS_DENIED` | `kAccessDenied` | -13 |
| `INVALID_STATE` | `kInvalidState` | -14 |
| 그 밖의 코드 | `kSensorError` | -8 |

`GetLastSensorResult()`는 레이더의 `return_code`, `return_name`, `error_message`(펌웨어 업데이트 메서드), 응답 E2E 판정 `e2e_ok`를 담는다. 인터페이스 v1.0.x 센서에서는 모든 Config API 호출이 송신 없이 `kNotSupported`를 반환하며, `GetLastSensorResult()`는 `NOT_SUPPORTED`와 `error_message`를 담는다.

Python SDK는 같은 숫자 값을 사용한다(`OK`, `NOT_INITIALIZED`, `CONNECT_FAILED`, `TIMEOUT`, `DISCONNECTED`, `INVALID_PARAM`, `PROTOCOL_ERROR`, `SENSOR_ERROR`, `INTERNAL_ERROR`, `NOT_SUPPORTED`, `BUSY`, `SESSION_REQUIRED`, `ACCESS_DENIED`, `INVALID_STATE`). `last_error`는 마지막 호출의 오류 코드를 담고, `last_result`는 `return_code`, `return_name`, `error_message`, `data`, 응답 E2E 판정 `e2e_ok`를 담는다.

---

## Examples

### C++ 예제

| 프로그램 | 설명 |
|----------|------|
| `sample_discovery` | 네트워크에서 센서 탐색 |
| `sample_pointcloud` | RDI Point Cloud 데이터 수신 |
| `sample_data_stream` | RDI/SHII/SPI 데이터 동시 수신 |
| `sample_config` | 센서 설정 조회 |
| `sample_multi_sensor` | 다중 센서 데이터 수신 |
| `set_stream_destination` | RDI/SHII/SPI 스트림 목적지 설정 후 재시작으로 적용 |
| `restart_sensor` | 센서 재시작 후 Config API 재연결 대기 |

소스 코드는 [examples/cpp/](examples/cpp/)에서 확인한다.

### Python 예제

| 프로그램 | 설명 |
|----------|------|
| `sample_discovery.py` | 네트워크에서 센서 탐색 |
| `sample_pointcloud.py` | RDI Point Cloud 데이터 수신 |
| `sample_data_stream.py` | RDI/SHII/SPI 데이터 동시 수신 |
| `sample_config.py` | 센서 설정 조회 및 동일 값 재기록 |
| `sample_multi_sensor.py` | 다중 센서 데이터 포트 설정 |
| `set_yaw_90.py` | 장착 yaw 기록 후 재시작으로 적용 |

소스 코드는 [python/examples/](python/examples/)에서 확인한다.

---

## Multi-Sensor

AFI920 SDK는 최대 **4개 센서**의 동시 운용을 지원한다. 센서마다 `AfiSdk` 인스턴스를 하나씩 생성하며, 각 인스턴스는 연결과 데이터 수신을 독립적으로 관리한다.

### 포트 할당

TCP(기본값)에서는 각 인스턴스가 자기 센서의 IP로 접속하므로 모든 센서가 기본 스트림 포트를 유지한다. UDP에서는 호스트가 스트림 포트를 bind하므로, 같은 호스트로 송신하는 센서는 서로 다른 포트를 사용해야 한다. 센서당 `+100` 오프셋을 권장한다. Config API는 각 센서 IP의 30500 포트로 나가는 TCP 연결이므로 오프셋을 적용하지 않는다.

**UDP 사용 시 센서별 포트 할당**

| 센서 | RDI 포트 | SHII 포트 | SPI 포트 |
|------|----------|-----------|----------|
| 센서 1 | 30509 | 30510 | 30511 |
| 센서 2 | 30609 | 30610 | 30611 |
| 센서 3 | 30709 | 30710 | 30711 |
| 센서 4 | 30809 | 30810 | 30811 |

각 센서의 목적지는 진단 접근 모드에서 `SetStreamDestination` 또는 `SetNetworkConfig`로 기록한 뒤 센서를 재시작한다. 같은 포트를 해당 인스턴스의 `AfiSdkConfig`(`rdi_port`, `shi_port`, `spi_port`)에도 지정한다.

다중 센서 예제는 [`sample_multi_sensor.cpp`](examples/cpp/sample_multi_sensor.cpp)와 [`sample_multi_sensor.py`](python/examples/sample_multi_sensor.py)에서 확인한다.

---

## Testing

### 단위 테스트

빌드 및 실행 명령은 다음과 같다.

```bash
cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
ctest --output-on-failure
```

CTest는 C++ 단위 테스트(`test_config_transport` 포함)를 실행하며, Python 3 인터프리터가 있으면 `test_python_streams`와 `test_python_sdk`도 실행한다. `test_python_config_transport`와 `test_python_firmware`는 `pytest`가 설치된 경우에 등록한다.

```bash
pip install pytest
```

### 통합 테스트

실 센서에 연결한 상태의 실행 명령은 다음과 같다.

```bash
./test/test_discovery
./test/test_config_api 192.168.10.150
./test/test_data_receive 192.168.10.150 60
python test/test_live_conformance.py 192.168.10.150 --duration 30
```

- `test_config_api`는 GET 요청만 송신한다. `--write`를 지정하면 진단 접근 모드에 진입하여 같은 값을 다시 기록한다. 모든 기록은 NVM에 저장되며, 레이더는 재부팅 전까지 진단 모드를 유지한다.
- `test_live_conformance.py`는 읽기 전용이며 SET, Restart, 펌웨어, 접근 모드 요청을 송신하지 않는다. `--skip-csii`는 CSII 항목을 건너뛴다. 종료 코드 0은 모든 센서에서 모든 점검을 통과했음을 뜻한다.

다음 시험은 센서 상태를 바꾸며, 확인 플래그 없이는 실행하지 않는다. 시험 전용 센서에만 사용한다.

```bash
python test/test_live_write.py 192.168.10.150 --yes-mutate
python test/test_live_firmware.py 192.168.10.150 --images DIR --fw-hash <hash>     [--bootloader --bl-hash <hash>] --yes-flash
./test/test_firmware_live 192.168.10.150 DIR/bootloader.bin <hash> --yes-flash
```

- `test_live_write.py`는 접근 모드 경계, config SET 전 종류(Restart 뒤 반영, GET과 Discovery로 재확인), ResetConfigDefaults, ResetAllSettings, Programming 모드를 점검한다. 시작 시 설정을 읽어 두고 종료 시 되돌린다.
- `test_live_firmware.py`는 갱신 거부(최대 조각 초과, CRC 불일치, ABORT, 10초 무진행)를 점검하고, `afi_sdk.firmware.update()`를 실행한 뒤 면을 왕복 전환한다. `DIR`에는 평문 컴포넌트 이미지 `m7_0.bin`, `m7_1.bin`, `m7_2.bin`, `a53.bin`과 선택적으로 `bootloader.bin`을 둔다. 센서는 기록한 면에서 종료한다.
- `test_firmware_live`는 C++ 경로를 점검한다. 부트로더를 제자리 기록한 뒤 FirmwareSwitchSlot으로 다른 면에 갔다가 돌아온다.

> **Windows** 환경에서는 실행 파일 이름에 `.exe`를 붙인다.

---

## Data Types

프로토콜 데이터는 `third_party/afi920_interface/src/c` 헤더에 정의된 ISO 23150 C 구조체와 SDK 래퍼 타입으로 표현한다.

### 핵심 구조체

| 구조체 | 설명 |
|--------|------|
| `AfiRdiFrame` | SDK 콜백에서 전달되는 RDI 프레임, E2E 판정(`e2e_counter`, `e2e_checked`, `e2e_valid`)과 송신 측 `generation` 포함 |
| `bts_rdi_detection_t` | 개별 detection(51바이트), 위치·속도·측정 오차 포함 |
| `bts_shi_message_t` | 센서 상태 정보 (가변 길이) |
| `bts_spi_message_t` | 센서 성능 정보 |
| `AfiStreamMeta` | SHII/SPI 콜백 오버로드에 전달되는 수신 시각과 E2E 판정 |
| `AfiWirePayloadFrame` | 타입별 파싱 전에 전달되는 재조립 스트림 payload |
| `AfiVehicleInput` | `SendVehicleInput`(CSII)으로 센서에 송신하는 차량 동역학 정보, `sensor_id` 기본값 1 |
| `AfiModeStatus` | 접근 모드, 현재 모드 유지 시간, 데이터 스트림 정지 여부 |
| `AfiSensorResult` | 마지막 Config API 호출에 대한 레이더 처리 결과 |
| `AfiInterfaceGeneration` | 센서 또는 프레임의 인터페이스 세대: `kUnknown`, `kV1_0`, `kV1_1` |

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

프로토콜은 프레임당 최대 **8192개 detection**(36바이트 헤더 + detection당 51바이트)을 허용하며, 펌웨어 v3.1.1은 최대 2048개를 송신한다. UDP 대형 프레임은 SOME/IP-TP로 분할되며 SDK가 자동으로 재조립한다. TCP는 완전한 SOME/IP 프레임 하나로 수신한다.

> **중요** callback으로 받은 frame 참조는 callback 실행 중에만 유효하다. `AfiRdiFrame` 전체를 복사하면 복사본이 vector를 소유하고 C payload 포인터를 다시 연결하므로 안전하다. 공개 storage vector를 직접 resize하거나 교체한 경우에는 `RebindStorage()`를 호출한다.

전체 구조체 정의와 응답 타입은 [docs/api_reference.md](docs/api_reference.md)와 [third_party/afi920_interface/src/c](third_party/afi920_interface/src/c) 헤더에서 확인한다.

---

## Architecture

```text
AfiSdk
  ├── Discovery
  ├── Config API Client (E2E 요청·응답, 필요 시 재연결)
  ├── Data Stream Receivers (RDI / SHII / SPI)
  │     ├── E2E Validation (length / Data ID / CRC-32, 판정 결과를 프레임과 함께 전달)
  │     └── Bounded SOME/IP-TP Reassembly (UDP)
  ├── CSII Vehicle Input Sender (Host → Radar, UDP or TCP by generation)
  └── Protocol Parsers
```

- 각 `AfiSdk` 인스턴스는 독립적으로 동작한다. 다중 센서 운용 시 센서마다 인스턴스를 하나씩 생성한다.
- 데이터 콜백은 내부 수신 스레드에서 호출되므로, 콜백 처리는 블로킹 없이 유지하거나 별도 처리 스레드로 넘긴다.
- RDI 손실 콜백은 E2E 검증을 통과한 프레임의 E2E Counter 결번으로 손실 송신 슬롯을 집계한다.
- TCP 데이터 스트림은 연결이 끊기면 1초에서 최대 30초까지 늘어나는 간격으로 재연결한다.
- `SendVehicleInput()`은 호출자 스레드에서 실행되며, 첫 호출 시 CSII 소켓을 생성한다. 인터페이스 v1.1.0에는 UDP, v1.0.x에는 TCP 소켓을 사용한다.
- 각 Config API 연결은 인터페이스 세대를 판정하는 읽기 전용 GetDeviceInfo 요청으로 시작한다. v1.0.x이면 연결을 닫고, Config API 호출은 송신 없이 `kNotSupported`를 반환한다.
- Config API 클라이언트는 요청을 한 번에 1건씩 송신하며, 각 응답이 요청의 message ID, client ID, session ID, 프로토콜 버전 `0x01`, RESPONSE 타입을 그대로 돌려주는지 검증한다. 타임아웃, 불일치 응답, 연결 소실이 발생하면 소켓을 닫고, 다음 호출에서 같은 endpoint로 재연결한다. 요청당 대기 시간은 `request_timeout_ms`로 제한하며, 본문이 16,384바이트를 넘는 요청은 송신하지 않고 `kInvalidParam`을 반환한다.
- `Init()` 중 Config API 연결에 실패해도 `Init()`은 실패하지 않는다. SDK는 endpoint를 유지하며 다음 Config API 호출에서 연결한다. `Stop()` 이후 `Init()`을 다시 호출할 수 있다.

---

## Troubleshooting

| 증상 | 원인 | 해결 방법 |
|------|------|-----------|
| `Init()`이 `kConnectFailed` 반환 | TCP 데이터 스트림 연결 실패 또는 UDP 스트림 포트 bind 실패 | 센서 IP, 네트워크 케이블, 서브넷 설정, 해당 포트를 점유한 다른 프로세스 확인 |
| 설정 SET이 `kSessionRequired` 반환 | 레이더가 진단 접근 모드가 아님 | SET 전에 `EnterMode(kDiagnostic)` 호출 |
| `SetMode` 또는 `EnterMode`가 `kAccessDenied` 반환 | 재진입, 역방향 전이, 또는 레이더가 프로그래밍 모드 | 레이더를 재시작하여 Normal 모드로 복귀 |
| SET은 성공했으나 동작이 바뀌지 않음 | 설정은 재시작 후 반영 | `Restart()` 호출 후 `ReconnectConfigApi()` 호출 |
| 프로그래밍 모드 진입 후 데이터 스트림 정지 | 프로그래밍 모드는 RDI/SHII/SPI 송출을 정지 | 레이더 재시작 |
| Config API 호출이 `kTimeout` 또는 `kDisconnected` 반환 | `request_timeout_ms` 내 무응답, 연결 소실, 또는 다른 클라이언트가 레이더의 단일 Config API 연결을 점유 | 다음 호출에서 자동 재연결, 다른 Config API 클라이언트 종료 |
| `kBusy` 반환 | 초당 200건 초과 요청 | 요청 빈도 감축 |
| Config API는 동작하지만 데이터가 수신되지 않음 | 스트림 목적지 또는 전송 방식 불일치, 또는 `sensor_ip`가 아닌 주소에서 온 UDP datagram | `GetNetworkConfig`로 목적지 IP/포트/프로토콜 확인 후 `AfiSdkConfig`와 일치 |
| Discovery 결과에 센서 누락 | UDP 응답 유실 또는 다른 NIC로 브로드캐스트 송출 | Discovery 재수행, `bind_ip` 지정 |
| 로그에 E2E CRC 불일치 경고 발생 | 프레임 손상 | 프레임별 `e2e_valid` 확인, `e2e_strict = true`로 실패 프레임 폐기 |
| 높은 패킷 손실률 | UDP 수신 버퍼 부족 | `AfiSdkConfig`의 `socket_buffer_size` 증가 (C++ SDK) |

전체 에러 코드와 복구 방법은 [API Reference](docs/api_reference.md#error-codes)에서 확인한다.

---

## Documentation

| 문서 | 설명 |
|------|------|
| [API Reference](docs/api_reference.md) | C++ 및 Python 공개 API와 주요 타입 레퍼런스 |
| [Python SDK](python/README.md) | Python SDK 빠른 참조 |
| [CHANGELOG](CHANGELOG.md) | 릴리스 이력 |

---

## Security Notice

AFI920 SDK는 센서와 평문 TCP/UDP 프로토콜로 통신하며, TLS/DTLS 같은 암호화 또는 인증 메커니즘을 포함하지 않는다.

- 센서 네트워크는 가능한 한 전용 VLAN 또는 물리적으로 격리된 네트워크에 구성한다.
- 센서 데이터가 흐르는 네트워크 구간의 접근을 제한한다.
- 신뢰되지 않은 네트워크 환경에서는 VPN, 방화벽 등 추가 보안 조치를 적용한다.
- 접근 모드 진입에는 인증 절차가 없다. Config API 포트에 도달하는 호스트는 누구나 진단 또는 프로그래밍 모드에 진입하여 설정을 변경하고 펌웨어를 갱신할 수 있으므로, Config API 포트는 신뢰 네트워크 안에만 둔다.

---

## Related Projects

| 프로젝트 | 설명 |
|----------|------|
| [afi920-ros2-driver](https://github.com/bitsensing/afi920-ros2-driver) | AFI920 ROS 2 드라이버 패키지 |

---

## Changelog

전체 릴리스 이력은 [CHANGELOG.md](CHANGELOG.md)에서 확인한다.

---

## License

BSD-3-Clause

Copyright (c) 2025-2026 bitsensing Inc.

전체 라이선스 문구는 [LICENSE](LICENSE)에서 확인한다. 서드파티 라이선스 고지는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)에서 관리한다.

---

## Support

- **GitHub Issues**는 공개 SDK 사용 중 재현 가능한 버그, 문서 오탈자, 개선 제안을 접수한다([github.com/bitsensing/afi920-sdk/issues](https://github.com/bitsensing/afi920-sdk/issues)).
- **기술 지원**은 AFI920 제품 사용, 고객 환경 통합, 별도 제공 문서 관련 문의를 담당한다(`tech-support@bitsensing.com`).
