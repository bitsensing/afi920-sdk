# AFI920 SDK AI 통합 가이드

| 항목 | 값 |
|------|----|
| 대상 | AI 코딩 어시스턴트, LLM 기반 자동화, bitsensing AFI920 4D 이미징 레이더를 가장 짧은 경로로 코드에 연동하려는 엔지니어 |
| SDK 버전 | 2.2.0 (C++, Python) |
| 센서 인터페이스 | AFI920 인터페이스 v1.1.0이다. 인터페이스 v1.0.x(펌웨어 3.0.0~3.1.0)는 데이터 수신만 지원하며, 세부 사항은 [13절](#13-인터페이스-v10x-호환)에서 확인한다 |
| 라이선스 | BSD-3-Clause |
| 전체 API 레퍼런스 | [docs/api_reference.md](docs/api_reference.md) |

---

## 목차

1. [SDK 개요](#1-sdk-개요)
2. [레포지토리 구조](#2-레포지토리-구조)
3. [네트워크 사전 조건](#3-네트워크-사전-조건)
4. [빌드 & 설치](#4-빌드--설치)
5. [빠른 시작: 포인트 클라우드 수신](#5-빠른-시작-포인트-클라우드-수신)
6. [세 가지 데이터 스트림 모두 사용](#6-세-가지-데이터-스트림-모두-사용)
7. [접근 모드와 설정 저장](#7-접근-모드와-설정-저장)
8. [Configuration API](#8-configuration-api)
9. [펌웨어 업데이트](#9-펌웨어-업데이트)
10. [차량 입력 (CSII)](#10-차량-입력-csii)
11. [센서 탐색 (Discovery)](#11-센서-탐색-discovery)
12. [멀티 센서 설정](#12-멀티-센서-설정)
13. [인터페이스 v1.0.x 호환](#13-인터페이스-v10x-호환)
14. [주요 데이터 구조](#14-주요-데이터-구조)
15. [에러 코드](#15-에러-코드)
16. [포트 & 프로토콜 레퍼런스](#16-포트--프로토콜-레퍼런스)
17. [문제 해결 체크리스트](#17-문제-해결-체크리스트)

---

## 1. SDK 개요

AFI920 SDK는 호스트 컴퓨터와 하나 이상의 bitsensing AFI920 레이더를 이더넷으로 연결한다.

| 방향 | 채널 | 전달 내용 |
|------|------|-----------|
| 레이더 → 호스트 | RDI (TCP/UDP 30509, Event `0x8002`) | 포인트 클라우드 detection(거리, 방위각, 고도각, 반경 속도, RCS, SNR, 오차) |
| 레이더 → 호스트 | SHII (TCP/UDP 30510, Event `0x8003`) | 센서 상태 정보 |
| 레이더 → 호스트 | SPI (TCP/UDP 30511, Event `0x8004`) | 센서 성능과 장착 자세 |
| 호스트 → 레이더 | CSII (30501, Event `0x8001`, v1.1.0은 UDP, v1.0.x는 TCP) | 차량 동역학 |
| 호스트 ↔ 레이더 | Config API (TCP 30500, SOME/IP + JSON) | 상태, 설정, 접근 모드, 펌웨어 업데이트 |
| 호스트 ↔ 레이더 | Discovery (UDP 브로드캐스트 30520) | LAN 내 센서 탐색 |

RDI, SHII, SPI, CSII 메시지와 Config API 요청·응답은 모두 20바이트 E2E 헤더(AUTOSAR Profile 7 layout, 본문 대상 IEEE 802.3 CRC-32)를 포함한다. Discovery에는 E2E 헤더가 없다. SOME/IP 프레이밍, SOME/IP-TP 재조립, E2E 검증은 SDK가 처리한다.

| 기능 | C++ (`afi::AfiSdk`) | Python (`afi_sdk`) |
|------|---------------------|--------------------|
| Discovery | `AfiSdk::Discover()` | `AfiSdk.discover()` |
| RDI / SHII / SPI 수신 | 수신 스레드와 타입별 callback | `afi_sdk.streams` 파서(소켓은 호출자가 관리) |
| CSII 차량 입력 | `SendVehicleInput()` | `send_vehicle_input()` |
| Config API와 접근 모드 | `int` 반환 코드와 출력 인자 | `dict` / `int` 반환값 |
| 펌웨어 업데이트 | 개별 프로토콜 단계 | 개별 단계와 전체 절차 `afi_sdk.firmware.update()` |

---

## 2. 레포지토리 구조

```
afi920-sdk/
├── CMakeLists.txt                 ← top-level build (CMake 3.16+, C++17)
├── README.md                      ← overview, build, workflows (English)
├── README_ko.md                   ← overview, build, workflows (Korean)
├── AFI920_SDK_GUIDE.md            ← this guide (English)
├── AFI920_SDK_GUIDE_ko.md         ← this guide (Korean)
├── CHANGELOG.md                   ← release history
├── LICENSE                        ← BSD-3-Clause
├── THIRD_PARTY_NOTICES.md
├── docs/
│   └── api_reference.md           ← complete C++ and Python API reference
├── include/afi/
│   ├── afi_sdk.h                  ← AfiSdk class: lifecycle, callbacks, CSII, Discovery, Config API
│   ├── afi_types.h                ← stream and Config API data types
│   ├── afi_config.h               ← AfiSdkConfig
│   └── afi_error.h                ← AfiError, AfiReturnCode, AfiErrorStr()
├── src/                           ← C++ implementation (static library target afi_sdk)
├── examples/cpp/                  ← sample_discovery, sample_pointcloud, sample_data_stream,
│                                    sample_config, sample_multi_sensor, restart_sensor,
│                                    set_stream_destination
├── python/
│   ├── pyproject.toml             ← package afi-sdk 2.2.0 (Python 3.8+, standard library only)
│   ├── src/afi_sdk/
│   │   ├── __init__.py            ← AfiSdk, AfiSdkConfig, RangeMode, AccessMode, error codes
│   │   ├── _sdk.py                ← AfiSdk class: Config API, CSII, Discovery
│   │   ├── firmware.py            ← complete firmware update sequence: update(), verify(), ...
│   │   ├── someip.py              ← SOME/IP framing, ConfigClient, discover(), constants
│   │   └── streams.py             ← E2E, SOME/IP-TP, RDI/SHII/SPI parsers, CSII builder
│   └── examples/                  ← sample_discovery.py, sample_pointcloud.py,
│                                    sample_data_stream.py, sample_config.py,
│                                    sample_multi_sensor.py, set_yaw_90.py
├── test/                          ← C++ and Python unit tests, live-sensor tests
├── third_party/
│   ├── afi920_interface/src/c/    ← vendored ISO 23150 protocol headers (bts_*.h)
│   └── nlohmann/                  ← nlohmann/json 3.12.0 (bundled)
└── tools/
    └── rdi_udp_sender.py          ← RDI UDP simulator for tests without a radar
```

| 예제 | 내용 |
|------|------|
| `sample_discovery` / `.py` | 네트워크 센서 탐색 |
| `sample_pointcloud` / `.py` | RDI 포인트 클라우드 수신 |
| `sample_data_stream` / `.py` | RDI, SHII, SPI 동시 수신 |
| `sample_config` / `.py` | 전체 설정 범주 조회(`.py --write`는 동일 값 재기록 포함) |
| `sample_multi_sensor` / `.py` | C++는 최대 4개 센서 수신, Python은 센서별 데이터 포트 기록과 센서 재시작 |
| `restart_sensor` | Config API를 통한 센서 재시작 |
| `set_stream_destination` | RDI, SHII, SPI 목적지를 호스트 IP로 지정 |
| `set_yaw_90.py` | 장착 yaw 설정(기본 90°) |

> ROS 2 드라이버는 별도 레포지토리 [afi920-ros2-driver](https://github.com/bitsensing/afi920-ros2-driver)에서 관리한다.

---

## 3. 네트워크 사전 조건

| 항목 | 기본값 |
|------|--------|
| 센서 IP | `192.168.10.150` |
| 호스트 IP | `192.168.10.x` (예: `192.168.10.100`) |
| 서브넷 마스크 | `255.255.255.0` |

1. 호스트와 레이더를 이더넷으로 연결한다.
2. 호스트 NIC에 `192.168.10.x` 대역의 정적 IP를 할당한다.
   - Linux에서는 `sudo ip addr add 192.168.10.100/24 dev eth0`을 실행한다.
   - Windows에서는 네트워크 어댑터 속성의 IPv4 항목에서 수동 IP `192.168.10.100`과 마스크 `255.255.255.0`을 설정한다.
3. `ping 192.168.10.150`으로 연결을 확인한다.

SDK는 암호화와 인증 기능을 포함하지 않으며, 접근 모드 진입도 인증하지 않는다. 센서 네트워크는 전용 VLAN 또는 분리된 네트워크 구간에 둔다.

---

## 4. 빌드 & 설치

### C++ (CMake 3.16+, C++17)

ISO 23150 프로토콜 헤더는 `third_party/afi920_interface/src/c`에, `nlohmann/json`은 `third_party/nlohmann`에 포함되어 있다. 별도의 체크아웃이나 다운로드 단계는 필요하지 않다.

```bash
cd afi920-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # examples build by default (BUILD_EXAMPLES=ON)
cmake --build build --config Release
# Linux:   build/src/libafi_sdk.a
# Windows: build/src/afi_sdk.lib (Visual Studio generators add a Release/ subfolder)
```

| CMake 옵션 | 기본값 | 설명 |
|------------|--------|------|
| `BUILD_EXAMPLES` | `ON` | `examples/cpp` 빌드 |
| `BUILD_TESTS` | `OFF` | `test/` 빌드(빌드 디렉터리에서 `ctest --output-on-failure`로 실행) |

**자체 CMakeLists.txt에서 링크하는 방법**

```cmake
add_subdirectory(afi920-sdk)                       # path to this repository
target_link_libraries(my_app PRIVATE afi_sdk)
```

`afi_sdk` 타깃은 include 디렉터리(`include/`, `third_party/afi920_interface/src/c`), C++17 요구 사항, 플랫폼 라이브러리(Windows의 `ws2_32`, Linux의 스레드 라이브러리)를 함께 전달한다. Winsock 초기화는 SDK 내부에서 처리한다.

### Python (3.8+, 표준 라이브러리만 사용)

```bash
cd afi920-sdk/python
pip install .            # or for development: pip install -e .
python -c "import afi_sdk; print(afi_sdk.__version__)"     # 2.2.0
```

---

## 5. 빠른 시작: 포인트 클라우드 수신

### C++

```cpp
#include <afi/afi_sdk.h>
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    afi::AfiSdkConfig cfg;
    cfg.sensor_ip = "192.168.10.150";    // default sensor IP
    cfg.enable_rdi = true;               // RDI over TCP (stream_transport defaults to "tcp")
    cfg.enable_config_api = false;       // data-only mode

    afi::AfiSdk sdk;

    // Called once per RDI frame (one per 100 ms send slot) on the RDI receiver thread.
    // Positions are spherical (range / azimuth / elevation), not Cartesian.
    sdk.RegRecvCallback([](const afi::AfiRdiFrame& frame) {
        const auto& m = frame.message;
        printf("Frame %u  detections: %u  e2e_valid: %d\n",
               frame.frame_seq, m.num_detections, frame.e2e_valid ? 1 : 0);
        for (uint16_t i = 0; i < m.num_detections; ++i) {
            const auto& d = m.detections[i];
            printf("  [%u] r=%.2f m  az=%.3f rad  el=%.3f rad  vel=%.2f m/s  rcs=%.1f dBsm\n",
                   i, d.position_radial_distance, d.position_azimuth,
                   d.position_elevation, d.relative_velocity_radial,
                   d.radar_cross_section);
        }
    });

    if (sdk.Init(cfg) != afi::kOk) return 1;    // connects the TCP stream
    if (sdk.Start() != afi::kOk) return 1;      // starts the receiver thread

    std::this_thread::sleep_for(std::chrono::seconds(10));
    sdk.Stop();
    return 0;
}
```

### Python

Python `AfiSdk` 클래스는 Config API, CSII, Discovery를 담당하며 스트림 소켓을 열지 않는다. 각 스트림은 일반 소켓으로 수신하고 `afi_sdk.streams`로 디코딩한다. `python/examples/sample_pointcloud.py`는 TCP와 UDP를 모두 다루며, `python/examples/sample_data_stream.py`는 `SomeipTpReassembler`를 사용한 제한형 SOME/IP-TP 재조립을 추가로 보여 준다.

```python
import socket, struct
from afi_sdk.someip import SOMEIP_HEADER_SIZE, DEFAULT_RDI_PORT
from afi_sdk.streams import (E2E_DATA_ID_RDI, strip_e2e,
                             parse_rdi_frame, parse_rdi_detection)

def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf

sock = socket.create_connection(("192.168.10.150", DEFAULT_RDI_PORT))   # TCP RDI stream

while True:
    hdr = recv_exact(sock, 8)                        # SOME/IP Message ID (4) + Length (4)
    if len(hdr) < 8:
        break
    length = struct.unpack(">I", hdr[4:8])[0]
    data = hdr + recv_exact(sock, length)            # one complete SOME/IP message
    e2e, iso = strip_e2e(data[SOMEIP_HEADER_SIZE:], expected_data_id=E2E_DATA_ID_RDI)
    if e2e is None or not e2e["ok"]:
        continue                                     # failed checks are logged; the body is kept
    frame = parse_rdi_frame(iso)                     # 36 B header
    if not frame:
        continue
    print(f"detections={frame['num_detections']}  counter={frame['message_counter']}  "
          f"dq={frame['data_qualifier']}")
    if frame["num_detections"] > 0:
        d = parse_rdi_detection(iso, frame["det_start"], 0)          # 51 B per detection
        print(f"  r={d['radial_distance']:.2f} m  az={d['azimuth']:.3f} rad  "
              f"el={d['elevation']:.3f} rad  v={d['radial_velocity']:.2f} m/s  "
              f"rcs={d['rcs']:.1f} dBsm")
```

DataQualifier `0`은 새 scan 데이터, `6`은 직전 scan의 재송신(MessageCounter 반복), `1`은 사용하지 않아야 하는 내용을 뜻한다.

---

## 6. 세 가지 데이터 스트림 모두 사용

### 모든 콜백 등록 (C++)

`RegRecvCallback`은 오버로드되어 있으며, callback 인자 타입으로 스트림을 선택한다. 설정에서 스트림(`enable_rdi`, `enable_shi`, `enable_spi`)을 활성화하고, `Start()` 전에 callback을 등록한다.

```cpp
afi::AfiSdkConfig cfg;
cfg.enable_rdi = true;
cfg.enable_shi = true;
cfg.enable_spi = true;

afi::AfiSdk sdk;

// RDI: point cloud (interface v1.1.0 firmware sends at most 2048 detections per frame; the protocol allows 8192)
sdk.RegRecvCallback([](const afi::AfiRdiFrame& f) {
    /* f.message.detections[0 .. f.message.num_detections-1], f.e2e_valid, f.generation */
});

// SHII: sensor health, with receive time and E2E verdict in AfiStreamMeta
sdk.RegRecvCallback([](const afi::AfiShiMessage& msg, const afi::AfiStreamMeta& meta) {
    /* msg.sensor_defect_recognised, msg.supply_voltage_status, meta.e2e_valid, ... */
});

// SPI: sensor performance and mounting pose
sdk.RegRecvCallback([](const afi::AfiSpiMessage& msg, const afi::AfiStreamMeta& meta) {
    /* msg.sensor_pose.orientation_yaw (rad), msg.fov_segments[0].blockage_status, ... */
});

// RDI send-slot loss: running totals of counted frames and lost slots
sdk.RegRecvCallback([](uint32_t total, uint32_t lost) {
    std::fprintf(stderr, "RDI: %u lost slots, %u frames counted\n", lost, total);
});

// Optional: reassembled E2E header + ISO payload before typed parsing (recorders)
sdk.RegWirePayloadCallback([](const afi::AfiWirePayloadFrame& w) {
    /* w.stream, w.payload, w.e2e_valid, w.generation */
});

if (sdk.Init(cfg) == afi::kOk) sdk.Start();
```

| 오버로드 | 호출 시점 |
|----------|-----------|
| `(const AfiRdiFrame&)` | RDI 프레임마다(`e2e_counter`, `e2e_checked`, `e2e_valid`, `generation` 포함) |
| `(const AfiShiMessage&)` / `(const AfiShiMessage&, const AfiStreamMeta&)` | SHII 메시지마다. 두 오버로드를 모두 등록하면 `AfiStreamMeta` 오버로드만 호출한다 |
| `(const AfiSpiMessage&)` / `(const AfiSpiMessage&, const AfiStreamMeta&)` | SPI 메시지마다(SHII와 같은 규칙) |
| `(uint32_t total, uint32_t lost)` | RDI E2E Counter(송신 슬롯)의 공백 발견 시점(누적값 전달) |
| `RegWirePayloadCallback(const AfiWirePayloadFrame&)` | E2E 검증 이후, 타입별 파서 이전의 모든 RDI, SHII, SPI payload |

> **콜백 스레드와 데이터 수명**
> - callback은 `Start()`를 호출한 스레드가 아니라 활성화된 스트림별 수신 스레드에서 실행된다. callback은 블로킹하지 않도록 작성하고, 공유 상태는 동기화한다.
> - 프레임 또는 메시지 참조는 callback 실행 중에만 유효하다. `AfiRdiFrame` 전체를 복사하는 것은 안전하다. 복사본은 자체 detection 벡터를 소유하며 `message.detections`를 다시 바인딩한다. `detections_storage`의 크기를 직접 바꾸거나 교체한 뒤에는 `RebindStorage()`를 호출한다.
> - E2E 검증 실패는 로그로 기록하며, 프레임은 `e2e_valid = false`와 함께 그대로 전달한다. `e2e_strict = true`이면 callback 실행 전에 폐기한다.
> - 타입별 callback은 타입별 파서가 payload를 수락한 경우에만 실행된다. wire payload callback은 같은 스레드에서 먼저 실행된다.
> - loss callback은 E2E 검증을 통과한 프레임의 E2E Counter 공백을 센다. 공백에는 레이더가 이 호스트로 보내지 않은 슬롯도 포함된다. 부팅 후 첫 scan 이전의 슬롯과 TCP 연결이 적체된 동안의 슬롯이 이에 해당한다. Counter가 감소하면(레이더 재시작) 새 기준점에서 다시 센다.
> - TCP 스트림은 연결이 끊기면 1초에서 최대 30초까지 늘어나는 backoff 간격으로 재연결한다. UDP 수신기는 `sensor_ip`에서 온 datagram만 수신한다.
> - Config API 호출과 `SendVehicleInput()`은 호출자 스레드에서 동기 방식으로 실행된다. Config API 호출은 내부에서 직렬화되지만 `GetLastSensorResult()`는 마지막으로 끝난 호출의 결과를 담으므로, Config API는 한 스레드에서 호출한다. `SendVehicleInput()`은 별도의 주기 스레드에서 실행할 수 있다.

### 모든 스트림 수신 (Python)

Python SDK에는 스트리밍 callback이 없다. RDI(30509), SHII(30510), SPI(30511)를 각각 별도 소켓으로 수신하며, 보통 스트림마다 백그라운드 스레드 하나를 둔다. 각 payload는 `afi_sdk.streams`로 파싱한다.

| 함수 | 결과 |
|------|------|
| `strip_e2e(frame, validate=True, expected_data_id=None)` | `(e2e, body)`. `e2e`는 `length_ok`, `data_id_ok`, `crc_zero`, `crc_ok`, `ok`, `generation`을 담는다 |
| `parse_rdi_frame(payload)` / `parse_rdi_detection(payload, det_start, index)` | RDI 헤더 dict / detection 1개의 dict |
| `parse_shi(payload)` | Common Header와 `operation_modes`, `defect_recognised`, `defect_reason`, `supply_voltage_status`, `temperature_status`, `input_signal_types`, `input_signal_statuses`, `time_sync`, `calibration_components`, `calibration_statuses`, `calibration_states` |
| `parse_spi(payload)` | Common Header와 `vehicle_coord_system`, `pose`, `fov_segments`, `recognisable_object_types`, `reference_target_types` |
| `SomeipTpReassembler(max_payload_size, max_sessions=8, max_segment_size=1392)` | UDP SOME/IP-TP 재조립, `feed(tp_header, payload, session_id)` |

`expected_data_id`에는 `E2E_DATA_ID_RDI`, `E2E_DATA_ID_SHII`, `E2E_DATA_ID_SPI`를 지정한다. `python/examples/sample_data_stream.py`는 이 처리를 TCP와 UDP 공용 `StreamReceiver` 클래스로 구현한다.

---

## 7. 접근 모드와 설정 저장

인터페이스 v1.1.0은 접근 모드로 Config API의 사용 범위를 제한한다. 모든 설정 SET은 진단(Diagnostic) 모드를, 펌웨어 업데이트 연쇄는 프로그래밍(Programming) 모드를 요구한다. 모드 진입은 Normal, Diagnostic, Programming 순서의 단방향이며, Normal 복귀는 재부팅 또는 레이더 자체의 통신 스택 복구로만 일어난다.

| 모드 | 값 | 개방 범위 | 데이터 스트림 |
|------|----|-----------|---------------|
| Normal | 0 | 모든 GET, `FirmwareUpdateStatus`, `GetBootInfo`, `Restart`, `GetMode`, `SetMode` | 송출 |
| Diagnostic | 1 | 위 항목과 모든 설정 SET, `ResetAllSettings`, `ResetConfigDefaults` | 송출(NVM 기록 중 최대 약 1초 지연) |
| Programming | 2 | 모든 GET, `Restart`, `GetMode`, `SetMode`, 펌웨어 업데이트 연쇄(진단 모드 메서드는 닫힌다) | 재부팅 전까지 RDI, SHII, SPI 정지(CSII는 계속 수락) |

| 동작 | C++ | Python |
|------|-----|--------|
| 현재 모드 조회 | `GetMode(AfiModeStatus& out)` | `get_mode()` → `{access_mode, access_mode_name, elapsed_ms, streams_halted}` |
| 다음 모드 진입 | `SetMode(AfiAccessMode mode, AfiModeStatus* out = nullptr)` | `set_mode(AccessMode.DIAGNOSTIC)` / `set_mode(AccessMode.PROGRAMMING)` |
| 현재 모드에서 목표 모드까지 진입 | `EnterMode(AfiAccessMode target)` | `enter_mode(AccessMode.DIAGNOSTIC)` / `enter_mode(AccessMode.PROGRAMMING)` |

| 상황 | 결과 |
|------|------|
| 진단 모드 밖에서 보낸 SET | `kSessionRequired` / `SESSION_REQUIRED` |
| `SetMode`의 현재 모드 재진입 또는 역방향 전이 | `kAccessDenied` / `ACCESS_DENIED` |
| Normal에서 호출한 `SetMode(kProgramming)` | `kSessionRequired`. `EnterMode(kProgramming)`는 Diagnostic을 거쳐 진입한다 |
| Programming에서 호출한 `EnterMode(kDiagnostic)` | `kAccessDenied`(먼저 재부팅한다) |
| `kNormal`을 지정한 `SetMode` / `EnterMode` | `kInvalidParam`(요청 미송신) |
| 현재 모드를 지정한 `EnterMode` | `kOk` |

### 설정 저장과 재시작

- 모든 SET과 `ResetAllSettings`, `ResetConfigDefaults`는 응답 전에 NVM에 기록한다(약 175 ms). `kOk`는 값이 저장되었음을 뜻한다. 별도의 저장 호출은 없다.
- 저장된 값은 `Restart()` 이후 반영된다. 그 전까지 GET은 저장된 값을 반환하므로 동작 중인 값과 다를 수 있다. `GetPtpConfig`는 두 값(`configured_profile`, `profile`)을 모두 보고한다.
- 실패한 SET은 RAM과 NVM을 변경하지 않는다. NVM 기록 실패는 `INTERNAL_ERROR`(`kSensorError`)로 응답한다.
- `Restart()`는 모든 모드에서 동작하며, 레이더가 리셋되기 전에 응답하므로 Config API 연결이 끊긴다. `ReconnectConfigApi(timeout_ms = 60000)`(C++, 500 ms 간격 재시도) 또는 `reconnect(timeout_s=60.0, interval_s=1.0)`(Python)이 레이더의 복귀를 기다린다. 재부팅 후 레이더는 Normal 모드이다.

| 설정 | C++ SET | Python SET | `restart_required` |
|------|---------|------------|--------------------|
| 레이더 네트워크와 전체 스트림 목적지 | `SetNetworkConfig`(전체 갱신) | `set_network_config(**fields)`(부분 갱신) | 항상 `true` |
| 스트림 목적지 1개 | `SetStreamDestination` | `set_stream_destination` | |
| 범위 모드 | `SetRangeMode` | `set_range_mode` | 항상 `true` |
| SNR 레벨과 CFAR 테이블 | `SetDetectionThreshold` | `set_detection_threshold` | |
| 감지 필터 | `SetDetectionFilters` | `set_detection_filters` | |
| PTP 프로파일 | `SetPtpConfig` | `set_ptp_config` / `set_ptp_config_with_result` | 저장된 프로파일이 동작 중인 프로파일과 다를 때 `true` |
| 장착 위치, 회전, 위치 코드 | `SetMountingPosition`, `SetRotation`, `SetSensorPosition` | `set_mounting_position`, `set_rotation`, `set_sensor_position` | |
| 지정 키의 기본값 복원 | `ResetConfigDefaults` | `reset_config_defaults` | |
| 전체 설정의 기본값 복원 | `ResetAllSettings` | `reset_all_settings` | 레이더가 스스로 재시작하며, IP 주소, 서브넷 마스크, 게이트웨이는 유지한다 |

모든 행은 진단 모드를 요구하며, 재시작 이후 반영된다.

```cpp
afi::AfiSdk sdk;
sdk.Init(cfg);                                   // enable_config_api = true (default)

if (sdk.EnterMode(afi::AfiAccessMode::kDiagnostic) != afi::kOk) {
    printf("EnterMode failed: %s\n", sdk.GetLastSensorResult().return_name.c_str());
    return 1;
}
bool restart_required = false;
sdk.SetRangeMode(afi::AfiRangeMode::kLongRange, &restart_required);
sdk.Restart();                                   // settings take effect after the restart
sdk.ReconnectConfigApi();                        // waits up to 60 s; the radar is back in Normal
```

```python
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, RangeMode, OK

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150"))

if sdk.enter_mode(AccessMode.DIAGNOSTIC) != OK:
    print(sdk.last_result)                       # return_code, return_name, error_message, ...
elif sdk.set_range_mode(RangeMode.LONG_RANGE) == OK:
    sdk.restart()
    sdk.reconnect()                              # waits up to 60 s
```

---

## 8. Configuration API

Config API는 데이터 스트림과 독립된 자체 TCP 연결(포트 30500)로 동작하며, 모든 호출은 동기 방식이다. 레이더는 Config API 클라이언트를 한 번에 하나만 받으며, 초당 200건을 넘는 요청에는 `BUSY`로 응답한다.

| 항목 | C++ | Python |
|------|-----|--------|
| 활성화 | 기본값은 `enable_config_api = true`이며, 끄면 호출이 `kNotInitialized`를 반환한다 | `sdk.init(config)`는 항상 연결한다. `init()`이 실패하면 `reconnect()`를 호출한다 |
| GET | `int GetX(AfiX& out)`. `kOk` 또는 음수 `AfiError`를 반환하며, `out`은 `kOk`일 때만 유효하다 | `get_x()`는 `dict`(범위 모드와 위치 코드는 `int`)를 반환하며, 오류 시 `None`을 반환한다 |
| SET / 명령 | `int SetX(...)` | `int`. 단, `set_network_config`, `set_ptp_config_with_result`, `reset_config_defaults`와 데이터를 반환하는 펌웨어 호출은 `dict` 또는 `None`을 반환한다 |
| 마지막 결과 | `GetLastSensorResult()` → `AfiSensorResult` | `last_error`, `last_result` |
| 재연결 | `ReconnectConfigApi(timeout_ms)` | `reconnect(timeout_s, interval_s)` |

타임아웃, 연결 끊김, 요청과 맞지 않는 응답이 발생하면 SDK는 소켓을 닫고, 다음 호출이 같은 endpoint로 재연결한다. 16,384바이트를 넘는 요청 본문은 송신하지 않고 `kInvalidParam` / `INVALID_PARAM`을 반환한다.

### 읽기 (C++)

```cpp
afi::AfiSdkConfig cfg;
cfg.sensor_ip = "192.168.10.150";
cfg.enable_rdi = false;                          // Config API only

afi::AfiSdk sdk;
if (sdk.Init(cfg) != afi::kOk) return 1;

afi::AfiDeviceInfo info;                         // serial, model, HW/SW version, hashes, MAC
if (sdk.GetDeviceInfo(info) == afi::kOk)
    printf("Serial: %s  SW: %s\n", info.serial_number.c_str(), info.software_version.c_str());

afi::AfiSensorStatus status;       sdk.GetSensorStatus(status);      // state, temperatures, voltage, uptime
afi::AfiDiagnosticInfo diag;       sdk.GetDiagnosticInfo(diag);      // combined state, fault list
afi::AfiNetworkConfig net;         sdk.GetNetworkConfig(net);        // radar network + stream destinations
afi::AfiDetectionThreshold thresh; sdk.GetDetectionThreshold(thresh);
afi::AfiDetectionFilters filters;  sdk.GetDetectionFilters(filters);
afi::AfiPtpConfig ptp;             sdk.GetPtpConfig(ptp);            // profiles + live sync status
afi::AfiModeStatus mode_status;    sdk.GetMode(mode_status);

afi::AfiRangeMode mode;
if (sdk.GetRangeMode(mode) == afi::kOk)
    printf("Range mode: %s\n", afi::AfiRangeModeName(mode));          // "DR" .. "UDR"
```

### 쓰기 (C++)

```cpp
if (sdk.EnterMode(afi::AfiAccessMode::kDiagnostic) != afi::kOk) return 1;

sdk.SetRangeMode(afi::AfiRangeMode::kLongRange);

// One stream destination (stored in NVM, applies after Restart)
afi::AfiStreamDestination dst;
dst.ip       = "192.168.10.100";
dst.port     = 30509;
dst.protocol = "udp";                            // "tcp" or "udp"; empty is not sent
sdk.SetStreamDestination(afi::AfiStreamKind::kDetection, dst);

// Full network update: start from the stored values and change what you need
afi::AfiNetworkConfig net;
sdk.GetNetworkConfig(net);
net.health      = {"192.168.10.100", 30510, "udp"};
net.performance = {"192.168.10.100", 30511, "udp"};
bool restart_required = false;
sdk.SetNetworkConfig(net, &restart_required);    // always true

// Mounting (metres, degrees)
afi::AfiRotation rot;
sdk.GetRotation(rot);
rot.yaw = 90.0f;
sdk.SetRotation(rot);

sdk.Restart();
sdk.ReconnectConfigApi();
```

목적지를 UDP로 바꾼 뒤에는 `AfiSdkConfig`의 `stream_transport = "udp"`(또는 `rdi_transport` / `shi_transport` / `spi_transport`)로 수신한다. 호스트의 전송 방식은 레이더가 송신하는 프로토콜과 일치해야 한다.

### 읽기 / 쓰기 (Python)

```python
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, RangeMode, OK

sdk = AfiSdk()
if sdk.init(AfiSdkConfig(sensor_ip="192.168.10.150")) != OK:
    raise SystemExit("Config API connect failed")

info   = sdk.get_device_info()                   # dict, or None on error
status = sdk.get_sensor_status()
net    = sdk.get_network_config()                # flat keys: detection_ip, detection_port, ...
mode   = sdk.get_range_mode()                    # int (RangeMode.*), or None
if info and mode is not None:
    print(info["serial_number"], info["software_version"], RangeMode.name(mode))

if sdk.enter_mode(AccessMode.DIAGNOSTIC) == OK:
    sdk.set_range_mode(RangeMode.LONG_RANGE)
    sdk.set_stream_destination("detection", "192.168.10.100", 30509, protocol="udp")
    sdk.set_network_config(health_ip="192.168.10.100", health_port=30510)   # partial update
    rot = sdk.get_rotation()                     # {yaw_deg, pitch_deg, roll_deg}
    if rot:
        sdk.set_rotation(yaw=90.0, pitch=rot["pitch_deg"], roll=rot["roll_deg"])
    sdk.restart()
    sdk.reconnect()
sdk.close()
```

`set_stream_destination(stream, ip, port, protocol="tcp")`의 `stream`은 `"detection"`, `"health"`, `"performance"` 중 하나이며, `protocol`(기본값 `"tcp"`)을 매번 송신한다.

### 범위 모드 값

| 값 | C++ `afi::AfiRangeMode` | Python `RangeMode` | 이름 |
|----|-------------------------|--------------------|------|
| 0 | `kDualRange` | `DUAL_RANGE` | DR |
| 1 | `kLongRange` | `LONG_RANGE` | LR |
| 2 | `kMidRange` | `MID_RANGE` | MR |
| 3 | `kShortRange` | `SHORT_RANGE` | SR |
| 4 | `kUltraLongRange` | `ULTRA_LONG_RANGE` | ULR |
| 5 | `kUltraShortRange` | `ULTRA_SHORT_RANGE` | USR |
| 6 | `kUltraDualRange` | `ULTRA_DUAL_RANGE` | UDR(DR과 같은 2개 subcycle) |

6을 넘는 값은 송신하지 않고 `kInvalidParam` / `INVALID_PARAM`을 반환한다. `AfiRangeModeName(mode)`와 `RangeMode.name(mode)`는 모드 이름을 반환한다.

### 감지 튜닝

```cpp
// C++: SNR level and 19-entry CFAR tables (levels 1..9, not physical dB)
afi::AfiDetectionThreshold thresh;
sdk.GetDetectionThreshold(thresh);
thresh.snr_dB = 6.0f;                            // wire key snr_threshold_db; fractions allowed
thresh.cfar_table_first_frame.assign(afi::AfiDetectionThreshold::kCfarTableLength, 5);
sdk.SetDetectionThreshold(thresh);               // second-frame table is used in DR and UDR only

// Eight filter toggles; the SDK sends all eight
afi::AfiDetectionFilters filters;
sdk.GetDetectionFilters(filters);
filters.filter_ground_reflection = true;
filters.filter_multibounce_2x = true;
sdk.SetDetectionFilters(filters);
```

```python
# Python (any subset; values outside 1..9 or tables not of 19 entries give INVALID_PARAM)
sdk.set_detection_threshold(snr_db=6.0, cfar_table_first_frame=[5] * 19)
sdk.set_detection_filters(filter_ground_reflection=True, filter_multibounce_2x=True)
```

| 필터 | 기본값 |
|------|--------|
| `filter_ego_specular` | `false` |
| `filter_ground_sidelobe` | `true` |
| `filter_upper_structure` | `false` |
| `filter_multibounce_2x` | `true` |
| `filter_structure_multipath` | `true` |
| `filter_specular_mirror` | `true` |
| `filter_wheel_microdoppler` | `false` |
| `filter_ground_reflection` | `false` (SR subcycle) |

인터페이스 v1.1.0 펌웨어는 SNR 레벨을 저장하지만 감지에는 사용하지 않는다. `filter_ego_specular`, `filter_upper_structure`, `filter_wheel_microdoppler`도 저장만 하며 해당 처리 단계를 구성하지 않는다.

### 시간 동기 (PTP)

`SetPtpConfig(profile)` / `SetPtpConfig(profile, &restart_required)`는 `0`(gPTP), `1`(1588v2-L2), `2`(1588v2-UDP) 중 하나를 저장하며, 2를 넘는 값은 `kInvalidParam`을 반환한다. `GetPtpConfig`는 동작 중인 `profile`, 저장된 `configured_profile`, `restart_required`, 실시간 `state`(`"INIT"`, `"SLAVE"`, `"NOT_SYNCED"`), `offset_ns`, `mean_path_delay_ns`, `master_clock_id`, 그리고 시스템 시간과 PTP 시간의 차이를 나타내는 `grid_offset_ns` / `grid_state`를 반환한다. Python에서는 `get_ptp_config()`, `set_ptp_config(profile=0)`, `set_ptp_config_with_result(profile=0)`을 사용한다.

### 전체 Config API 메서드 카탈로그

| 범주 (Method ID) | C++ | Python |
|------------------|-----|--------|
| 연결(SDK 측) | `GetLastSensorResult`, `GetInterfaceGeneration`, `ReconnectConfigApi` | `last_error`, `last_result`, `interface_generation`, `reconnect`, `close`, `connected` |
| Access Mode (`0x00E0`–`0x00E1`) | `GetMode`, `SetMode`, `EnterMode` | `get_mode`, `set_mode`, `enter_mode` |
| Device & Status (`0x0010`–`0x0016`) | `GetDeviceInfo`, `GetSensorStatus`, `GetDiagnosticInfo`, `Restart`, `ResetAllSettings`, `GetCommandHistoryInfo` | `get_device_info`, `get_sensor_status`, `get_diagnostic_info`, `restart`, `reset_all_settings`, `get_command_history_info` |
| Network (`0x0020`–`0x0021`) | `GetNetworkConfig`, `SetNetworkConfig`, `SetStreamDestination` | `get_network_config`, `set_network_config`, `set_stream_destination` |
| Range Mode (`0x0030`–`0x0031`) | `GetRangeMode`, `SetRangeMode` | `get_range_mode`, `set_range_mode` |
| Detection (`0x0040`–`0x0043`) | `GetDetectionThreshold`, `SetDetectionThreshold`, `GetDetectionFilters`, `SetDetectionFilters` | `get_detection_threshold`, `set_detection_threshold`, `get_detection_filters`, `set_detection_filters` |
| Time Sync (`0x0050`–`0x0051`) | `GetPtpConfig`, `SetPtpConfig` | `get_ptp_config`, `set_ptp_config`, `set_ptp_config_with_result` |
| Mounting (`0x0070`–`0x0075`) | `GetMountingPosition`, `SetMountingPosition`, `GetRotation`, `SetRotation`, `GetSensorPosition`, `SetSensorPosition` | `get_mounting_position`, `set_mounting_position`, `get_rotation`, `set_rotation`, `get_sensor_position`, `set_sensor_position` |
| Firmware Update (`0x0080`–`0x0087`) | `FirmwareUpdateStart`, `FirmwareDataChunk`, `FirmwareUpdateCommit`, `FirmwareUpdateStatus`, `FirmwareUpdateAbort`, `FirmwareSwitchSlot`, `FirmwareUpdateReboot`, `GetBootInfo` | `firmware_update_start`, `firmware_data_chunk`, `firmware_update_commit`, `firmware_update_status`, `firmware_update_abort`, `firmware_switch_slot`, `firmware_update_reboot`, `get_boot_info` |
| Config Persistence (`0x0091`–`0x0092`) | `GetConfigStatus`, `ResetConfigDefaults` | `get_config_status`, `reset_config_defaults` |

시그니처와 필드 표는 [docs/api_reference.md](docs/api_reference.md)에서 확인한다.

---

## 9. 펌웨어 업데이트

펌웨어 업데이트 연쇄는 프로그래밍 접근 모드에서 수행하며, 이 모드는 재부팅 전까지 RDI, SHII, SPI 송출을 정지한다. 대상 레이더는 인터페이스 v1.1.0이어야 한다.

| `update_type` | 컴포넌트 | 기록 위치 |
|---------------|----------|-----------|
| `"firmware"` | `m7_0`, `m7_1`, `m7_2`, `a53` (4종 모두 필수) | 비활성 슬롯(`target_slot`), `FirmwareSwitchSlot`으로 활성화 |
| `"bootloader"` | `bootloader` | 제자리 기록과 백업 사본(슬롯 전환 미수행) |

| 단계 | C++ | Python (`AfiSdk`) | ID |
|------|-----|-------------------|----|
| 세션 시작 | `FirmwareUpdateStart(const AfiOtaStartRequest&, AfiOtaStartResponse&)` | `firmware_update_start(update_type, component, total_size, crc32)` | `0x0080` |
| 조각 송신 | `FirmwareDataChunk(const uint8_t*, size_t, AfiOtaChunkResult*)` | `firmware_data_chunk(data)` → `{received_bytes, total_bytes, ...}` | `0x0081` |
| 커밋(검증, 기록) | `FirmwareUpdateCommit()` | `firmware_update_commit()` | `0x0082` |
| 상태 폴링 | `FirmwareUpdateStatus(AfiOtaStatus&)` | `firmware_update_status()` | `0x0083` |
| 중단 | `FirmwareUpdateAbort()` | `firmware_update_abort()` | `0x0084` |
| 기록한 슬롯 활성화 | `FirmwareSwitchSlot("firmware", AfiOtaSwitchSlotResult&)` | `firmware_switch_slot("firmware")` | `0x0085` |
| 재부팅 | `FirmwareUpdateReboot()` | `firmware_update_reboot()` | `0x0086` |
| 슬롯 상태 | `GetBootInfo(AfiBootInfo&)` | `get_boot_info()` | `0x0087` |

레이더가 강제하는 규칙은 다음과 같다.

- `FirmwareUpdateStatus`와 `GetBootInfo`를 제외한 모든 단계는 프로그래밍 모드를 요구한다.
- 레이더는 업데이트 세션을 하나만 유지한다. 커밋과 슬롯 전환은 응답 이후에도 진행되므로, 다음 시작, 전환, 재부팅 전에 `state`가 `"idle"`이 될 때까지 `FirmwareUpdateStatus`를 폴링한다.
- 조각은 `max_chunk_size`(4096)를 넘지 않는다. 레이더는 조각의 일부만 수락할 수 있으며, 이때 `received_bytes`가 다음 오프셋이고 나머지는 다시 송신한다. C++에서는 `AfiOtaChunkResult` 오버로드를 사용한다.
- 레이더는 업데이트 요청 없이 10초가 지나면 전송 중인 세션을 중단한다.
- `crc32`는 이미지의 IEEE CRC-32이며 `"0x%08X"` 형식으로 송신한다. C++의 빈 문자열이나 Python의 `None`은 키를 생략하며, 이 경우 레이더는 이미지 검사를 생략한다.
- 슬롯 전환은 펌웨어 컴포넌트 4종의 커밋이 모두 `"idle"`에 도달한 뒤에만 수행한다. 재부팅은 상태가 `"idle"`일 때만 송신한다.
- 시작 이후 실패가 발생하면 `FirmwareUpdateAbort()`를 호출하고 해당 컴포넌트를 처음부터 다시 시작한다. 슬롯 전환 전에 전송이 중단되면 레이더는 이전 슬롯으로 부팅한다.
- `FirmwareUpdateStatus`가 반환하는 `AfiOtaStatus::error_message`는 비어 있다. 실패한 단계의 사유는 `GetLastSensorResult().error_message` / `last_result["error_message"]`에서 확인한다.

### C++ 절차

```cpp
#include <afi/afi_sdk.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <vector>

// IEEE 802.3 CRC-32 (zlib), sent by FirmwareUpdateStart as "0x%08X".
static std::string Crc32Hex(const std::vector<uint8_t>& d) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    char s[11];
    std::snprintf(s, sizeof(s), "0x%08X", static_cast<unsigned>(~c));
    return s;
}

// Poll FirmwareUpdateStatus until "idle".
static int WaitIdle(afi::AfiSdk& sdk, int timeout_s) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    while (std::chrono::steady_clock::now() < deadline) {
        afi::AfiOtaStatus st;
        int rc = sdk.FirmwareUpdateStatus(st);
        if (rc != afi::kOk) return rc;
        if (st.state == "idle") return afi::kOk;
        if (st.state == "error") return afi::kSensorError;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return afi::kTimeout;
}

// Start, chunks (resume at received_bytes), commit, wait for idle. Aborts on failure.
static int Upload(afi::AfiSdk& sdk, const char* type, const char* component,
                  const std::vector<uint8_t>& image, const std::string& expected_slot) {
    afi::AfiOtaStartRequest req;
    req.update_type = type;
    req.component   = component;
    req.total_size  = static_cast<uint32_t>(image.size());
    req.crc32       = Crc32Hex(image);
    afi::AfiOtaStartResponse start;
    int rc = sdk.FirmwareUpdateStart(req, start);
    if (rc != afi::kOk) return rc;
    if (!expected_slot.empty() && start.target_slot != expected_slot) {
        sdk.FirmwareUpdateAbort();
        return afi::kInvalidState;
    }

    size_t sent = 0;
    int stalled = 0;
    while (sent < image.size()) {
        const size_t len = std::min<size_t>(start.max_chunk_size, image.size() - sent);
        afi::AfiOtaChunkResult chunk;
        rc = sdk.FirmwareDataChunk(image.data() + sent, len, &chunk);
        if (rc == afi::kOk && chunk.received_bytes > image.size()) rc = afi::kProtocolError;
        if (rc == afi::kOk && chunk.received_bytes <= sent && ++stalled > 200) rc = afi::kTimeout;
        if (rc != afi::kOk) { sdk.FirmwareUpdateAbort(); return rc; }
        if (chunk.received_bytes > sent) { sent = chunk.received_bytes; stalled = 0; }
    }
    rc = sdk.FirmwareUpdateCommit();
    if (rc == afi::kOk)   // budget: max(120 s, size at 50 KiB/s)
        rc = WaitIdle(sdk, std::max(120, static_cast<int>(image.size() / (50 * 1024))));
    if (rc != afi::kOk) sdk.FirmwareUpdateAbort();
    return rc;
}

// images holds "m7_0", "m7_1", "m7_2" and "a53" (all four are required).
int UpdateFirmware(afi::AfiSdk& sdk, const std::map<std::string, std::vector<uint8_t>>& images) {
    if (sdk.EnterMode(afi::AfiAccessMode::kProgramming) != afi::kOk) return 1;   // halts RDI/SHII/SPI

    afi::AfiBootInfo boot;
    if (sdk.GetBootInfo(boot) != afi::kOk) return 1;
    const std::string target = boot.fw_update_active == "A" ? "B"
                             : boot.fw_update_active == "B" ? "A" : "";
    if (target.empty()) return 1;

    for (const char* comp : {"m7_0", "m7_1", "m7_2", "a53"})
        if (Upload(sdk, "firmware", comp, images.at(comp), target) != afi::kOk) return 1;

    afi::AfiOtaSwitchSlotResult sw;                       // after all four commits reached idle
    if (sdk.FirmwareSwitchSlot("firmware", sw) != afi::kOk || WaitIdle(sdk, 60) != afi::kOk) return 1;
    if (sdk.GetBootInfo(boot) != afi::kOk || boot.fw_update_active != target) return 1;

    sdk.FirmwareUpdateReboot();                           // the connection drops after the reply
    if (sdk.ReconnectConfigApi(120000) != afi::kOk) return 1;

    afi::AfiDeviceInfo info;                              // the running image after the reboot
    if (sdk.GetBootInfo(boot) != afi::kOk || sdk.GetDeviceInfo(info) != afi::kOk) return 1;
    printf("active=%s status=%s fw_hash=%s\n", boot.fw_update_active.c_str(),
           boot.boot_info_status.c_str(), info.fw_hash.c_str());
    return (boot.fw_update_active == target && boot.boot_info_status == "ok") ? 0 : 1;
}
```

부트로더 업데이트는 프로그래밍 모드에서 펌웨어 컴포넌트보다 먼저 `Upload(sdk, "bootloader", "bootloader", image, "")`로 수행하며, `"idle"`에 도달하면 완료된다. 재부팅 여부는 `afi_sdk.firmware.wait_for_restart()`와 같은 방식으로 Discovery의 `uptime_seconds`를 업데이트 전후로 비교하여 확인하고, `GetDeviceInfo().fw_hash`를 기대 해시와 비교한다.

### Python 절차 (`afi_sdk.firmware`)

`afi_sdk.firmware`는 연결된 `AfiSdk`에서 전체 절차를 수행한다. 컴포넌트 이미지는 bytes로 받으며, 번들 추출, 복호화, 버전 선택은 호출자가 담당한다.

```python
from afi_sdk import AfiSdk, AfiSdkConfig, firmware

IP = "192.168.10.150"
EXPECTED_FW_HASH = None                          # git short hash of the release, if known

sdk = AfiSdk()
sdk.init(AfiSdkConfig(sensor_ip=IP))

images = {}
for name in firmware.FIRMWARE_COMPONENTS:        # "m7_0", "m7_1", "m7_2", "a53"
    with open(f"{name}.bin", "rb") as f:
        images[name] = f.read()

uptime = firmware.radar_uptime(IP)               # Discovery uptime before the update
result = firmware.update(sdk, firmware=images, bootloader=None, reboot=True,
                         on_progress=lambda step, state, pct, rx, total: print(step, state, pct))
if not result.ok:
    print("update failed:", result.error)        # "<step>: <reason>"
elif result.rebooted:
    if uptime is not None:
        firmware.wait_for_restart(IP, uptime)    # Discovery shows a lower uptime
    sdk.reconnect()
    problems = firmware.verify(sdk, result.target_face, fw_hash=EXPECTED_FW_HASH)
    print(problems or "verified")
sdk.close()
```

`update()`는 Diagnostic을 거쳐 프로그래밍 모드에 진입하고, 부트로더 이미지가 있으면 먼저 업로드한다. 이어서 `m7_0`, `m7_1`, `m7_2`, `a53`을 비활성 슬롯에 업로드하며 컴포넌트마다 `target_slot`을 확인한다. 모든 커밋이 idle에 도달하면 슬롯을 한 번 전환하고 Boot-Info로 결과를 확인하며, `reboot=True`이면 세션이 idle인 상태에서 `FirmwareUpdateReboot`를 송신한다. 첫 실패에서 절차를 중단한다. 4종 컴포넌트를 정확히 담지 않은 `firmware` dict는 아무것도 송신하지 않고 거부한다.

| 함수 / 타입 | 설명 |
|-------------|------|
| `update(sdk, firmware=None, bootloader=None, reboot=True, on_progress=None) -> UpdateResult` | 위의 전체 절차 |
| `upload_component(sdk, update_type, component, image, crc32=None, on_progress=None) -> StepResult` | 시작, `received_bytes`가 크기와 같아질 때까지 조각 송신, 커밋, idle 대기를 수행하며, 실패 시 중단을 송신한다 |
| `wait_idle(sdk, timeout_s, step="", on_progress=None, total=0) -> Optional[str]` | `"idle"`까지 상태를 폴링하며 `None` 또는 실패 사유를 반환한다 |
| `radar_uptime(ip, timeout_s=1.5, discover=None) -> Optional[int]` | `ip` 레이더의 Discovery `scan_status.uptime_seconds` |
| `wait_for_restart(ip, uptime_before, timeout_s=120.0, interval_s=0.5, discover=None) -> Optional[dict]` | Discovery가 더 낮은 uptime을 보고할 때까지 대기하며 Discovery 레코드 또는 `None`을 반환한다 |
| `verify(sdk, target_face, fw_hash=None, bootloader_hash=None) -> List[str]` | Boot-Info(활성, 유효, `boot_info_status` `"ok"`)와 장치 해시를 확인하며, 모두 통과하면 빈 리스트를 반환한다 |
| `hashes_match(reported, expected) -> bool` | 7자 이상 공통 접두부로 git short hash 비교(`-dirty`와 대소문자 무시) |
| `image_crc32(image) -> int` | `FirmwareUpdateStart`가 검사하는 IEEE CRC-32 |
| `commit_timeout_s(size) -> float` | `max(120 s, size / 50 KiB/s)` |
| `StepResult` | `step`, `ok`, `error`, `target_slot` |
| `UpdateResult` | `ok`, `steps`(`StepResult` 리스트), `target_face`(`"A"`/`"B"`, 부트로더만 갱신한 경우 `None`), `rebooted`, `error`(property, `"<step>: <reason>"`) |

`on_progress(step, state, percent, received_bytes, total_bytes)`는 전송과 idle 대기 중에 호출된다. 모듈 상수는 `FIRMWARE_COMPONENTS`, `BOOTLOADER_COMPONENT`, `DEFAULT_MAX_CHUNK_SIZE`(4096), `MAX_STALLED_CHUNKS`(200), `STEP_MODE`, `STEP_BOOT_INFO`, `STEP_SWITCH`, `STEP_REBOOT`이다.

---

## 10. 차량 입력 (CSII)

`SendVehicleInput()`은 CSII 메시지 1건(Event `0x8001`, 115바이트 프레임, SOME/IP 16 B + E2E 20 B + ISO payload 79 B)을 `sensor_ip:csii_port`(기본 30501)로 송신한다. 호출 주기는 **100 ms**를 권장한다. SDK는 인터페이스 헤더를 채우고 메시지 카운터와 E2E 카운터를 관리하며, 첫 호출 시 소켓을 생성한다.

| 항목 | 동작 |
|------|------|
| 선행 조건 | C++는 `Init()` 이후 호출하며 `Start()`는 필요하지 않다. Python은 `init()` 이후 호출한다 |
| 전송 방식 | 인터페이스 세대에 따라 v1.1.0은 UDP datagram 1개, v1.0.x는 TCP 연결을 사용한다. 세대를 모르는 동안에는 UDP를 사용하며, `csii_transport = "udp"` / `"tcp"`로 강제한다 |
| 스레드 | 호출자 스레드에서 실행되며 내부에서 직렬화된다 |
| 오류 | C++는 `Init()` 전에 `kNotInitialized`를, 소켓 생성·연결·송신 실패 시 `kConnectFailed`를 반환한다. Python은 `NOT_INITIALIZED`, `DISCONNECTED`를 반환한다 |
| 접근 모드 | 모든 모드에서 수락된다 |
| 인터페이스 v1.1.0 펌웨어 | 수신 값을 저장한다. SOMC와 global timestamp는 센서 동작을 바꾸지 않는다 |

```cpp
// C++
afi::AfiVehicleInput vi;                         // sensor_id 1, somc NORMAL_MODE, rear-axle frame
vi.velocity_x_mps = 10.0f;                       // m/s forward
vi.rotation_rate_yaw_rps = 0.05f;                // rad/s
vi.steering_angle_rad = 0.02f;
vi.gear_position = BTS_CSII_GP_FORWARD;
int rc = sdk.SendVehicleInput(vi);               // timestamp_ns = 0 uses the host clock
```

```python
# Python
from afi_sdk import streams

rc = sdk.send_vehicle_input(
    velocity_mps=(10.0, 0.0, 0.0),               # x, y, z (m/s)
    rotation_rate_rps=(0.05, 0.0, 0.0),          # yaw, pitch, roll (rad/s)
    steering_angle_rad=0.02,
    gear_position=streams.GEAR_FORWARD,
)
```

| `AfiVehicleInput` 필드 | Python 키워드 | 기본값 |
|------------------------|---------------|--------|
| `sensor_id` | `sensor_id` | `1` |
| `timestamp_ns` | `timestamp_ns` | `0` (호스트 시계) |
| `somc` | `somc` | `BTS_CSII_SOMC_NORMAL_MODE` / `streams.SOMC_NORMAL_MODE` |
| `global_timestamp_utc` | `global_timestamp_utc` | `0.0` |
| `coord_system` | `coord_system` | `BTS_CSII_VCS_REAR_AXLE` / `0` |
| `velocity_x_mps`, `velocity_y_mps`, `velocity_z_mps` | `velocity_mps=(x, y, z)` | `0` |
| `rotation_rate_yaw_rps`, `rotation_rate_pitch_rps`, `rotation_rate_roll_rps` | `rotation_rate_rps=(yaw, pitch, roll)` | `0` |
| `wheel_flp_rps`, `wheel_frp_rps`, `wheel_rlp_rps`, `wheel_rrp_rps` | `wheel_speeds_rps=(fl, fr, rl, rr)` | `0` |
| `steering_angle_rad` | `steering_angle_rad` | `0` |
| `gear_position` | `gear_position` | `BTS_CSII_GP_NEUTRAL` / `streams.GEAR_NEUTRAL` |

---

## 11. 센서 탐색 (Discovery)

Discovery는 UDP 30520으로 요청을 한 번 브로드캐스트하고, timeout 구간 1회(기본 3초) 동안 응답을 수집한다. 각 센서는 unicast와 limited broadcast로 두 번 응답하며, SDK는 (송신 IP, 시리얼 번호)마다 응답 하나를 유지한다. 응답은 누락될 수 있으므로, 센서가 없다고 판단하기 전에 다시 탐색한다. NIC가 여러 개인 호스트에서는 센서 서브넷에 연결된 NIC의 로컬 IP를 지정한다. Discovery는 모든 접근 모드에서 응답하며 `Init()`이 필요하지 않다.

```cpp
// C++: static, no instance needed. Discover(timeout_ms = 3000, port = 30520, bind_ip = "")
auto sensors = afi::AfiSdk::Discover(3000, 30520, "192.168.10.100");
for (const auto& s : sensors) {
    printf("%s  %s  sw=%s  gen=%d\n", s.serial_number.c_str(), s.ip.c_str(),
           s.software_version.c_str(), static_cast<int>(s.generation));   // 1 = v1.0.x, 2 = v1.1.0
}
```

```python
# Python: list of dicts as the sensor sent them, plus _source_ip, _source_port, _interface_generation
from afi_sdk import AfiSdk, INTERFACE_GEN_V1_0

for s in AfiSdk.discover(timeout_s=3.0, bind_ip="192.168.10.100"):
    print(s["_source_ip"], s.get("serial_number"), s.get("software_version"),
          s.get("ip_address"), (s.get("scan_status") or {}).get("uptime_seconds"),
          "v1.0.x" if s["_interface_generation"] == INTERFACE_GEN_V1_0 else "")
```

| 주요 `AfiSensorInfo` 필드 | 설명 |
|---------------------------|------|
| `serial_number`, `model`, `hw_version`, `software_version`, `mac_address` | 장치 식별 정보와 통합 소프트웨어 버전 |
| `bootloader_hash`, `fw_hash` | 동작 중인 이미지의 git short hash |
| `ip`, `source_ip` | 센서가 보고한 주소(없으면 `source_ip`) / 응답을 보낸 주소 |
| `subnet_mask`, `gateway`, `port` | 레이더 네트워크(`port`는 Config API 포트 30500) |
| `detection_port`, `health_port`, `performance_port` | RDI / SHII / SPI 포트 |
| `detection_protocol`, `health_protocol`, `performance_protocol` | `"tcp"` 또는 `"udp"` |
| `detection_ip`, `health_ip`, `performance_ip`, `host_ip` | 스트림 목적지 IP |
| `range_mode`, `range_mode_name` | 저장된 범위 모드 |
| `position_code`, `offset_x/y/z`, `yaw_deg`, `pitch_deg`, `roll_deg` | 장착 값 |
| `radar_active`, `scan_index`, `uptime_seconds` | 동작 상태, 현재 MessageCounter, uptime(초) |
| `ptp_state`, `ptp_offset_ns` | PTP 상태 |
| `generation` | 응답에 `driver_hash`가 있으면 `kV1_0`, `m72_*` 키가 있으면 `kV1_1` |

Python dict는 레이더 IP를 `ip_address`에, uptime을 `scan_status.uptime_seconds`에 담는다.

---

## 12. 멀티 센서 설정

SDK는 최대 **4개 센서**의 동시 운용을 지원한다. 센서마다 독립된 `AfiSdk` 인스턴스를 하나씩 사용하며, 각 인스턴스는 자체 소켓, 스레드, callback을 가진다.

- **TCP 스트림(기본값)**에서는 각 인스턴스가 자기 센서의 IP로 접속하므로 모든 센서가 기본 스트림 포트를 그대로 사용한다. `sensor_ip`만 다르게 지정한다.
- **UDP 스트림**에서는 호스트가 각 스트림 포트를 bind하므로 센서마다 별도의 호스트 포트가 필요하다. 관례는 센서당 `+100`이다([포트 표](#16-포트--프로토콜-레퍼런스)). 진단 모드에서 센서별 목적지를 기록하고 센서를 재시작한 뒤, 해당 인스턴스의 `AfiSdkConfig`에 같은 포트를 지정한다. 각 UDP 수신기는 자기 센서의 datagram만 수신한다.
- Config API는 각 센서의 30500 포트로 나가는 TCP 연결이므로 오프셋이 필요하지 않다.

```cpp
// C++: one instance per sensor (TCP, default ports)
#include <afi/afi_sdk.h>
#include <cstdio>
#include <memory>
#include <vector>

const char* ips[] = {"192.168.10.150", "192.168.10.151"};

std::vector<std::unique_ptr<afi::AfiSdk>> sdks;
for (const char* ip : ips) {
    afi::AfiSdkConfig cfg;
    cfg.sensor_ip = ip;
    cfg.enable_rdi = true;
    cfg.enable_config_api = false;
    // UDP instead: cfg.stream_transport = "udp"; cfg.rdi_port = 30509 + 100 * index; (same for shi_port, spi_port)

    auto sdk = std::make_unique<afi::AfiSdk>();
    sdk->RegRecvCallback([ip](const afi::AfiRdiFrame& f) {
        printf("[%s] detections=%u\n", ip, f.message.num_detections);
    });
    if (sdk->Init(cfg) == afi::kOk && sdk->Start() == afi::kOk)
        sdks.push_back(std::move(sdk));
}
```

```python
# Python: write per-sensor UDP destinations (+100 per sensor), then restart each sensor
from afi_sdk import AfiSdk, AfiSdkConfig, AccessMode, OK

HOST_IP = "192.168.10.100"                       # host NIC on the sensor subnet
PORT_OFFSET_PER_SENSOR = 100

for idx, ip in enumerate(["192.168.10.150", "192.168.10.151"]):
    offset = idx * PORT_OFFSET_PER_SENSOR
    sdk = AfiSdk()
    if sdk.init(AfiSdkConfig(sensor_ip=ip)) != OK or sdk.enter_mode(AccessMode.DIAGNOSTIC) != OK:
        print(ip, "refused:", sdk.last_result)
        sdk.close()
        continue
    resp = sdk.set_network_config(               # partial update; NVM; applies after restart
        detection_ip=HOST_IP,   detection_port=30509 + offset,   detection_protocol="udp",
        health_ip=HOST_IP,      health_port=30510 + offset,      health_protocol="udp",
        performance_ip=HOST_IP, performance_port=30511 + offset, performance_protocol="udp",
    )
    if resp is not None:
        sdk.restart()
        sdk.reconnect(timeout_s=90)
    sdk.close()
```

---

## 13. 인터페이스 v1.0.x 호환

SDK 2.2.0은 인터페이스 v1.1.0 센서와 함께 인터페이스 v1.0.x(펌웨어 3.0.0~3.1.0) 센서의 데이터를 수신한다. Config API와 펌웨어 업데이트는 인터페이스 v1.1.0에서만 동작한다. v1.0.x 지원은 이번 릴리스로 종료한다.

| 기능 | 인터페이스 v1.1.0 | 인터페이스 v1.0.x (FW 3.0.0~3.1.0) |
|------|------------------------------|-------------------------------------|
| Discovery | 지원 | 지원 |
| RDI, SHII, SPI 수신 | 지원 | 지원 |
| E2E CRC | 본문 대상 CRC-32(IEEE 802.3), CRC 상위 워드 0 | Length, Counter, Data ID, 본문 대상 CRC-64/XZ(0~7바이트), 수신 시 허용 |
| CSII | 30501 포트로 UDP datagram 1개 | 30501 포트로 TCP 연결 |
| Config API | 지원 | `kNotSupported` / `NOT_SUPPORTED`(요청 미송신) |
| 펌웨어 업데이트 | 지원 | `kNotSupported` / `NOT_SUPPORTED`(요청 미송신) |

SDK가 세대를 판정하는 방식은 다음과 같다.

- Config API 연결은 매번 읽기 전용 GetDeviceInfo 요청으로 시작한다. v1.0.x 응답이 오면 연결을 닫고, 다음 연결 전까지 이후의 Config API 호출은 송신 없이 `kNotSupported`를 반환한다. 이때 `GetLastSensorResult()`는 `NOT_SUPPORTED`와 `error_message`를 담으며, Python `connected`는 `False`이다.
- Config API를 끈 구성에서는 E2E 검증을 통과한 마지막 스트림 프레임으로 세대를 판정한다.
- `GetInterfaceGeneration()`(C++)은 `kUnknown`(0), `kV1_0`(1), `kV1_1`(2)을, `interface_generation`(Python)은 `INTERFACE_GEN_UNKNOWN`, `INTERFACE_GEN_V1_0`, `INTERFACE_GEN_V1_1`을 보고한다.
- 프레임 단위 세대는 `AfiRdiFrame`, `AfiStreamMeta`, `AfiWirePayloadFrame`의 `generation`과 Python `strip_e2e()`의 `e2e["generation"]`에 담긴다. 센서 단위 세대는 `AfiSensorInfo::generation`과 Python Discovery 키 `_interface_generation`(`discovery_generation(info)`)에 담긴다.
- Config API를 끈 구성에서 첫 스트림 프레임을 받기 전에는 세대를 알 수 없으므로 CSII는 UDP를 사용한다. 첫 호출부터 v1.0.x 센서에 전달하려면 `csii_transport = "tcp"`를 지정한다.

스트림 layout은 두 세대에서 같으며, SDK는 모든 값을 변환하지 않고 전달한다. 아래 필드는 v1.0.x에서 의미가 다르며, 두 세대는 Common Header의 인터페이스 버전(`1.0.0` 또는 `1.1.0`)으로 구분한다.

| 필드 | 인터페이스 v1.0.x | 인터페이스 v1.1.0 |
|------|-------------------|-------------------|
| RDI `existence_probability` | 표지값 100, 60, 40 | 확률 × 100, 증강 detection은 95 또는 90 |
| RDI DataQualifier | 0, 5(시험 모드), 6 | 0, 1, 6 |
| RDI MessageCounter | 송신 카운터 | scan index, DataQualifier 6 재송신에서 반복 |
| RDI RecognisedDetectionsCapability | 4096(FW 3.0.x) 또는 8192 | 2048 |
| RDI 반경 속도 영역 | ±50 m/s(FW 3.0.0) | −111.11 m/s부터 |
| SHII 센서 운영 모드 | `150` 발생 가능 | `6`(UDR) 발생 가능 |
| SHII 입력 신호 상태 | 1개 | 4개 |
| E2E Counter | 스트림별, TCP 연결마다 1부터 증가 | RDI, SHII, SPI가 공유하는 송신 슬롯 |

---

## 14. 주요 데이터 구조

### `AfiSdkConfig` (C++)

| 필드 | 기본값 | 설명 |
|------|--------|------|
| `sensor_ip` | `"192.168.10.150"` | 센서 IP이며, UDP 수신기는 이 주소에서 온 datagram만 수신한다 |
| `config_port` | `30500` | Config API TCP 포트 |
| `rdi_port` | `30509` | RDI 포트(TCP는 접속할 센서 포트, UDP는 bind할 호스트 포트) |
| `shi_port` | `30510` | SHII 포트 |
| `spi_port` | `30511` | SPI 포트 |
| `csii_port` | `30501` | CSII 목적지 포트 |
| `stream_transport` | `"tcp"` | 기본 스트림 전송 방식, `"tcp"` 또는 `"udp"` |
| `rdi_transport`, `shi_transport`, `spi_transport` | 빈 값 | 스트림별 덮어쓰기 값이며, 빈 값이면 `stream_transport`를 사용한다 |
| `csii_transport` | 빈 값 | 빈 값이면 세대에 따라 선택한다(v1.1.0은 UDP, v1.0.x는 TCP, 미판정 시 UDP). `"udp"` 또는 `"tcp"`로 강제한다 |
| `enable_rdi` | `true` | RDI 수신 |
| `enable_shi` | `false` | SHII 수신 |
| `enable_spi` | `false` | SPI 수신 |
| `enable_config_api` | `true` | `Init()`에서 Config API를 연결한다. 연결 실패는 치명적이지 않으며, 이후 호출이 필요 시 연결한다 |
| `validate_e2e` | `true` | 수신 프레임의 E2E CRC 검증(Length와 Data ID는 항상 검증한다) |
| `e2e_strict` | `false` | E2E 검증에 실패한 프레임을 폐기한다. 꺼져 있으면 경고 후 전달한다 |
| `socket_buffer_size` | `16 MiB` | TCP/UDP 수신기의 `SO_RCVBUF` |
| `connect_timeout_ms` | `5000` | TCP 연결 타임아웃 |
| `request_timeout_ms` | `5000` | Config API 응답 타임아웃 |

스트림 포트와 전송 방식은 호스트의 수신 위치를 뜻한다. 레이더는 `SetNetworkConfig` / `SetStreamDestination`으로 저장한 목적지로 송신하며, 이 값은 `Restart()` 이후 반영된다.

Python `AfiSdkConfig` 데이터클래스는 `sensor_ip`, `config_port`, `csii_port`, `rdi_port`, `shi_port`, `spi_port`, `timeout`(`5.0`초, Config API 연결과 응답), `stream_transport`, `validate_e2e`, `csii_transport` 필드를 가진다. `AfiSdk`는 `sensor_ip`, `config_port`, `csii_port`, `timeout`, `csii_transport`를 사용하며, 스트림 필드는 호출자의 수신 코드에서 사용한다.

### `AfiRdiFrame`

| 필드 | 타입 | 설명 |
|------|------|------|
| `message` | `bts_rdi_message_t` | ISO 23150 RDI 메시지이며, `message.detections`는 `detections_storage`를 가리킨다 |
| `recv_timestamp_ns` | `uint64_t` | 호스트 수신 시각(steady clock) |
| `frame_seq` | `uint32_t` | SDK 프레임 시퀀스 번호 |
| `detections_storage` | `std::vector<bts_rdi_detection_t>` | 소유한 detection 배열 |
| `e2e_counter` | `uint32_t` | E2E Counter(송신 슬롯) |
| `e2e_checked` | `bool` | `validate_e2e` 활성 여부 |
| `e2e_valid` | `bool` | Length, Data ID, CRC 검증 통과 여부 |
| `generation` | `AfiInterfaceGeneration` | E2E 헤더로 판정한 송신 측 세대 |

`message`는 Common Header(`interface_version`, `sensor_id`, ns 단위 `timestamp`, `message_counter`, `interface_cycle_time`, `data_qualifier`), `ambiguity_domain.radial_velocity_begin` / `radial_velocity_end`, `recognised_detections_capability`, `num_detections`, `detections`를 담는다. RDI payload는 36바이트 헤더와 detection당 51바이트로 구성되며 최대 8192개 detection을 담는다. 인터페이스 v1.1.0 펌웨어는 최대 2048개를 송신한다.

### Detection 필드 (`bts_rdi_detection_t`, 51바이트)

위치는 구면 좌표이다. 데카르트 좌표가 필요하면 호출자 코드에서 변환한다.

| C++ 필드 | Python 키 (`parse_rdi_detection`) | 단위 | 설명 |
|----------|-----------------------------------|------|------|
| `existence_probability` | `existence_probability` | % | 0~100 |
| `detection_id` | `detection_id` | | 미사용 시 `0xFFFF` |
| `object_id_reference` | `object_id_reference` | | 연관 객체 ID(예약) |
| `timestamp_difference` | `timestamp_difference` | s | 메시지 타임스탬프 대비 시간차 |
| `radar_cross_section` | `rcs` | dBsm | 레이더 단면적 |
| `signal_to_noise_ratio` | `snr` | dB | 신호 대 잡음비 |
| `ambiguity_grouping_id` | `ambiguity_grouping_id` | | 미사용 시 `0xFFFF` |
| `position_radial_distance` | `radial_distance` | m | 거리 |
| `position_azimuth` | `azimuth` | rad | 방위각 |
| `position_elevation` | `elevation` | rad | 고도각 |
| `position_radial_distance_error` | `radial_distance_error` | m | 거리 오차 |
| `position_azimuth_error` | `azimuth_error` | rad | 방위각 오차 |
| `position_elevation_error` | `elevation_error` | rad | 고도각 오차 |
| `relative_velocity_radial` | `radial_velocity` | m/s | 반경 속도 |
| `relative_velocity_radial_error` | `radial_velocity_error` | m/s | 반경 속도 오차 |

### `AfiShiMessage` (= `bts_shi_message_t`, 센서 상태)

`AfiShiMessage`는 Common Header 필드, `num_valid_operation_modes` / `sensor_operation_modes[]`(DR..UDR `0..6`, Degradation `10`, Evaluation `50`, Calibration `100`, Initialising `200`, Test `201`), `sensor_defect_recognised`, `sensor_defect_reason`, `supply_voltage_status`, `sensor_temperature_status`, `num_valid_input_signal_statuses` / `sensor_input_signal_types[]` / `sensor_input_signal_statuses[]`, `sensor_time_sync`, `num_valid_calibration_components` / `sensor_calibration_components[]` / `sensor_calibration_statuses[]` / `sensor_calibration_states[]`를 담는다. 메시지 길이는 가변이다.

### `AfiSpiMessage` (= `bts_spi_message_t`, 센서 성능과 자세)

| 필드 | 설명 |
|------|------|
| `vehicle_coordinate_system` | 후륜 차축(0) 또는 노면 기준(1) |
| `sensor_pose.origin_point_x/y/z` | 장착 위치(m) |
| `sensor_pose.orientation_yaw/pitch/roll` | 장착 방향(rad) |
| `sensor_pose.orientation_error_yaw/pitch` | 방향 오차(rad) |
| `num_fov_segments`, `fov_segments[]` | 방위각과 고도각 범위, `blockage_status`(`BTS_SPI_BS_NONE`, `_PARTIAL_BLOCKAGE`, `_FULL_BLOCKAGE`, `_UNKNOWN`) |
| `num_recognisable_object_types`, `recognisable_object_types[]` | 객체 분류와 감지 거리 |
| `num_reference_target_types`, `reference_target_types[]` | 기준 타겟의 RCS, 감지 거리, SNR |

Config API 회전값(`AfiRotation`, `get_rotation()`)의 단위는 도(degree)이며, SPI pose의 단위는 라디안이다.

### `AfiStreamMeta` / `AfiWirePayloadFrame`

| 필드 | `AfiStreamMeta` | `AfiWirePayloadFrame` | 설명 |
|------|:---:|:---:|------|
| `stream` | | ✓ | `AfiStreamKind::kDetection`, `kHealth`, `kPerformance` |
| `recv_timestamp_ns` | ✓ | ✓ | 호스트 수신 시각(steady clock) |
| `sensor_timestamp_ns`, `message_counter` | | ✓ | ISO Common Header의 timestamp와 MessageCounter |
| `e2e_counter`, `e2e_checked`, `e2e_valid` | ✓ | ✓ | E2E Counter와 판정 결과 |
| `payload` | | ✓ | 재조립된 그대로의 E2E 헤더와 ISO payload |
| `generation` | ✓ | ✓ | 송신 측 세대 |

### Config API 결과 타입

| 타입 | 필드 |
|------|------|
| `AfiModeStatus` | `access_mode`(`AfiAccessMode`), `access_mode_name`(`"normal"` / `"diagnostic"` / `"programming"`), `elapsed_ms`, `streams_halted` |
| `AfiSensorResult` | `return_code`(응답이 없으면 `-1`), `return_name`(예: `"SESSION_REQUIRED"`), `error_message`(펌웨어 업데이트 메서드), `e2e_ok` |
| `AfiInterfaceGeneration` | `kUnknown`(0), `kV1_0`(1), `kV1_1`(2) |
| `AfiDeviceInfo` | `serial_number`, `mac_address`, `model_name`, `hw_version`, `software_version`, `bootloader_hash`, `fw_hash` |
| `AfiSensorStatus` | `operation_state`, `operation_state_code`, `temperature_mcu`, `temperature_rf`, `voltage`, `uptime_sec` |
| `AfiNetworkConfig` | `ip_address`, `subnet_mask`, `gateway`, `api_port`, `vlan_id`, `detection`, `health`, `performance`(`AfiStreamDestination`의 `ip`, `port`, `protocol`) |
| `AfiBootInfo` | `fw_update_active`, `fw_update_valid_a`, `fw_update_valid_b`, `fw_update_boot_count`, `boot_info_status` |
| `AfiOtaStatus` | `state`(`"idle"`, `"transferring"`, `"verifying"`, `"writing"`, `"error"`), `progress_pct`, `current_component`, `error_message` |

---

## 15. 에러 코드

C++ 코드는 `afi_error.h`의 `afi::AfiError` enum이다. Python 상수는 같은 값을 가지며, 비교는 이름으로 한다.

| C++ (`afi::`) | 값 | Python | 의미 |
|---------------|----|--------|------|
| `kOk` | `0` | `OK` | 성공 |
| `kNotInitialized` | `-1` | `NOT_INITIALIZED` | `Init()` 미호출, Config API 비활성, 또는 `Stop()` 이후 |
| `kAlreadyRunning` | `-2` | (미제공) | 초기화 또는 실행 중에 `Init()`을 호출했다. 먼저 `Stop()`을 호출한다 |
| `kConnectFailed` | `-3` | `CONNECT_FAILED` | 스트림 또는 CSII 소켓 연결 실패, `ReconnectConfigApi` / `reconnect` 타임아웃, Python `init()` 연결 실패 |
| `kTimeout` | `-4` | `TIMEOUT` | 타임아웃 안에 Config API 응답이 오지 않았다. 다음 호출이 재연결한다 |
| `kDisconnected` | `-5` | `DISCONNECTED` | Config API 미연결, 연결·송신 실패, 연결 끊김, Python CSII 소켓 오류 |
| `kInvalidParam` | `-6` | `INVALID_PARAM` | SDK 또는 레이더(`INVALID_PARAM`)가 거부한 인자, 또는 16,384 B를 넘는 요청 본문 |
| `kProtocolError` | `-7` | `PROTOCOL_ERROR` | 요청과 맞지 않는 응답 또는 형식 오류 |
| `kSensorError` | `-8` | `SENSOR_ERROR` | 그 밖의 레이더 반환 코드(예: `INTERNAL_ERROR`) |
| `kInternalError` | `-9` | `INTERNAL_ERROR` | SDK 내부 오류 |
| `kNotSupported` | `-10` | `NOT_SUPPORTED` | 레이더 `NOT_SUPPORTED`, 또는 인터페이스 v1.0.x 센서(요청 미송신) |
| `kBusy` | `-11` | `BUSY` | 레이더 `BUSY`(초당 200건 초과 요청) |
| `kSessionRequired` | `-12` | `SESSION_REQUIRED` | 레이더 `SESSION_REQUIRED`(현재 접근 모드에서 열리지 않은 메서드) |
| `kAccessDenied` | `-13` | `ACCESS_DENIED` | 레이더 `ACCESS_DENIED`(모드 재진입 또는 역방향 전이). Programming 해제는 재부팅으로 한다 |
| `kInvalidState` | `-14` | `INVALID_STATE` | 레이더 `INVALID_STATE`(순서가 맞지 않는 단계, 예: idle이 아닌 업데이트 세션) |

| 레이더 반환 코드 | C++ | Python |
|------------------|-----|--------|
| `SUCCESS` | `kOk` | `OK` |
| `INVALID_PARAM` | `kInvalidParam` | `INVALID_PARAM` |
| `NOT_SUPPORTED` | `kNotSupported` | `NOT_SUPPORTED` |
| `BUSY` | `kBusy` | `BUSY` |
| `SESSION_REQUIRED` | `kSessionRequired` | `SESSION_REQUIRED` |
| `ACCESS_DENIED` | `kAccessDenied` | `ACCESS_DENIED` |
| `INVALID_STATE` | `kInvalidState` | `INVALID_STATE` |
| 그 밖의 코드 | `kSensorError` | `SENSOR_ERROR` |

- C++의 `afi::AfiErrorStr(code)`는 읽을 수 있는 메시지를 반환한다. `GetLastSensorResult()`는 레이더의 `return_code`, `return_name`, `error_message`, `e2e_ok`를 담는다. `afi::AfiErrorFromReturnCode(rc)`와 `afi::AfiReturnCodeName(rc)`는 레이더 반환 코드를 SDK 오류와 이름으로 변환한다.
- Python의 `sdk.last_error`는 마지막 Config API 호출의 코드를, `sdk.last_result`는 `{return_code, return_name, error_message, data, e2e_ok}`를 담는다. 전송 실패에서는 `return_code`가 음수이고 `return_name`은 `"TRANSPORT"`, `"TIMEOUT"`, `"PROTOCOL"`, `"TOO_LARGE"` 중 하나이다. `error_from_result(rc)`는 `ConfigClient` 결과를 SDK 코드로 변환한다.

---

## 16. 포트 & 프로토콜 레퍼런스

| 포트 | 프로토콜 | 방향 | Event ID | E2E Data ID | 용도 |
|------|----------|------|----------|-------------|------|
| 30500 | TCP | ↔ | | `0x6000 << 16 \| Method ID` | Config API(SOME/IP + JSON, 펌웨어 조각은 raw binary), Method ID `0x0010`~`0x00E1` |
| 30501 | UDP (v1.1.0), TCP (v1.0.x) | → | `0x8001` | `0x60008001` | CSII 차량 입력 |
| 30509 | TCP/UDP | ← | `0x8002` | `0x60008002` | RDI 포인트 클라우드 |
| 30510 | TCP/UDP | ← | `0x8003` | `0x60008003` | SHII 상태 정보 |
| 30511 | TCP/UDP | ← | `0x8004` | `0x60008004` | SPI 성능 정보 |
| 30520 | UDP 브로드캐스트 | ↔ | | (미적용) | Discovery |

TCP(기본값)에서는 호스트가 센서의 스트림 포트로 접속하고, UDP에서는 호스트가 스트림 포트를 bind한다. SOME/IP Service ID는 `0x6000`이다. 레이더는 100 ms 송신 슬롯마다 RDI, SHII, SPI 메시지를 하나씩 송신한다.

**프로토콜 스택 (데이터 스트림)**

```
User callback / afi_sdk.streams parser
    ↑
ISO 23150 payload (binary, little-endian)
    ↑
E2E header (20 B, big-endian: CRC high 0 | CRC-32 | Length | Counter | Data ID)
    ↑
SOME/IP header (16 B, big-endian)  [+ 4 B SOME/IP-TP header on segmented UDP RDI]
    ↑
TCP / UDP
```

| E2E 오프셋 | 크기 | 필드 | 설명 |
|------------|------|------|------|
| 0 | 4 | CRC high | 인터페이스 v1.1.0에서 `0x00000000` |
| 4 | 4 | CRC | 본문만 대상으로 하는 IEEE 802.3 CRC-32(KAT `0xCBF43926`)이며, CRC가 0이면 통과로 판정하지 않는다 |
| 8 | 4 | Length | 20바이트 헤더를 제외한 본문 길이 |
| 12 | 4 | Counter | 데이터 스트림은 송신 슬롯, Config API는 시퀀스 |
| 16 | 4 | Data ID | `0x6000 << 16`에 Event ID 또는 Method ID를 더한 값 |

**멀티 센서 호스트 수신 포트 규칙 (UDP, 센서당 +100)**

| 센서 | RDI | SHII | SPI |
|------|-----|------|-----|
| 1 | 30509 | 30510 | 30511 |
| 2 | 30609 | 30610 | 30611 |
| 3 | 30709 | 30710 | 30711 |
| 4 | 30809 | 30810 | 30811 |

TCP에서는 각 센서가 기본 포트를 그대로 사용하므로 오프셋이 필요하지 않다.

---

## 17. 문제 해결 체크리스트

| 증상 | 확인 사항 |
|------|-----------|
| `ping` 실패 | 호스트 IP의 `192.168.10.x` 대역 여부, 케이블, NIC, 방화벽을 확인한다 |
| `Init()`이 `kConnectFailed` 반환 | TCP 스트림 연결 또는 UDP 포트 bind가 실패했다. `sensor_ip`, 케이블, 서브넷, 같은 포트를 쓰는 다른 프로세스를 확인한다 |
| `Init()`이 `kInvalidParam` 반환 | `"tcp"`, `"udp"` 이외의 전송 문자열을 지정했다(`csii_transport`는 빈 값도 허용한다) |
| `Start()`는 성공하지만 callback이 호출되지 않는다 | `GetNetworkConfig`의 목적지 IP, 포트, 프로토콜이 `AfiSdkConfig`와 일치해야 한다. UDP에서는 `sensor_ip` 이외의 주소에서 온 datagram을 버린다. unicast 목적지 IP는 TCP 스트림 클라이언트도 그 IP로 제한한다 |
| callback은 호출되나 `num_detections == 0` | 시야 내 물체 존재 여부와 범위 모드(`GetRangeMode`)를 확인한다. DataQualifier `1`은 사용하지 않아야 하는 내용을 뜻한다 |
| SET이 `kSessionRequired` 반환 | SET 전에 `EnterMode(kDiagnostic)`를 호출한다 |
| `SetMode` / `EnterMode`가 `kAccessDenied` 반환 | 재진입, 역방향 전이, 또는 Programming 상태이다. 레이더를 재시작한다 |
| SET은 성공하지만 동작이 바뀌지 않는다 | 설정은 `Restart()` 이후 반영된다. 이어서 `ReconnectConfigApi()`를 호출한다 |
| 데이터 스트림 정지 | 프로그래밍 모드는 재부팅 전까지 RDI, SHII, SPI를 정지한다(`GetMode().streams_halted`) |
| 모든 Config API 호출이 `kNotInitialized` 반환 | `enable_config_api = false`, `Init()` 이전, 또는 `Stop()` 이후 상태이다 |
| 모든 Config API 호출이 `kNotSupported` 반환 | 인터페이스 v1.0.x 센서이다(`GetInterfaceGeneration() == kV1_0`). 데이터 스트림과 CSII는 계속 동작한다 |
| Config API 호출의 `kTimeout` / `kDisconnected` | 레이더는 Config API 클라이언트를 하나만 받으므로 다른 클라이언트를 닫는다. 다음 호출이 재연결한다 |
| `kBusy` | 초당 200건을 넘는 요청이다 |
| Config API를 끈 구성에서 v1.0.x 센서가 CSII를 받지 못한다 | 세대를 판정하지 못해 CSII가 UDP로 송신되었다. `csii_transport = "tcp"`를 지정한다 |
| 로그에 E2E 경고 | 프레임별 `e2e_valid`를 확인한다. `e2e_strict = true`는 실패 프레임을 폐기한다 |
| RDI loss callback이 손실 보고 | UDP에서는 `socket_buffer_size`를 늘리거나 TCP를 사용한다. 공백에는 이 호스트로 송신되지 않은 슬롯(첫 scan 이전, TCP 적체)도 포함된다 |
| Discovery가 센서를 찾지 못한다 | 동일 서브넷 여부, `bind_ip` 지정, UDP 30520 방화벽 차단 여부를 확인하고 다시 탐색한다 |
| 레이더 없이 수신 경로 시험 | `python tools/rdi_udp_sender.py 127.0.0.1 30509 50 10`을 실행하고 `sensor_ip = "127.0.0.1"`, `stream_transport = "udp"`, `enable_config_api = false`로 수신한다 |

---

## 빠른 참조 카드

```
INIT      → sdk.Init(cfg)                          one AfiSdk per sensor
STREAM    → sdk.RegRecvCallback(cb)                RDI / SHII / SPI (+ AfiStreamMeta) / loss
          → sdk.RegWirePayloadCallback(cb)         E2E header + ISO payload before parsing
RUN       → sdk.Start()   |   STOP → sdk.Stop()
CONFIG    → GetX(out) / SetX(in)                   int: kOk or negative AfiError
MODE      → sdk.EnterMode(afi::AfiAccessMode::kDiagnostic)      before any SET
APPLY     → sdk.Restart(); sdk.ReconnectConfigApi();             no save call
FIRMWARE  → EnterMode(kProgramming) → Start/Chunk/Commit/idle x4 → SwitchSlot → Reboot
VEHICLE   → sdk.SendVehicleInput(vi)               after Init, every 100 ms
DISCOVER  → afi::AfiSdk::Discover()                static, no Init needed
DIAGNOSE  → sdk.GetLastSensorResult(), sdk.GetInterfaceGeneration()
PYTHON    → afi_sdk.AfiSdk (Config API, CSII, discover), afi_sdk.streams (parsers),
            afi_sdk.firmware.update() (complete firmware update)
```

**다음으로 읽을 파일**
- [examples/cpp/sample_pointcloud.cpp](examples/cpp/sample_pointcloud.cpp)
- [examples/cpp/sample_data_stream.cpp](examples/cpp/sample_data_stream.cpp)
- [examples/cpp/sample_config.cpp](examples/cpp/sample_config.cpp)
- [python/examples/sample_data_stream.py](python/examples/sample_data_stream.py)
- [python/examples/sample_multi_sensor.py](python/examples/sample_multi_sensor.py)
- [docs/api_reference.md](docs/api_reference.md) (전체 API 레퍼런스)
