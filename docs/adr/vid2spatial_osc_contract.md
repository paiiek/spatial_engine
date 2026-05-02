# ADR: vid2spatial_v2 → spatial_engine ADM-OSC 変換契約

**Version**: v1.0  
**Date**: 2026-05-02  
**Status**: Accepted

---

## 概要 (Overview)

vid2spatial_v2 파이프라인에서 공간 엔진으로 전달되는 ADM-OSC 메시지의 포트, 주소 체계, 좌표 변환, 레이트 제한, 스무딩 정책을 정의한다.

---

## 포트 배정 (Port Assignment)

| 방향 | 포트 | 설명 |
|------|------|------|
| vid2spatial_v2 → spatial_engine (수신) | **9000** | 파이프라인 출력 수신 |
| spatial_engine → 렌더러/UI (송신) | **9100** | ADM-OSC 명령 송신 |

---

## OSC 주소 접두사 변환 (Address Prefix Mapping)

vid2spatial_v2 내부 주소 `/vid2spatial/obj/{N}/...` 는 spatial_engine 수신 시 ADM-OSC 표준 주소로 변환된다.

```
/vid2spatial/obj/{N}/azim  →  /adm/obj/{N}/azim
/vid2spatial/obj/{N}/elev  →  /adm/obj/{N}/elev
/vid2spatial/obj/{N}/dist  →  /adm/obj/{N}/dist
/vid2spatial/obj/{N}/gain  →  /adm/obj/{N}/gain
```

---

## 좌표 변환 (Coordinate Conversion)

### 방위각 (Azimuth)

vid2spatial_v2 파이프라인은 **RIGHT-handed** 좌표계(시계 방향 양수)를 사용한다.  
ADM-OSC 표준은 **LEFT-handed** (반시계 방향 양수).

```
az_adm = -az_pipeline
```

범위: `az_pipeline ∈ [-180, +180]°` → `az_adm ∈ [-180, +180]°`

### 거리 (Distance)

vid2spatial_v2의 `dist_v2s`는 `0.0` (far) ~ `1.0` (near) 의미.  
ADM-OSC `dist`는 `0.0` (near) ~ `1.0` (far) 의미.

```
dist_adm = 1.0 - dist_v2s
```

### 앙각 (Elevation)

부호 변환 없음. 두 시스템 모두 위쪽이 양수.

```
elev_adm = elev_pipeline
```

---

## 오브젝트 번호 매핑 (Object Number Mapping)

vid2spatial_v2의 트랙 ID를 ADM-OSC 오브젝트 번호로 직접 매핑한다.

- 트랙 ID `T` → ADM 오브젝트 번호 `N = T`
- 유효 범위: `N ∈ [1, 64]`
- 범위 초과 트랙은 무시하고 경고 로그를 남긴다.
- 오브젝트 0은 예약됨 (마스터 버스 용도), 매핑에서 제외.

---

## 레이트 제한 (Rate Limiting)

- **상한**: 60 Hz (메시지 간 최소 간격 ≥ 16.67 ms)
- 초과 메시지는 **마지막 값만 유지** (last-write-wins 드롭)하여 다음 슬롯에 전송
- 슬롯 경계는 수신 첫 메시지 시각 기준 정렬

---

## IIR 스무딩 (IIR Smoothing)

각 오브젝트의 방위각·거리·앙각에 1차 IIR 저역통과 필터를 적용한다.

```
y[n] = α · x[n] + (1 - α) · y[n-1]
α = 0.3
```

### 알려진 한계

| 상황 | 영향 |
|------|------|
| 패닝 속도 > 180°/s | 약 **33 ms** 추가 지연 (α=0.3 시정수에 의한 과도 응답) |
| 오브젝트 순간 워프 (장면 전환) | 스무딩으로 인한 위치 드리프트 (~5 프레임) |
| 60 Hz 상한 + α=0.3 조합 | 실효 대역폭 ≈ 8 Hz (인지 가능한 최대 패닝 추적 한계) |

스무딩 우회가 필요한 경우 `/vid2spatial/ctrl/smooth_bypass 1` OSC 명령으로 α=1.0 강제 적용 가능.

---

## 검증 기준 (Acceptance Criteria)

1. `az_adm = -az_pipeline` 수치 테스트 통과
2. `dist_adm = 1.0 - dist_v2s` 수치 테스트 통과
3. 60 Hz 초과 전송 시 수신측 60 Hz 상한 유지 확인
4. IIR 필터 스텝 응답: 5τ 수렴 시간 ≤ 83 ms (α=0.3, 60 Hz)

---

## 관련 ADR

- [ADR-0003](0003-ipc-osc-udp.md) — OSC/UDP IPC 채택 근거
- [ADR-0005](0005-algorithm-dispatch.md) — 알고리즘 디스패치 (거리 기반 렌더러 선택)
