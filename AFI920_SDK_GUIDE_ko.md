# AFI920 SDK — AI 통합 가이드

> **대상:** AI 에이전트, LLM 기반 자동화, bitsensing AFI920 4D 이미징 레이더에서 가장 빠르게 데이터를 수신하려는 개발자  
> **SDK 버전:** 2.1.0 · **프로토콜:** AFI920 v3.0 · **라이선스:** BSD-3-Clause

---

## 목차

1. [SDK 개요](#1-sdk-개요)
2. [레포지토리 구조](#2-레포지토리-구조)
3. [네트워크 사전 조건](#3-네트워크-사전-조건)
4. [빌드 & 설치](#4-빌드--설치)
5. [빠른 시작: 포인트 클라우드 수신](#5-빠른-시작-포인트-클라우드-수신)
6. [세 가지 데이터 스트림 모두 사용](#6-세-가지-데이터-스트림-모두-사용)
7. [Configuration API](#7-configuration-api)
8. [차량 입력 (CSII)](#8-차량-입력-csii)
9. [센서 탐색 (Discovery)](#9-센서-탐색-discovery)
10. [멀티 센서 설정](#10-멀티-센서-설정)
11. [주요 데이터 구조](#11-주요-데이터-구조)
12. [에러 코드](#12-에러-코드)
13. [포트 & 프로토콜 레퍼런스](#13-포트--프로토콜-레퍼런스)
14. [문제 해결 체크리스트](#14-문제-해결-체크리스트)

---

## 1. SDK 개요

AFI920 SDK는 호스트 컴퓨터와 하나 이상의 bitsensing AFI920 레이더가 이더넷으로 통신할 수 있게 해줍니다.

| 방향 | 채널 | 전달 내용 |
|------|------|---------|
| 레이더 → 호스트 | RDI (TCP/UDP 30509) | 포인트 클라우드 감지 (거리, 방위각, 고도각, 속도, RCS, SNR…) |
| 레이더 → 호스트 | SHII (TCP/UDP 30510) | 센서 상태 정보 |
| 레이더 → 호스트 | SPI (TCP/UDP 30511) | 센서 성능 & 장착 포즈 |
| 호스트 → 레이더 | Config API (TCP 30500) | 설정 읽기/쓰기 약 30종 (7개 카테고리) |
| 호스트 → 레이더 | CSII (TCP 30501) | 차량 동역학 입력 |
| 호스트 ↔ 레이더 | Discovery (UDP 30520) | LAN에서 센서 탐색 |

모든 데이터 스트림은 **SOME/IP 프레이밍 + AUTOSAR E2E Profile 7 (CRC-64/XZ)** 를 사용합니다. SDK가 이를 투명하게 처리합니다.

---

## 2. 레포지토리 구조

```
afi920-sdk/
├── include/afi/
│   ├── afi_sdk.h        ← 메인 C++ API (AfiSdk 클래스)
│   ├── afi_types.h      ← 모든 데이터 구조체
│   ├── afi_config.h     ← AfiSdkConfig 구조체
│   └── afi_error.h      ← 에러 코드
├── src/                 ← C++ 구현 (빌드 산출물: libafi_sdk.a)
├── python/src/afi_sdk/  ← Python 패키지
│   ├── _sdk.py          ← AfiSdk Python 클래스 (Config API)
│   ├── someip.py        ← SOME/IP 프레이밍 + Config 클라이언트 + 탐색
│   └── streams.py       ← 저수준 E2E / ISO 23150 스트림 파서
├── examples/cpp/        ← 바로 실행 가능한 C++ 예제 7개
├── python/examples/     ← 바로 실행 가능한 Python 예제 6개
└── docs/api_reference.md← 완전한 API 레퍼런스
```

> ROS 2 드라이버는 이 SDK 트리 안이 아니라 **별도 레포지토리**(`afi920-ros2-driver`)에 있습니다.

---

## 3. 네트워크 사전 조건

| 항목 | 기본값 |
|------|-------|
| 센서 IP | `192.168.10.150` |
| 호스트 IP | `192.168.10.100` |
| 서브넷 | `255.255.255.0` |

**코드 실행 전 필수 확인 사항:**
1. 호스트와 레이더를 이더넷으로 연결합니다.
2. 호스트에 `192.168.10.x` 대역의 정적 IP를 할당합니다.
3. 연결 확인: `ping 192.168.10.150`

---

## 4. 빌드 & 설치

### C++ (CMake 3.16+, C++17)

```bash
cd afi920-sdk
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_EXAMPLES=ON        # 선택: examples/ 빌드
cmake --build . -j$(nproc)
# 산출물: build/src/libafi_sdk.a
```

**자체 CMakeLists.txt에 링크:**
```cmake
add_subdirectory(afi920-sdk)
target_link_libraries(my_app PRIVATE afi_sdk)
```

### Python (3.8+, 외부 의존성 없음)

```bash
cd afi920-sdk/python
pip install .
# 개발 모드:
pip install -e .
```

---

## 5. 빠른 시작: 포인트 클라우드 수신

### C++

```cpp
#include <afi/afi_sdk.h>
#include <cstdio>
#include <thread>
#include <chrono>

int main() {
    afi::AfiSdkConfig cfg;
    cfg.sensor_ip = "192.168.10.150";   // 기본 센서 IP
    cfg.enable_rdi = true;
    cfg.enable_config_api = false;      // 데이터 전용 모드

    afi::AfiSdk sdk;

    // 콜백 등록 — RDI 프레임마다 호출됨 (~10 Hz).
    // 위치는 데카르트가 아니라 구면 좌표(거리/방위각/고도각)입니다.
    sdk.RegRecvCallback([](const afi::AfiRdiFrame& frame) {
        const auto& m = frame.message;
        printf("프레임 %u  감지 수: %u\n", frame.frame_seq, m.num_detections);
        for (uint16_t i = 0; i < m.num_detections; ++i) {
            const auto& d = m.detections[i];
            printf("  [%u] r=%.2f m  az=%.3f rad  el=%.3f rad  vel=%.2f m/s  rcs=%.1f dBsm\n",
                   i,
                   d.position_radial_distance,
                   d.position_azimuth,
                   d.position_elevation,
                   d.relative_velocity_radial,
                   d.radar_cross_section);
        }
    });

    if (sdk.Init(cfg) != afi::kOk) return 1;
    sdk.Start();

    std::this_thread::sleep_for(std::chrono::seconds(10));
    sdk.Stop();
}
```

### Python

> Python `AfiSdk` 클래스는 **Config API만** 제공합니다. 데이터 스트림은 일반
> 소켓으로 수신하고 `afi_sdk.streams` / `afi_sdk.someip` 파서로 디코딩합니다.
> (TCP/UDP 전체 버전과 SOME/IP-TP 재조립은 `python/examples/sample_pointcloud.py` 참고)

```python
import socket, struct
from afi_sdk.someip import SOMEIP_HEADER_SIZE, DEFAULT_RDI_PORT
from afi_sdk.streams import strip_e2e, parse_rdi_frame, parse_rdi_detection

def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf += chunk
    return buf

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("192.168.10.150", DEFAULT_RDI_PORT))   # TCP RDI 스트림

while True:
    hdr = recv_exact(sock, 8)                          # SOME/IP MessageID(4)+Length(4)
    if len(hdr) < 8:
        break
    length = struct.unpack(">I", hdr[4:8])[0]
    data = hdr + recv_exact(sock, length)
    _, iso = strip_e2e(data[SOMEIP_HEADER_SIZE:])      # 20B E2E 헤더 제거
    frame = parse_rdi_frame(iso)
    if not frame:
        continue
    print(f"감지 수={frame['num_detections']}  counter={frame['message_counter']}")
    if frame["num_detections"] > 0:
        d = parse_rdi_detection(iso, frame["det_start"], 0)
        print(f"  r={d['radial_distance']:.2f} m  az={d['azimuth']:.3f} rad  "
              f"el={d['elevation']:.3f} rad  v={d['radial_velocity']:.2f} m/s  "
              f"rcs={d['rcs']:.1f} dBsm")
```

---

## 6. 세 가지 데이터 스트림 모두 사용

### 모든 콜백 등록 (C++)

`RegRecvCallback`은 **오버로드**되어 있습니다 — SDK가 콜백 인자 타입으로 스트림을
구분합니다. 먼저 config에서 원하는 스트림을 활성화하세요(`enable_rdi` /
`enable_shi` / `enable_spi`).

```cpp
// RDI — 포인트 클라우드 (프레임당 최대 4096개 감지)
sdk.RegRecvCallback([](const afi::AfiRdiFrame& f) {
    /* f.message.detections[0 .. f.message.num_detections-1] 처리 */
});

// SHII — 센서 상태
sdk.RegRecvCallback([](const afi::AfiShiMessage& msg) {
    /* msg.sensor_defect_recognised, msg.supply_voltage_status, … */
});

// SPI — 센서 성능 & 포즈
sdk.RegRecvCallback([](const afi::AfiSpiMessage& msg) {
    /* msg.sensor_pose.origin_point_x, msg.sensor_pose.orientation_yaw,
       msg.num_fov_segments, msg.fov_segments[0].blockage_status, … */
});

// 선택: 패킷 손실 알림 — (total, lost)
sdk.RegRecvCallback([](uint32_t total, uint32_t lost) {
    std::fprintf(stderr, "%u / %u 패킷 손실\n", lost, total);
});
```

> **⚠️ 콜백 스레드 & 데이터 수명**
> - 콜백은 `Start()`를 호출한 스레드가 아니라 **수신 스레드**(스트림당 하나)에서
>   호출됩니다. 공유 상태에 접근한다면 동기화하세요.
> - 감지 배열(`frame.message.detections`)은 **다음 프레임에서 재사용되는 내부
>   버퍼**를 가리킵니다. **콜백 실행 중에만** 유효하므로, 콜백 범위 밖에서
>   보관하려면 반드시 복사하세요.
> - Config API 메서드와 `SendVehicleInput()`은 동기 방식이며 호출자 스레드에서
>   실행됩니다. 동일한 `AfiSdk` 인스턴스에서 Config API 메서드를 동시에 호출하지
>   마세요.

### 모든 스트림 수신 (Python)

Python `AfiSdk` 클래스에는 **스트리밍 콜백이 없습니다**. 각 스트림(RDI 30509,
SHII 30510, SPI 30511)을 각자의 소켓으로 수신하고 `afi_sdk.streams`로 파싱합니다
— 보통 스트림당 백그라운드 스레드 하나를 둡니다. 이 패턴을 TCP/UDP(SOME/IP-TP
재조립 포함) 모두에 대해 `StreamReceiver` 클래스로 감싼 예제가
`python/examples/sample_data_stream.py`에 있습니다.

---

## 7. Configuration API

모든 설정 호출은 동기(블로킹) 방식입니다. Config API는 자체 TCP 연결(포트 30500)로
동작하며 데이터 스트림과 독립적입니다 — `enable_config_api = true`(기본값)를 유지하세요.
모든 C++ getter는 **`int` 에러 코드를 반환하고 결과는 출력 인자에 채워 줍니다.**

> 아래 예제는 대표적인 일부입니다. 전체 Config API는 **7개 카테고리에 걸친 약 30개
> 메서드**(Device/Status, Network, Range Mode, Detection, Time Sync, Mounting,
> Persistence)이며, 전체 목록은 이 섹션 끝의 카탈로그를, 시그니처는
> [docs/api_reference.md](docs/api_reference.md)를 참고하세요.

### 읽기 (C++)

```cpp
afi::AfiSdk sdk;
sdk.Init(cfg);

afi::AfiDeviceInfo info;
if (sdk.GetDeviceInfo(info) == afi::kOk)        // 시리얼, SW 버전, …
    printf("Serial: %s\n", info.serial_number.c_str());

afi::AfiSensorStatus status;
sdk.GetSensorStatus(status);                    // 동작 상태, 온도, 전압

afi::AfiNetworkConfig net;
sdk.GetNetworkConfig(net);                       // 레이더 IP + 스트림 목적지

afi::AfiRangeMode mode;
sdk.GetRangeMode(mode);                          // DR / LR / MR / SR / ULR / USR

afi::AfiDetectionThreshold thresh;
sdk.GetDetectionThreshold(thresh);

afi::AfiPtpConfig ptp;
sdk.GetPtpConfig(ptp);
```

### 쓰기 (C++)

```cpp
sdk.SetRangeMode(afi::AfiRangeMode::kLongRange);

// 한 스트림의 목적지를 이 호스트로 지정 (즉시 적용, 재시작 불필요)
afi::AfiStreamDestination dst;
dst.ip       = "192.168.10.100";
dst.port     = 30509;
dst.protocol = "udp";                            // "tcp" 또는 "udp"
sdk.SetStreamDestination(afi::AfiStreamKind::kDetection, dst);

sdk.SaveConfig();           // NVM에 저장
```

### 읽기 / 쓰기 (Python)

```python
from afi_sdk import AfiSdk, AfiSdkConfig, RangeMode

info   = sdk.get_device_info()          # dict 반환 (실패 시 None)
status = sdk.get_sensor_status()
mode   = sdk.get_range_mode()           # int 반환 (RangeMode 참고)

sdk.set_range_mode(RangeMode.LONG_RANGE)   # int 상수를 받음
sdk.save_config()
```

### 범위 모드 값

| 상수 | 의미 |
|------|------|
| `DR` | 듀얼 범위 (Dual Range) |
| `LR` | 장거리 (Long Range) |
| `MR` | 중거리 (Mid Range) |
| `SR` | 단거리 (Short Range) |
| `ULR` | 초장거리 (Ultra Long Range) |
| `USR` | 초단거리 (Ultra Short Range) |

### 감지 튜닝 (C++)

```cpp
// SNR 임계값 + 19단계 CFAR 테이블
afi::AfiDetectionThreshold thresh;
sdk.GetDetectionThreshold(thresh);
thresh.snr_dB = 14.0f;                          // CFAR 레벨은 1..9
sdk.SetDetectionThreshold(thresh);

// 9가지 클러터/다중경로 필터 토글
afi::AfiDetectionFilters filters;
sdk.GetDetectionFilters(filters);
filters.filter_fov = true;
filters.filter_multibounce_2x = true;
sdk.SetDetectionFilters(filters);

// 거리/각도 측정 밀도 증강
afi::AfiDetectionDensity density{ /*range*/ true, /*angle*/ true };
sdk.SetDetectionDensity(density);
```

```python
# Python
sdk.set_detection_threshold(snr_db=14.0)
sdk.set_detection_filters(filter_fov=True, filter_multibounce_2x=True)  # 일부만 지정 가능
sdk.set_detection_density(range_density=True, angle_density=True)
```

### 전체 Config API 메서드 카탈로그

모든 getter는 `int` + 출력 인자(C++) / `Optional[dict]`(Python)를, setter는 `int`을
반환합니다. 시그니처와 필드 표는 [docs/api_reference.md](docs/api_reference.md)를 참고하세요.

| 카테고리 (Method ID) | 메서드 |
|---------------------|--------|
| Device & Status (0x001x) | `GetDeviceInfo`, `GetSensorStatus`, `GetDiagnosticInfo`, `ClearFaultLog`, `Restart`, `ResetAllSettings`, `GetCommandHistoryInfo` |
| Network (0x002x) | `GetNetworkConfig`, `SetNetworkConfig` (`restart_required` 보고), `SetStreamDestination` |
| Range Mode (0x003x) | `GetRangeMode`, `SetRangeMode` |
| Detection (0x004x) | `GetDetectionThreshold`, `SetDetectionThreshold`, `GetDetectionFilters`, `SetDetectionFilters`, `GetDetectionDensity`, `SetDetectionDensity` |
| Time Sync (0x005x) | `GetPtpConfig`, `SetPtpConfig` (profile 0만 지원) |
| Mounting (0x007x) | `GetMountingPosition`, `SetMountingPosition`, `GetRotation`, `SetRotation`, `GetSensorPosition`, `SetSensorPosition` |
| Persistence (0x009x) | `SaveConfig`, `GetConfigStatus`, `ResetConfigDefaults` |

> `Restart`, `ResetAllSettings`, `SetNetworkConfig`(레이더 네트워크 필드)는 연결을
> 끊거나 센서 재시작이 필요합니다. `SaveConfig`는 변경 사항을 NVM에 영구 저장합니다
> — 그렇지 않으면 Set* 변경은 재부팅 시 사라집니다.

---

## 8. 차량 입력 (CSII)

자아 운동 보정을 위해 차량 동역학을 레이더에 주기적으로 전송합니다 —
**100 ms (10 Hz) 권장**. SDK가 최초 호출 시 지연 연결하고 메시지/E2E 카운터를
자동으로 관리합니다.

```cpp
// C++
afi::AfiVehicleInput vi{};
vi.velocity_x_mps = 10.0f;          // m/s 전방
vi.velocity_y_mps = 0.0f;
vi.velocity_z_mps = 0.0f;
vi.rotation_rate_yaw_rps = 0.05f;   // rad/s
sdk.SendVehicleInput(vi);
```

```python
# Python
sdk.send_vehicle_input(
    velocity_mps=(10.0, 0.0, 0.0),
    rotation_rate_rps=(0.0, 0.0, 0.05)
)
```

---

## 9. 센서 탐색 (Discovery)

로컬 네트워크의 모든 AFI920 센서를 탐색합니다 (UDP 브로드캐스트, 기본 3초 타임아웃).

```cpp
// C++ — 모든 인터페이스에서 탐색
auto sensors = afi::AfiSdk::Discover();
for (const auto& s : sensors) {
    std::cout << s.serial_number << "  " << s.ip
              << "  sw=" << s.software_version << "\n";
}
```

```python
# Python — dict 리스트 반환
sensors = AfiSdk.discover(timeout_s=3.0, bind_ip="")
for s in sensors:
    print(s.get("serial_number"), s.get("ip_address"), s.get("software_version"))
```

주요 `AfiSensorInfo` 필드: `serial_number`, `ip`, `model`, `hw_version`, `software_version`, `range_mode`, `detection_port`, `health_port`, `performance_port`, `ptp_state`, `ptp_offset_ns`. (Python 탐색 dict도 동일한 키를 쓰며, 레이더 IP는 `ip_address` 키에 있습니다.)

---

## 10. 멀티 센서 설정

센서마다 독립적인 `AfiSdk` 인스턴스를 하나씩 사용합니다(각자 소켓·스레드·콜백을
가짐). **TCP**에서는 호스트 측이 임시 포트를 쓰므로 포트 오프셋이 필요 없고
인스턴스마다 `sensor_ip`만 다르게 주면 됩니다. **UDP**에서는 센서별로 호스트 수신
포트를 다르게 지정해야 하며, SDK 관례는 `센서당 +100`입니다(`sample_multi_sensor.py` 참고).

```cpp
// C++ — 센서당 인스턴스 하나 (TCP, 기본 포트)
const char* ips[] = {"192.168.10.150", "192.168.10.151"};

std::vector<std::unique_ptr<afi::AfiSdk>> sdks;
for (const char* ip : ips) {
    afi::AfiSdkConfig cfg;
    cfg.sensor_ip = ip;
    cfg.enable_rdi = true;
    cfg.enable_config_api = false;

    auto sdk = std::make_unique<afi::AfiSdk>();
    sdk->RegRecvCallback([ip](const afi::AfiRdiFrame& f) {
        printf("[%s] 감지 수=%u\n", ip, f.message.num_detections);
    });
    sdk->Init(cfg);
    sdk->Start();
    sdks.push_back(std::move(sdk));
}
```

```python
# Python — 센서별 UDP 수신 포트를 다르게(+100) 지정하고,
# Config API로 각 센서의 스트림 목적지를 그 포트로 설정
PORT_OFFSET_PER_SENSOR = 100
for idx, ip in enumerate(["192.168.10.150", "192.168.10.151"]):
    sdk = AfiSdk()
    sdk.init(AfiSdkConfig(sensor_ip=ip))
    offset = idx * PORT_OFFSET_PER_SENSOR
    sdk.set_network_config(
        detection_ip=host_ip, detection_port=30509 + offset,
        health_ip=host_ip,    health_port=30510 + offset,
        performance_ip=host_ip, performance_port=30511 + offset,
    )
    sdk.close()
```

**지원:** 최대 4개 센서 동시 운용

---

## 11. 주요 데이터 구조

### `AfiSdkConfig` (C++) / `AfiSdkConfig` (Python dict/dataclass)

| 필드 | 기본값 | 설명 |
|-----|-------|------|
| `sensor_ip` | `"192.168.10.150"` | 레이더 IP |
| `config_port` | `30500` | Config API TCP 포트 |
| `rdi_port` | `30509` | 호스트의 RDI 수신 포트 |
| `shi_port` | `30510` | SHII 수신 포트 |
| `spi_port` | `30511` | SPI 수신 포트 |
| `csii_port` | `30501` | 차량 입력 TCP 포트 |
| `stream_transport` | `"tcp"` | `"tcp"` 또는 `"udp"` (문자열) |
| `enable_rdi` | `true` | RDI 데이터 스트림 수신 |
| `enable_shi` | `false` | SHII 데이터 스트림 수신 |
| `enable_spi` | `false` | SPI 데이터 스트림 수신 |
| `enable_config_api` | `true` | `Init()` 시 Config API(TCP) 자동 연결 |
| `validate_e2e` | `true` | 수신 프레임 CRC 검증 |
| `e2e_strict` | `false` | CRC 실패 시 프레임 폐기 (아니면 경고 후 전달) |
| `socket_buffer_size` | `16 MiB` | TCP/UDP `SO_RCVBUF` |
| `connect_timeout_ms` | `5000` | TCP 연결 타임아웃 |
| `request_timeout_ms` | `5000` | Config API 요청 타임아웃 |

> Python `AfiSdkConfig` 데이터클래스는 위와 거의 동일하지만 두 타임아웃을 하나의
> `timeout` 필드(기본 `5.0`초)로 합쳤고, `enable_*`, `e2e_strict`,
> `socket_buffer_size`는 없습니다(Python 스트리밍은 직접 만든 소켓으로 처리하므로).
> `validate_e2e`는 유지됩니다.

### `AfiRdiFrame`

| 필드 | 타입 | 설명 |
|-----|------|------|
| `message` | `bts_rdi_message_t` | 역직렬화된 RDI 메시지 (아래 참조) |
| `recv_timestamp_ns` | uint64 | 호스트 수신 타임스탬프 (ns, 에포크 기준) |
| `frame_seq` | uint32 | SDK 내부 프레임 시퀀스 번호 |

감지 개수와 배열은 **`message` 안에** 있습니다:
`frame.message.num_detections`(uint16, 0–4096), `frame.message.message_counter`,
`frame.message.timestamp`(ns), `frame.message.detections[i]`.

### 감지 필드 (bts_rdi_detection_t, 51바이트)

위치는 **구면 좌표**(거리/방위각/고도각)입니다 — 필요하면 직접 데카르트로 변환하세요.

| 필드 | 단위 | 설명 |
|-----|------|------|
| `existence_probability` | % | 0–100 |
| `detection_id` | uint16 | 미사용 시 `0xFFFF` |
| `object_id_reference` | uint16 | 연관 객체 ID (예약) |
| `timestamp_difference` | s | |
| `radar_cross_section` | dBsm | 레이더 단면적 |
| `signal_to_noise_ratio` | dB | 신호 대 잡음비 |
| `ambiguity_grouping_id` | uint16 | 미사용 시 `0xFFFF` |
| `position_radial_distance` | m | 방사 거리 |
| `position_azimuth` | rad | 방위각 |
| `position_elevation` | rad | 고도각 |
| `position_radial_distance_error` | m | |
| `position_azimuth_error` | rad | |
| `position_elevation_error` | rad | |
| `relative_velocity_radial` | m/s | 방사 속도 |
| `relative_velocity_radial_error` | m/s | |

### `AfiSpiMessage` (= `bts_spi_message_t`, 센서 성능 & 포즈)

| 필드 | 설명 |
|-----|------|
| `sensor_pose.origin_point_x/y/z` | 장착 위치 (m) |
| `sensor_pose.orientation_yaw/pitch/roll` | 장착 방향 (rad) |
| `num_fov_segments`, `fov_segments[]` | 시야각 구역; 각 구역은 `blockage_status`(`NONE`/`PARTIAL`/`FULL`/`UNKNOWN`) 보유 |
| `num_recognisable_object_types`, `recognisable_object_types[]` | 객체 분류 + 감지 거리 |
| `num_reference_target_types`, `reference_target_types[]` | 기준 타겟의 RCS / SNR / 거리 |

---

## 12. 에러 코드

C++ 코드는 `afi::AfiError` enum(`afi_error.h`)입니다. Python 상수는 아래 이름은
공유하지만 **숫자 값은 다르므로** 항상 이름으로 비교하세요.

| C++ (`afi::`) | C++ 값 | Python 상수 | Python 값 | 의미 |
|---------------|--------|-------------|-----------|------|
| `kOk` | `0` | `OK` | `0` | 성공 |
| `kNotInitialized` | `-1` | — | — | `Init()` 미호출 |
| `kAlreadyRunning` | `-2` | — | — | 이미 시작됨 |
| `kConnectFailed` | `-3` | — | — | TCP 연결 실패 |
| `kTimeout` | `-4` | `TIMEOUT` | `-2` | 타임아웃 내 응답 없음 |
| `kDisconnected` | `-5` | `DISCONNECTED` | `-1` | TCP 연결 끊김 |
| `kInvalidParam` | `-6` | — | — | 잘못된 인자 |
| `kProtocolError` | `-7` | `PROTOCOL_ERROR` | `-3` | SOME/IP 오류 또는 E2E 실패 |
| `kSensorError` | `-8` | `SENSOR_ERROR` | `-8` | 센서 측 장애 |
| `kInternalError` | `-9` | — | — | 내부 SDK 오류 |
| `kNotSupported` | `-10` | `NOT_SUPPORTED` | `-9` | 현재 펌웨어에서 미지원 |

C++에서는 `afi::AfiErrorStr(code)`로 사람이 읽을 수 있는 메시지를 얻습니다.

---

## 13. 포트 & 프로토콜 레퍼런스

| 포트 | 프로토콜 | 방향 | 용도 |
|-----|---------|------|------|
| 30500 | TCP | ↔ | Config API (SOME/IP + JSON) |
| 30501 | TCP | → | CSII — 차량 입력 |
| 30509 | TCP/UDP | ← | RDI — 포인트 클라우드 |
| 30510 | TCP/UDP | ← | SHII — 상태 정보 |
| 30511 | TCP/UDP | ← | SPI — 성능 정보 |
| 30520 | UDP 브로드캐스트 | ↔ | Discovery |

**프로토콜 스택 (모든 데이터 스트림):**
```
사용자 콜백
    ↑
ISO 23150 페이로드 (바이너리, 리틀 엔디안)
    ↑
AUTOSAR E2E Profile 7 헤더 (20B · CRC-64/XZ)
    ↑
SOME/IP 헤더 (16B · 빅 엔디안)  [세그먼트된 UDP에서는 + 4B TP 오프셋 헤더]
    ↑
TCP / UDP
```

**멀티 센서 호스트 수신 포트 규칙 (UDP, 센서당 +100):**

| 센서 | RDI | SHII | SPI |
|-----|-----|------|-----|
| 1 | 30509 | 30510 | 30511 |
| 2 | 30609 | 30610 | 30611 |
| 3 | 30709 | 30710 | 30711 |
| 4 | 30809 | 30810 | 30811 |

(TCP에서는 센서마다 임시 호스트 포트를 쓰므로 오프셋이 필요 없습니다.)

---

## 14. 문제 해결 체크리스트

| 증상 | 확인 사항 |
|-----|---------|
| `ping` 실패 | 호스트 IP가 `192.168.10.x` 대역인가? 방화벽 개방? |
| `Init()` 타임아웃 | 센서 전원 켜짐? `config_port` 30500 접근 가능? |
| `Start()` 후 콜백 없음 | `sensor_ip` 올바름? 호스트의 RDI 포트 30509 개방? |
| 콜백 호출되나 `num_detections == 0` | 레이더 시야 내 물체 있음? `GetRangeMode()`로 범위 모드 확인 |
| 패킷 손실 콜백 발생 | TCP로 전환 (`stream_transport = "tcp"`) |
| 로그에 E2E 오류 | `validate_e2e=true` 활성화; 프레임 유지하려면 `e2e_strict` 비활성화 |
| Config API 타임아웃 | `enable_config_api=true` 유지; 포트 30500 접근 가능 여부 확인 |
| Discovery 결과 없음 | 동일 서브넷에서 실행 중? UDP 30520 방화벽 차단 여부 확인 |

---

## 빠른 참조 카드

```
초기화  → AfiSdk::Init(cfg)
설정    → GetX(out) / SetX() — int 반환, 결과는 출력 인자에
스트림  → RegRecvCallback(cb) — RDI / SHII / SPI / 손실용으로 오버로드
실행    → sdk.Start()   |   종료 → sdk.Stop()
차량 입력 → sdk.SendVehicleInput(vi)   (Start() 이후 언제든)
센서 탐색 → AfiSdk::Discover()         (정적 메서드, Init 불필요)
```

**다음으로 읽어볼 파일:**
- [examples/cpp/sample_pointcloud.cpp](afi920-sdk/examples/cpp/sample_pointcloud.cpp)
- [examples/cpp/sample_data_stream.cpp](afi920-sdk/examples/cpp/sample_data_stream.cpp)
- [python/examples/sample_data_stream.py](afi920-sdk/python/examples/sample_data_stream.py)
- [docs/api_reference.md](docs/api_reference.md) — 완전한 API 레퍼런스
