# Spatial Engine v0.2.0 릴리스 노트 (한국어)

**릴리스일**: 2026-05-10 (DAW 핸즈온 검증 후 태그)
**대상**: Linux (Ubuntu 24.04 / GLIBC 2.39 prebuilt; 그 외 배포판은 소스 빌드)
**이전 릴리스**: [v0.1.0](https://github.com/paiiek/spatial_engine/releases/tag/v0.1.0) (2026-05-01)

---

## Highlights — 변경 요약

5가지 핵심 변화를 한 화면에 담았습니다.

1. **ADM-OSC v1.0 호환** — DiGiCo / Avid / Yamaha 콘솔, L-ISA Controller, Spat
   Revolution이 보내는 `/adm/obj/N/{aed, gain, mute, xyz, …}` 패킷을 표준
   포맷 그대로 수신합니다. 한국 라이브 베뉴 첫 도입 시나리오를 위한 핵심 기능.
2. **HOA 디코더 4종 추가** — MaxRE / AllRAD / EPAD / InPhase 4개 알고리즘과
   런타임 OSC 전환(`/sys/ambi_decoder_type i {0..4}`)으로, 룸 / 레이아웃별 최적
   디코딩을 선택할 수 있습니다.
3. **VST3 플러그인 프로덕션 그레이드** — 7개 자동화 파라미터(Pan Az, Pan El,
   Source Width, Master Gain, Ambi Order, Room Preset, Bypass), state v1→v2
   다중 버전 리더, 바이패스 dry pass-through, 1000-iter RT-safety 검증. JUCE-free
   유지.
4. **OFF byte-baseline 게이트** — GHA `ubuntu-24.04` 러너에서 빌드 산출물의
   바이트 + 심볼 해시를 핀하여 의도치 않은 의존성 추가를 차단. 공개
   re-pin 경로(GHA Step Summary) 확보.
5. **한국어 매뉴얼** — 설치 가이드(12장) + 운용 가이드(15장) 한국어 정식 발행.
   라이브 운용 워크플로, OSC 프로토콜 레퍼런스, 트러블슈팅 포함.

---

## Added — 신규 기능

### ADM-OSC v1.0 수신 커버리지 (Phase C3)
- `/adm/obj/N/{azim, elev, dist, aed, gain, mute, xyz, active, width, name}` 디코딩
- 4개 신규 `CommandTag` 추가: `ObjXYZ` (0x06), `ObjActiveAdm` (0x07),
  `ObjWidth` (0x08), `ObjName` (0x09)
- 88-row CSV 호환성 fixture (`core/tests/core_unit/adm_osc_v1_compliance.csv`)
- 3 합성 벤더 fixture (L-ISA, Spat Revolution, d&b Soundscape) — 첫 실제 캡처는
  60일 내 교체 예정
- Soak harness `core/tests/perf/soak_adm_osc_flood.cpp` — 64 obj × 1 kHz × 60 s
- ADR 0006 spec freeze + `MAX_DIST=20.0f` 단일 소스
- 브리지 헬퍼 `bridge/_adm_osc_common.py` 추출
- `--osc-dialect adm` CLI 플래그 (기본값 `legacy`로 v0.1.0 호환 유지)

### HOA 디코더 4종 추가 (M2-HOA Extended)
- **MaxRE**: Legendre-root 기반 (M2HOA-Q7 해결: g_1(N=1)=0.5774)
- **AllRAD**: t-design quadrature projection (O(n×K))
- **EPAD**: two-sided Jacobi SVD (cond>1e10 fallback)
- **InPhase**: Daniel 2000 §3.30, golden vectors 검증
- 5-value `DecoderType` enum + RT-safe 런타임 dispatch
- OSC `/sys/ambi_decoder_type i {0..4}` 컨트롤
- ctest 51/51 PASS

### VST3 플러그인 통합 (Phase C C2 Option B + C2B postmortem)
- JUCE-free vst3sdk 핸드롤 빌드
- 7개 자동화 파라미터 (id 0..6): Pan Az, Pan El, Source Width, Master Gain,
  Ambi Order, Room Preset, Bypass
- state 바이너리 v2 포맷 (36 byte: 8 byte header + 7×float) + v1 multi-version
  리더 (`vst3/SpatialEngineProcessor.cpp:267-289`)
- 바이패스 dry pass-through (channel-wise input→output memcpy)
- `kIsBypass` 플래그 (id=6)
- `restartComponent(kParamValuesChanged)` on `setComponentState`
- 21-assertion in-process host fixture, 1000-iter RT-safety probe
- 53 ctest tests (vst3 7개 포함, 전체 PASS)

### OFF byte-baseline 게이트
- dual-gate: `core/libspe_core.a` + `core/spatial_engine_core` byte+symbol pinning
- GHA ubuntu-24.04 runner reproducible build
- `LD_DEBUG=libs` 런타임 sysdep 감사
- public re-pin 경로 (GHA Step Summary echo)

### LTC 동기 (Phase C1)
- SMPTE LTC biphase 디코더 (25fps 합성 신호 검증)
- `LtcChase` audio→ring→control-thread consumer
- `SpscRing<T,N>` 재사용 가능한 템플릿
- `/sys/ltc_chase` opcode `0x14`

### Phase B feature parity
- per-object Source WIDTH (0..π rad, VBAP/DBAP/WFS/HOA fan-out)
- Snapshot Crossfade (시간 기반 scene 전환)
- per-speaker time-alignment (delay_ms / gain_db)
- per-channel ChannelLimiter + `/output/{ch}/{gain,limit}` OSC
- Object Trajectory Animation (circle / line / lissajous)
- DBAP width 정밀화 + IRConvReverb WAV 로딩 + HOA 2/3차

### vid2spatial 통합
- Phase 2 production bridge + 이중 모드 전환
- WebGUI 통합 (start/stop API + UI 버튼)

### 한국어 매뉴얼
- `docs/manual_kr/install/README.md` (12장)
- `docs/manual_kr/operation/README.md` (15장)

### Phase C4 design contract drafts (v0.3.0 구현용)
- ADR 0010 — VST3 OSC binding model (per-instance recv-only UDP, A1-ε)
- ADR 0011 — multi-instance discovery (file-based JSON registry)
- ADR 0012 — vendor quirks overlay slot (예약)

### 테스트 인프라
- VBAP gain cache (0.5° bin, open-addressing FIFO,
  prepareToPlay invalidation)
- AmbisonicEncoder 2/3차 (ACN/SN3D, 9ch / 16ch 폐쇄형)
- VBAP 3D fallback gain pattern + 수치 테스트

---

## Changed — 변경 사항

- **GHA OFF baseline**: ubuntu-24.04 러너 이미지로 재핀 (3회 사이클; 최종 핀
  해시는 §호환성 참고)
- **`--osc-dialect` 플래그**: 신규 추가, 기본값 `legacy`로 v0.1.0 호환 유지
- **CI 워크플로**: `vst3.yml` 두 job 분리 — `vst3-build-and-host-fixture` +
  `off-byte-identical`
- **WebGUI**: FastAPI lifespan 마이그레이션 + asyncio.run 전환
- **Limiter**: peak-attack 엔벨로프 + gain-ramp warmup, assertion 강제

---

## Deprecated — 사용 중단 예정

(v0.2.0 없음)

---

## Removed — 제거됨

(v0.2.0 없음 — 내부 deslop pass에서 dead code 정리는 §Fixed 참조)

---

## Fixed — 버그 수정

- **AmbisonicEncoder ACN4 계수 정정**: `kSqrt3 → kSqrt3_2` (2x 오차 수정);
  test13 회귀 테스트 추가
- **OlaConvolver alloc-free `process()`** + `SofaBinReader` 방어적 검증
- **WebGUI + bridge 5건 핵심 버그** (사용자 핸드오프 직전 발견)
- **NaN Z assert** + `blockSignals` on `set_object` + `sys` import hoisted
- **SceneController** 핸들러 + `fromJson` 안전성 + 경로 traversal 가드
- **MIDI OSC send** + VBAP 2D 제약사항 문서화
- **pytest 수집**: importlib 모드 + `norecursedirs` + pythonpath/testpaths 확장
- **sofa_inspector IR_len 384** + flaky latency test + `ADDR_METRICS` +
  `utcnow` deprecation

---

## Security — 보안

- **경로 traversal 가드** (`d8056db`): `SceneController` `fromJson`이 악의적
  파일명 페이로드를 거부합니다.
- **NaN Z assert** (`a2b10f9`): `SceneSnapshot` / Z-좌표 경로의 방어적 검증
  강화 — wire 입력의 손상에 대한 fail-fast 동작.

---

## Compatibility — 호환성

### 빌드 환경 (CI baseline)
- **Ubuntu**: 24.04 (Noble Numbat)
- **GLIBC**: 2.39
- **GCC**: 13.3.0
- **CMake**: 3.20+
- **C++ 표준**: C++20

### 런타임 ABI
- `--osc-port` / `--osc-dialect` 기본값 v0.1.0 보존
- Component / Controller IID 변경 없음
- **VST3 state 포맷**: v0.1.0 = state v1 (28 byte / 6 float). v0.2.0 = state v2
  (36 byte / 7 float, C2B postmortem `acb8c27`에서 추가). Multi-version 리더가
  `Processor.cpp:267-289`에 위치하여 v0.1.0 `.vstpreset` 파일이 자동으로
  v1 경로로 로드됩니다. v0.2.0에서 추가 state bump 없음.
- **IPC schema_version**: 1 (변경 없음)

### 구버전 배포판
- **Ubuntu 22.04 / Debian 11 / RHEL 9**: GLIBC 미스매치 (2.35 / 2.31 / 2.34
  < 2.39). Prebuilt `.so`는 작동하지 않으므로 **소스 빌드 필요**.
- 빌드 가이드: `docs/manual_kr/install/README.md` 3장 참조.

### prebuilt 자산 명명 규칙
- `spatial_engine_v0.2.0_linux_glibc239.tar.gz` — GLIBC 버전을 파일명에
  명시하여 다운로드 전 호환성 확인 가능.

---

## Known limitations — 알려진 제약

- **VST3 플러그인 ADM-OSC 라우팅**: Phase C4 / v0.3.0으로 이월 (Plan §1.4
  deliverable matrix 참조). v0.2.0은 design contract drafts(ADR 0010 / 0011
  / 0012)만 출시.
- **macOS / Windows 빌드**: CI 미커버, prebuilt 미지원. v0.3.0+ 후보.
- **DAW 핸즈온 검증**: Reaper 7.x + Bitwig Studio 5.x (Linux) 한정 검증.
- **ADM-OSC 벤더 캡처**: 현재 합성 fixture만 보유; 60일 내 첫 contract 후
  실제 캡처로 교체 예정.

---

## Install — 설치

전체 설치 가이드: [`docs/manual_kr/install/README.md`](../../manual_kr/install/README.md)

빠른 시작:
```bash
# 1. 소스 빌드 (또는 prebuilt 다운로드)
git clone https://github.com/paiiek/spatial_engine.git
cd spatial_engine && git checkout v0.2.0
cmake -B core/build -DSPATIAL_ENGINE_NO_JUCE=ON -S core
cmake --build core/build -j$(nproc)

# 2. 검증
cd core/build && ctest --output-on-failure

# 3. 실행
./core/spatial_engine_core --version
# 출력: 0.2.0
```

---

## What's next — 다음

- [`docs/release/v0.2.0/CHANGES.md`](CHANGES.md) — 88개 commit 전체 인벤토리
- [`.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md`](../../.omc/plans/spatial-engine-phaseC4-and-v0.2-release.md) — Phase C4 v0.3.0 sidecar 계획
- v0.3.0 로드맵: VST3 plugin ADM-OSC 직접 수신 (ADR 0010 / 0011 구현),
  state v3 + `kMute` 8th param, 60-day 벤더 fixture 교체

---

## Acknowledgements

본 릴리스는 RALPLAN(Planner / Architect / Critic 합의) 기반 자율 워크플로로
계획 + 검증되었으며, ralplan 합의 결과는 commit `0282f6b`에 보존되어 있습니다.
