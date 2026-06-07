# Dreamscape Convergence — 마스터 플랜

> ⛔ **SUPERSEDED (2026-06-07)** — 이 문서는 **구(舊) 6-Phase 수렴 플랜**이다. Phase 0/1 완료 후 **`spatial-engine-v1-full-coverage-plan.md`**(v1.0 Full-Coverage)로 대체되었다. 활성 플랜·resume 포인터·Phase 번호의 **단일 진실원천은 v1.0 플랜의 §6 진행 로그**다.
>
> ⚠️ **번호 주의 — 두 플랜의 Phase 번호가 다르다**:
> | 작업 | 이 구 플랜 §4 | **활성 v1.0 플랜(정준)** |
> |---|---|---|
> | 패닝 | Phase 1 | Phase 1(성능 경화에 포함) |
> | 룸 엔진 | Phase 2 | (Phase 1 토대 위 ⑥ 증분) |
> | 디코릴레이션 | Phase 3 | Phase 4 후속 |
> | 헤드트래킹+바이노럴 | Phase 4 | **Phase 2** |
> | ADM-OSC | Phase 5 | **Phase 3** |
>
> 최근 커밋·진행 로그의 "Phase 2=바이노럴 / Phase 3=ADM"는 **v1.0 플랜 기준이며 올바르다**. 이 구 플랜의 §4 번호와 혼동 금지. 아래 §8 진행 로그는 ⑥b(룸)에서 멈춘 **미완 이력**이며, 이후 전체 기록은 v1.0 §6에 있다. (역사적 분석·증분 ①–⑥ 상세는 보존 목적.)

> **목표**: `spatial_engine`이 ① Dreamscape xlsx 스펙 ② `github.com/dreamscapeaudio2023-star/immersive-audio-engine` 레퍼런스 엔진의 기능을 **모두 품으면서**(최소 기준), mmhoa 고유 인프라 우위(테스트·CI·헤드리스·크로스플랫폼·Ambisonics·SHM·VST3)를 유지/강화하여 **하나의 완성·실행 가능한 엔진**으로 수렴.

- **브랜치**: `feat/dreamscape-convergence` (worktree: `/home/seung/mmhoa/spatial_engine-convergence`)
- **베이스**: `main` @ 213b19e (v0.9 Lane F4)
- **전략**: Path A — 레퍼런스 DSP를 mmhoa 코어로 **이식**(레퍼런스를 베이스로 삼지 않음)
- **근거 문서**(이 worktree `.omc/research/`):
  - `ENGINE_GAP_ANALYSIS_20260603.md` — mmhoa ↔ xlsx 스펙 갭
  - `REFERENCE_ENGINE_ANALYSIS_20260603.md` — 레퍼런스 엔진 정밀 분석 + xlsx 정확도 검증
  - `CONVERGENCE_PROPOSAL_20260603.md` — 통합 전략 + JUCE 결합도 실측

---

## ⚙️ 증분 작업 프로토콜 (MANDATORY — 세션 끊겨도 유지)

각 증분은 **반드시** 아래를 끝까지 밟은 뒤에만 다음으로 넘어간다. 구현하고 곧장 다음으로 넘어가지 말 것 (사용자 지침 2026-06-03).

1. **구현** — 하나의 응집된 변경.
2. **단위 테스트 + no-alloc 게이트** — ctest, RT 경로 무할당.
3. **스모크 테스트** — 실제 헤드리스 바이너리 `spatial_engine_core`를 돌려 end-to-end 동작 확인: OSC로 알고리즘 선택+활성+위치 주입 → `--wav` 출력 → **비무음 + L/R 정합** 검사. 유닛 통과만으로 완료 처리 금지. 스모크 스크립트는 `scripts/smoke_*.py`(또는 tests/)에 재사용 가능하게 둔다.
4. **독립 검증** — 전체 ctest 회귀 0, 필요 시 `code-reviewer` 패스(authoring과 분리).
5. **"잘 구현됐고 실제로 돌아간다" 증거 확인 후** 커밋.
6. **durable 상태 갱신**(§8 + 자동 메모리) → 다음 증분.

빌드/스모크는 background 실행 → 완료 통지 후 결과 검증. 참고: [[convergence-increment-protocol]] (자동 메모리).

---

## 0. 성공 기준 (Definition of Done)

엔진이 다음을 **모두** 만족하면 수렴 완료:

1. **패닝**: VBAP 2D/3D + 5단 고도 레이어링, MDAP(K=8 원뿔, 40° 클램프), VAP, DBAP, WFS 전모드(평면파·곡률·obliquity·shaping·VBAP블렌드) — 등음량 Σg²=1.
2. **룸**: Shoebox ER(6방향·pre-delay·Width) + Cluster + N=8 Hadamard FDN(T60 피드백·8 큐브코너) + per-bus EQ + 방향성 wet. (mmhoa IR 리버브도 유지)
3. **디코릴레이션**: Murmur 딜레이 + 1–8단 Schroeder allpass + 에너지보존 dry/wet.
4. **바이노럴**: prefeed LP 4200 / 에너지랭킹 상위24 / 딜레이링 / 5밴드EQ / 모니터게인 + **헤드트래킹**(yaw/pitch/roll → HRTF 회전).
5. **ADM-OSC**: 수신 전메시지 + `app_az=−adm_az` + 클램프 + 송신 브로드캐스터(번들/타임태그) + `/ypr`.
6. **세션**: 3 게인행렬(in→obj/out→spk/spk→dev) + speakerKinds + 50슬롯 레이아웃 라이브러리 + 스냅샷 스피커 포함.
7. **per-object**: 4밴드 EQ + 자유 딜레이(요구 #7b — 양 엔진 공통 결손, mmhoa가 마감).
8. **규모**: 기본 32obj/32spk, 천장 128/128/128.
9. **품질 게이트**(mmhoa 인프라로 레퍼런스 약점 교정):
   - `just test` 전 ctest green, 신규 모듈마다 단위테스트.
   - no-alloc 게이트(`RtAssertNoAlloc`) 오디오 경로 통과 — **이식 코드의 `std::vector` 힙할당 제거 포함**.
   - 좌표 왕복 항등 + **L/R 정합 골든 테스트** 통과.
   - soak/p99 RTT 회귀 없음. CI 4종(cross-platform/relacy/vst3/release) green.

---

## 1. 아키텍처 결정 (확정)

| # | 결정 | 내용 |
|---|---|---|
| AD-1 | 전략 | Path A 이식. mmhoa 코어·인터페이스·OSC·CI 유지 |
| AD-2 | 정준 좌표 | **mmhoa 규약 유지**(+X 우/+Y 상/+Z 전, `az=atan2(x,z)`). 이식 알고리즘 입력단에 **Y↔Z 스왑 어댑터**. *(사용자 확정 필요 — §7 D1)* |
| AD-3 | HRTF 스택 | mmhoa `.speh`+OlaConvolver+KdTree **유지**. 레퍼런스에서 **헤드회전 수학만** 이식. *(D2)* |
| AD-4 | 이식 코드 격리 | 이식 DSP는 **분리 모듈**(`core/src/render/ported/`, `core/src/reverb/room/`)에 출처 헤더와 함께. 라이선스/제거 용이성 + 테스트 격리 |
| AD-5 | 라이선스 | 레퍼런스 라이선스 불명 → **소스 직접 복사 전 권리 확인**. 알고리즘은 공개논문(Pulkki/Lossius/Kareer) 기반 재구현은 자유. *(D3 — Phase 0 게이트)* |
| AD-6 | 인터페이스 | 기존 `RenderingAlgorithm`(`ObjectState`, `AlgoScratch`) 유지·확장. 신규 알고리즘은 동일 계약 |
| AD-7 | 안정성 | main 항상 green. Phase = 머지 단위. additive-first(기존 코드 swap은 검증 후) |

**JUCE 결합도 실측(이식 가능성 근거)**: 패닝 전체(Vbap/Vap/Dbap/Wfs/SpatialMath/SpeakerDecorrelation, ~1,270줄) **juce::=0**. RoomEngine(61)/Binaural(18)/Kemar(29)는 전부 사소한 헬퍼(`jlimit→clamp`, `AudioBuffer→float*`, `dsp::IIR→biquad`, `MathConstants→M_PI`). → T0 drop-in / T1 기계치환.

---

## 2. 현 코드 결선 지점 (실측)

- 렌더 인터페이스: `core/src/render/RenderingAlgorithm.h` — `ObjectState{az_rad,el_rad,dist_m,active,width_rad}`, `AlgoScratch.gains[64][MAX_OBJECTS]` (**스피커 차원 64 하드코딩 → 128 리프트 대상**), `processBlock(objects, dry_mono, out, num_samples)`.
- 알고리즘 enum: `core/src/ipc/Command.h` `Algorithm{VBAP,WFS,DBAP,Ambisonic}` → **VAP 추가**(MDAP는 width_rad 경유 VBAP 모드).
- 상수: `core/src/core/Constants.h` `MAX_OBJECTS`(SPE_MAX_OBJECTS 64/128), `MAX_BLOCK=512`, `SOUND_C=343`.
- 좌표: `core/src/coords/Coords.h` (`az=atan2(x,z)`).
- 기존 렌더러: `core/src/render/{VBAPRenderer,DBAPRenderer,WFSRenderer,AmbisonicRenderer}`, 공용 `AlgorithmAnalyticReference.cpp`, 게인캐시(0.5°-bin, AZ_OFFSET=1440 — **유지할 perf 자산**).
- 리버브: `core/src/reverb/{FdnReverb,IRConvReverb,ReverbEngine}`.
- per-object: `core/src/dsp/PerObjectChain.h`(EQ4Band/DelayLine/DistanceGain/DistanceLPF/PropagationDelay 보유).
- 빌드/검증: `just build-test`, `just test`(ctest), `just measure-latency`, `just accuracy`, no-alloc `util/RtAssertNoAlloc`, CI `.github/workflows/{cross-platform,relacy,vst3,release}.yml`.

---

## 3. 모듈별 통합 방침 (요약 — 상세는 CONVERGENCE_PROPOSAL §4)

| 영역 | 방침 | 이식 등급 |
|---|---|---|
| VBAP 2D/3D + 5단 고도 | 코어 교체(게인캐시 래퍼 유지) | T0 |
| MDAP | 신규 이식 | T0 |
| VAP | 신규 이식 + enum 추가 | T0 |
| WFS 전모드 | 코어 교체 | T0 |
| 룸 엔진 | FdnReverb→RoomEngine 교체 | T1 |
| IR 리버브 | mmhoa 유지(우위) | — |
| 디코릴레이션 | drop-in | T0 |
| HRTF 코어 | mmhoa 유지 | — |
| 헤드트래킹 | 신규 이식(worldDirToHead) | T0 |
| 바이노럴 모니터 체인 | 이식+신규 | T1 |
| ADM-OSC | mmhoa 확장(부호/메시지/송신) | mmhoa |
| 세션(행렬/kinds/50슬롯) | mmhoa 확장 | mmhoa |
| per-object EQ·딜레이 | mmhoa 마감 | mmhoa |
| 128채널 | mmhoa 수정 | mmhoa |
| VST3/Dante/LTC/Cue/Ambi/헤드리스/UI | mmhoa 유지 | — |

---

## 4. 단계별 실행 계획

> 각 Phase 공통 게이트: **이식 → 단위테스트 + no-alloc → 골든 회귀 → `just test` green → 머지**. 레퍼런스 "테스트 0" 약점을 이식과 동시에 메움.

### Phase 0 — 기반 & 블로커 해소 ⭐ (먼저 단독 완료)
**목적**: 모든 이식의 전제조건 확정. 저비용·고확신.

- **0.1 라이선스/출처 확인 (게이트 D3)**: 레퍼런스 코드 직접 이식 가부 결정. 불가 시 "논문 기반 재구현" 모드로 전환(코드 복사 금지, 분석 보고서만 참조).
- **0.2 정준 좌표 어댑터**: `core/src/coords/`에 ref↔mmhoa 축 변환(Y↔Z) 유틸 + 명세 주석. 경계(이식 알고리즘 입력단)에서만 적용.
- **0.3 좌표 골든 테스트**: ① az/el→xyz→az/el 왕복 항등 ② **L/R 정합 골든**(우측 소스 → R 우세) — 메모리의 L/R 반전 버그(2026-02-08/02-25/03-01) 재발 차단.
- **0.4 DSP 프리미티브 shim**: 범용 RBJ biquad(`dsp::IIR` 대체), `no_denormals` 가드, `AudioBuffer→float*` 어댑터, clamp/lerp. (mmhoa EQ4Band 재사용 검토)
- **0.5 128채널 천장 리프트**: `RenderingAlgorithm.h`의 `gains[64]`→128(또는 컴파일상수), `SPE_MAX_OBJECTS=128` 경로 + 메모리 예산 재검증.
- **0.6 이식 모듈 스캐폴딩**: `core/src/render/ported/`, `core/src/reverb/room/` 디렉터리 + CMake + 출처 헤더 템플릿.
- **게이트**: `just build-test` + 좌표 골든 green, no-alloc 통과, 128 빌드 성공.

### Phase 1 — 패닝 완성 (최대 이득·최저 위험)
- VAP(`Vap`), MDAP(VBAP 원뿔 샘플링), WFS 전모드(`WfsDrivingParams`), 5단 고도 레이어링(`fillVbapMaskForObject`) 이식.
- mmhoa VBAP/WFS 코어 교체, 게인캐시 래퍼 유지, `Algorithm` enum에 VAP 추가.
- ⚠ 2D-VBAP/MDAP의 `std::vector` 힙할당 → 고정버퍼 교정(no-alloc 게이트).
- **검증**: 알고리즘별 등음량·기준방향 단위테스트, `just accuracy` 회귀.

### Phase 2 — 룸 엔진 교체 (T1)
- RoomEngine 이식(juce 헬퍼 기계치환), per-bus EQ는 biquad/EQ4Band로. FdnReverb→RoomEngine swap.
- **검증**: ER/Cluster/FDN 임펄스 응답 테스트, T60 감쇠 검증, no-alloc.

### Phase 3 — 스피커 디코릴레이션 (drop-in)
- SpeakerDecorrelation 이식 + 결정론 시드/에너지보존 단위테스트.

### Phase 4 — 헤드트래킹 + 바이노럴 모니터 체인
- `worldDirToHead` 회전을 mmhoa HRTF 조회 앞단 삽입 + `/ypr` OSC(또는 9100 통합).
- prefeed LP 4200 / 에너지랭킹 상위24 / 딜레이링 / 5밴드EQ / 모니터게인 버스 구성.
- **검증**: 헤드회전 정합(yaw +90° → 음상 회전) 골든, no-alloc.

### Phase 5 — ADM-OSC 완성 + 세션 확장
- 부호반전(`app_az=−adm_az`) / 누락메시지(/x /y /z /xy, name 저장, mute 독립) / 클램프 / 송신 브로드캐스터+번들 / `/ypr`.
- 3 게인행렬 + speakerKinds enum + 50슬롯 레이아웃 라이브러리 + 스냅샷 스피커 포함.
- **검증**: OSC 라운드트립 테스트, 행렬 라우팅 테스트.

### Phase 6 — per-object EQ·딜레이 마감 + 통합 검증
- per-object 4밴드 EQ·자유딜레이 OSC/UI 노출(요구 #7b).
- 전 알고리즘 등음량·CPU예산·soak·p99 종합. 32/32 기본 + 128 천장 확인. CI 4종 green.

---

## 5. 검증 전략

- **단위**: ctest(`core/tests/core_unit`) — 신규 모듈마다. 등음량, 폴백 체인, 결정론, 임펄스 응답.
- **RT-safety**: `RtAssertNoAlloc` 오디오 경로 — 이식 코드의 모든 동적할당 제거.
- **골든 오디오**: 좌표 왕복/L/R, 헤드회전, 알고리즘 기준 출력 회귀.
- **성능**: `just measure-latency`, soak, p99 RTT — Phase별 회귀 감시.
- **CI**: cross-platform(Linux/win/mac 컴파일), relacy(동시성), vst3, release.

---

## 6. 리스크 레지스터

| 리스크 | 영향 | 완화 |
|---|---|---|
| 좌표 규약 혼선 → L/R 반전 | 高 | Phase 0 어댑터 + L/R 골든 테스트 선행 |
| 레퍼런스 라이선스 불명 | 高(법적) | D3 게이트, 모듈 격리, 필요시 논문 재구현 |
| 이식 코드 RT 위반(std::vector) | 中 | no-alloc 게이트, 고정버퍼 교정 |
| 128채널 메모리/CPU 폭증 | 中 | 예산 재검증, 기본 32 유지 |
| 기존 엔진 회귀 | 中 | additive-first, main green, Phase 머지 게이트 |
| O(N³) VBAP3D 대형 레이아웃 | 低 | 게인캐시 유지, 필요시 후속 최적화 |

---

## 7. 사용자 결정 사항 (Decision Log)

| ID | 질문 | 권고 | 상태 |
|---|---|---|---|
| D1 | 정준 좌표: mmhoa 유지+어댑터 vs ref/ADM 표준 통일 | **mmhoa 유지** (vid2spatial 락인) | ✅ **확정: mmhoa 유지+어댑터** (2026-06-03) |
| D2 | HRTF: `.speh` 유지+헤드회전만 vs libmysofa+KEMAR 임베딩 | **`.speh` 유지** | ✅ **확정: `.speh` 유지+헤드회전만** (권장대로) |
| D3 | 레퍼런스 소스 직접 이식 가부(라이선스) | **권리 확인 후** / 불명 시 논문 재구현 | ✅ **확정: 직접 이식 가능(권리 있음)** — 출처 헤더+ported/ 격리 (2026-06-03) |
| D4 | 범위: 6 Phase 전체 vs Phase 0–2 선행 후 재평가 | **Phase 0 단독 → Phase 1 PoC → 재평가** | ✅ **확정: Phase 0 → Phase 1 PoC → 재평가** (2026-06-03) |

---

## 8. 진행 로그
- 2026-06-03: worktree+브랜치 생성, 분석 3종 `.omc/research/` 동봉, 마스터 플랜 작성.
- 2026-06-03: 결정 D1–D4 확정(§7).
- 2026-06-03: **Phase 0.2/0.3 완료 ✅** — 정준 좌표 어댑터(`coords/Coords.h`: `mmhoa_to_ported`/`ported_to_mmhoa`/`pipeline_dir_to_ported`, Y↔Z 스왑) + 골든 테스트(`test_convergence_coords.cpp`, CMake 등록). **ctest 100% green**(convergence_coords + p2_coords 회귀 없음). L/R 불변식 락인.
  - 부수: baseline 빌드 블로커 수정 — `ipc/EchoSubscriber.cpp:63` `memset`→값초기화(GCC 13 `-Werror=class-memaccess`). `just build-test`(WERROR=ON) 로컬 빌드 복구.
- 2026-06-03: **Phase 0.6 + Phase 1 PoC 완료 ✅** — `core/src/render/ported/`(SpatialMath.h/Vbap.{h,cpp}/Vap.{h,cpp}, 출처헤더, namespace iae, juce-free) spe_core 결선. `test_convergence_vap_port`: 좌표어댑터로 mmhoa→ported 변환한 옥타헤드론 리그에 VAP 구동 → 등음량·L/R 불변식·체적vs방향성 검증. **ctest green**. 커밋 fb061be. **D4 PoC 게이트 달성** — 이식 논리 end-to-end 증명(레퍼런스 DSP가 mmhoa 트리에서 컴파일·동작).
- 커밋: 3d6e0ef fix / b4f2228 coords / 52fd85a plan / fb061be VAP-PoC.
- 2026-06-03: **Phase 1a VAP 라이브 통합 완료 ✅** — `Algorithm::VAP=4`(Command.h/decoder), `VAPRenderer`(prepare 시 스피커 ported 프레임 사전계산, per-obj 좌표어댑터 변환, no-alloc), SpatialEngine 버킷/디스패치/scratch/sum 결선. `test_convergence_vap_renderer`(우 객체→우 스피커). **전체 ctest 122/122 green(WERROR off 빌드) — VAP 통합 기능 회귀 0**. 내 신규 코드는 WERROR=ON에서도 클린(spe_core WERROR 빌드 통과).
- **알려진 baseline 이슈(convergence 범위 밖, 별도 위생작업)**: GCC 13.3 + `-Werror`에서 기존 테스트파일 6종 경고로 빌드 실패 — test_p_ambi(unused var), test_p_speaker_alignment/test_p_ambi_decoder_max_re(unused function), test_shm_telemetry_emitter/test_shared_ring_backend(memset(RingHeader) class-memaccess + unused test fns). CI는 더 관대한 툴체인이라 green. → executor로 `[[maybe_unused]]`/value-init 정리 위임 중.
- 2026-06-03: **baseline 위생 완료 ✅** — GCC13+`-Werror` 테스트 경고 6파일 정리(executor + placement-new 수정 `new (h) RingHeader{}`). **전체 ctest 122/122 green under WERROR=ON** (`just build-test` 게이트 로컬 완전 통과). 커밋: c326f2d(VAP) / 161ca49(plan) / <test-fix> / <plan>.
- 2026-06-03: **VAP 증분 프로토콜 전단계 검증 완료 ✅** — 스모크(`scripts/smoke_vap.py`, 실제 바이너리: az=+90°→ch3/4 우측링만, xruns=0, L/R end-to-end 확정) + code-reviewer 패스(COMMENT/shippable, CRITICAL/HIGH 0) + MEDIUM 2건 수정(LayoutCompatibilityChecker VAP 케이스, VAPRenderer >64 assert). 커밋 26e9515. **스모크 템플릿 확립** → 이후 증분 재사용.
- 2026-06-03: **② VBAP 3D 5단 고도 레이어링 증분 — 프로토콜 전단계 검증 완료 ✅**
  - **이식**: `core/src/render/ported/VbapMask.{h,cpp}`(iae, juce-free, 출처헤더 f2cb796) — 레퍼런스 `fillVbapMaskForObject`/`buildElevatedObjectVbapMask`/`countVbapMaskTrue`/`speakerOnHorizontalVbapLayer` + 5단 상수(52/62/74/86° + steep-source 0.70/0.22°) **바이트 충실 이식**. 어댑테이션은 `SpatialAudioPull→raw Vec3*`, `std::array<bool,N>→raw bool*`, juce pi→리터럴 (전부 기계적, 로직 무변경).
  - **결선**: `AlgorithmAnalyticReference::vbap_gain_3d_into`에 마스크 후보 제한 — 스피커·소스를 `coords::mmhoa_to_ported`(Y↔Z, up=z)로 ported 프레임 변환 후 `fillVbapMaskForObject`로 참가마스크 생성 → 삼중항 탐색 + 최근접3 폴백을 마스크로 제한. **안전장치**: 참가 <3이면 마스크 무시(언더셀렉트 방지). 전부 스택(no-alloc) — RT 경로 유지.
  - **버그 수정(잠복 빌드 블로커)**: `LayoutCompatibilityChecker.h` enum에 `Algorithm::VAP=4` 추가 — 직전 VAP 증분이 `.cpp`에 `case Algorithm::VAP`를 넣었으나 enum에 미추가, **stale object로 가려져 있던 컴파일 에러**를 fresh 빌드가 표면화. (ipc::Algorithm::VAP=4 미러; Ambisonic=3은 이 체커에 의도적 부재.)
  - **단위**: `test_convergence_vbap_elevation` — 마스크 단독(flat→수평층만, elevated→인접/반대코너 제외, steep→천장참가) + vbap_gain 통합(flat→하부링 Σupper E≈0, elevated→상부링 Σupper E↑·반대코너=0, Σg²=1, L/R). **PASS**.
  - **스모크**: `scripts/smoke_vbap_elevation.py`(실제 바이너리, lab_8ch 3D 리그) — elevated(az+45,el+30)→ch7 상부우측 100%, flat(az+45,el0)→ch3 하부우측 100%, right>left, **xruns=0**. 고도 상승이 음상을 돔 상부로 라우팅함을 end-to-end 확정.
  - **검증**: 전체 ctest **123/123 green under WERROR=ON**(122 + 신규1), 회귀 0, no-alloc(p1_rt_no_alloc) pass. **code-reviewer APPROVE/shippable**(CRITICAL/HIGH 0; MEDIUM 1=기존 no-alloc 센티넬이 신규 3D 경로 미커버[증분이 도입한 결함 아님, 후속]; LOW 2=옵션). LOW#2(objectFlat 카르테시안 형태 주석) 적용.
- 2026-06-03: **③ MDAP(소스 width/spread) 증분 — 프로토콜 전단계 검증 완료 ✅**
  - **설계 결정(③-B, 기록)**: ported `computeSpatialMdap` 와이어링 대신 **mmhoa 네이티브 콘/아크 샘플링**으로 구현 — 증분 ②에서 고도 마스크+게인캐시가 네이티브 `vbap_gain_3d_into`에 들어갔으므로, MDAP를 그 위에 얹어 width=0↔width>0 일관성·캐시·마스크 재사용·2엔진 분기 회피. (ported 2D-VBAP/MDAP의 `std::vector` no-alloc 정리는 ported `computeHorizontalVbap` 자체가 alloc → 별도 증분 ③'로 분리; 라이브 경로 아님.)
  - **구현**: `AlgorithmAnalyticReference::vbap_mdap_gain_into(layout, az, el, spread_deg, out, cap)` — K=8, spread [0,40°] 클램프(ref `kMdapSpreadMaxDegrees`), 2D(max|y|<1e-3)=방위각 아크 / 3D=접선기저 콘(반각 spread/2), 샘플마다 `vbap_gain_into`(고도 마스크+2D/3D 디스패치 상속) 합산→L2 정규화, 퇴화 시 점원 폴백. 스택 전용 no-alloc. VBAPRenderer width 경로(주 + 테이블-풀 폴백)의 구식 uniform-blend 제거 → width>0이면 MDAP, 아니면 점원. 캐시 키에 width 이미 포함.
  - **단위** `test_convergence_mdap`: spread=0==점원, 분포 확장(2→4), Σg²=1, 40° 클램프, L/R, 2D 아크 strict 확장(dense 24링 2→4). **PASS**.
  - **스모크** `scripts/smoke_mdap.py`: 실제 바이너리, 생성한 dense 24스피커 링(15° 간격<40°반각 → 이웃 모집), width=0 vs 40° → active 2→4, right>left, MDAP가 베이스라인 대비 xrun 추가 0(24ch null 백엔드 콜드스타트 1-block xrun은 point/spread 동일 → MDAP 무관). **PASS**.
  - **검증**: 전체 ctest **124/124 green WERROR=ON**(123+신규1), 회귀 0, no-alloc green. **code-reviewer APPROVE/shippable**(CRITICAL/HIGH/MEDIUM 0; LOW 2=width-디스패치 중복/매직넘버, COMMENT 3). 리뷰가 내 스모크 주석의 "ObjMove가 width를 clobber" 설명 오류 지적 → 코드 재확인(ObjMove는 az/el/dist/active만 설정, width 미변경; ObjDsp-7·ObjWidth 직접 커밋) → 주석 수정.
- 2026-06-04: **④ WFS 전모드 증분 — 프로토콜 전단계 검증 완료 ✅**
  - **이식**: `core/src/render/ported/{Wfs.h,Wfs.cpp,WfsDrivingParams.h,SpeakerKind.h}`(iae, juce-free, 출처헤더 f2cb796) — 레퍼런스 `computeWavefieldSynthesisDriving` **바이트 충실 이식**(code-reviewer가 `diff`로 로직 무변경 확인). 어댑테이션은 include 경로·`namespace iae` 래핑·`kPrototypeChannels`는 ported/SpatialMath.h·`SpatialSessionState.h` include 제거뿐. 평면파/곡률(wavefrontCurvature)/obliquity(cosφ radialBlend)/게인·딜레이 shaping을 단일 커널이 모두 포함.
  - **결선**: `WFSRenderer` 재작성 — 구식 ad-hoc(r/c 절대지연·1/√r·crude width) 제거, ported 커널로 per-spk 게인+상대지연 산출. **핵심: 커널은 frame-agnostic**(전부 dot·유클리드 길이, 단일 일관 프레임) → mmhoa 네이티브 좌표 그대로 입력, **Y↔Z 어댑터 불필요**(VAP/VBAP와 다른 점). 스피커 forward=normalized(−pos)(리스너 향함, 레퍼런스 "orientation toward listener" 규약). VBAP 블렌드(vbapGainBlend)는 네이티브 `vbap_gain_into` 재사용(②/③ 고도·2D/3D 코어 공유) → g=(1−b)·wfs+b·vbap, 지연 ×(1−b), Σg²=1 재정규화. prepareToPlay서 스피커 지오메트리 사전계산, F5-M3b lazy-alloc/ready_ 핸드셰이크 유지. WfsDrivingParams 디폴트(레퍼런스 미드레인지)+`setWfsParams` 세터; OSC/세션 파라미터 plumbing은 후속 증분(VAPRenderer 패턴).
  - **단위** `test_convergence_wfs`: 구면 Σg²=1·상대지연(min=0)·소스측 최대, 평면파(패턴≠구면), 곡률(지연패턴 변화), obliquity(off-radial forward로 blend 0vs1 차이), 게인shaping(피크↑), 딜레이shaping(스케일↑/↓), VBAP블렌드(렌더러 E2E: 워밍업+지속입력 정상상태, 우측 바이어스, **blend=1이 wavefront를 triplet로 축소 → active< pure WFS로 블렌드 경로 실행 증명**). **PASS**.
  - **스모크** `scripts/smoke_wfs.py`: 실제 바이너리, dense 24링, 소스 az±40 dist=4(링밖). **WFS 분산 wavefront 8스피커 vs VBAP 베이스라인 2**(WFS≠VBAP를 라이브로 입증), 좌우 소스 따라 lateralisation 반전, 콜드스타트 베이스라인 대비 xrun 추가 0. **PASS**.
  - **검증**: 전체 ctest **125/125 green WERROR=ON+RT_ASSERTS=ON**(124+신규1), 회귀 0. processBlock no-alloc(스택 std::array scratch + 사전할당 delays_/ramps_ 인덱싱 + RT-safe vbap_gain_into; S≤64 클램프) 정적확인. **code-reviewer APPROVE**(CRITICAL/HIGH 0; MEDIUM 2=둘 다 테스트/스모크 위생 → 즉시 반영: 블렌드경로 실행 단정 추가·±180° 주석 수정; LOW 3 비차단).
  - **⚠ 발견(중요·기록)**: `/obj/algo` OSC 와이어포맷은 `[seq,id,obj,algo]` **4-int**. CommandDecoder가 `,ii…` 메시지의 **앞 2 int를 seq/id 트랜잭션 헤더로 소비**(payload_int_offset=2). 기존 smoke_vap/smoke_mdap은 `[obj,algo]` 2-int만 보내 algo가 조용히 VBAP 디폴트로 폴백 → **그 스모크들은 사실상 VBAP만 렌더했고 right>left가 VBAP로도 성립해 spuriously PASS**. WFS 스모크는 VBAP 베이스라인과 active-count 비교로 알고 구분을 강제. → smoke_vap/smoke_mdap도 4-int로 고쳐야 진짜 VAP/MDAP 검증됨(후속).
- 2026-06-04: **⑤ Phase 0.5 — 스피커 차원 64→128 리프트 — 프로토콜 전단계 검증 완료 ✅** (커밋 6a39e67)
  - **단일 진실원천**: `Constants.h` `MAX_SPEAKERS`(매크로 `SPE_MAX_SPEAKERS`, 디폴트 64, 천장 128) — `MAX_OBJECTS` 패턴 정확 미러. cmake 캐시변수 `SPATIAL_ENGINE_MAX_SPEAKERS∈{64,128}`(top + core, PUBLIC on spe_util). bare 빌드 바이트동일.
  - **리프트 범위**: 모든 스피커차원 스크래치가 `MAX_SPEAKERS` 파생 — RenderingAlgorithm AlgoScratch.gains; VBAP/VAP/DBAP ramps_ + per-block 스택버퍼(final_gains/gain_acc/g_v/gains); VAP spk_pos/dir_ported_; WFSRenderer::MAX_SPEAKERS; AAR kMaxVbapSpeakers; SpeakerLayout kMaxYamlChannel. ported `kPrototypeChannels=128`은 안전천장이라 무변경. **객체/블록/캐시/버전 상수는 의도적 불변**(over-reach 0 — 리뷰 grep 확인).
  - **부수(리뷰 MEDIUM 반영)**: DBAPRenderer prepareToPlay에 VBAP/VAP와 동일한 assert+clamp 추가(유일하게 가드 없던 렌더러; ramps_ 리셋 루프가 >cap에서 OOB였음. LayoutLoader가 YAML은 이미 바운드하나 프로그램적 호출자 방어).
  - **단위** `test_convergence_max_speakers`(cap-agnostic, #if 없음, 양 빌드 통과): 4 렌더러 모두 N==MAX_SPEAKERS 리그 수용, 고인덱스 스피커(5N/8 → 128빌드서 **인덱스 80**) 렌더 — VBAP/DBAP/WFS는 정확히 그 스피커가 최대(argmax=kTarget, near/total=1.0), VAP는 전 128채널 버스 exercise(유한·비묵음). **PASS**.
  - **검증**: **양 빌드 ctest 126/126 green WERROR=ON+RT_ASSERTS=ON** — 디폴트 64 빌드 + `-DSPATIAL_ENGINE_MAX_SPEAKERS=128` 빌드 모두. 회귀 0, no-alloc 센티넬 green.
  - **스모크** `scripts/smoke_max_speakers.py`(실제 바이너리, build-128): 100스피커(>64) 엔진이 100채널 버스 렌더 + VBAP 소스를 **채널 인덱스 70(>64)**로 라우팅. xrun은 채널수 무관(24ch=0, 100ch=0; smoke의 2 xruns는 OSC 버스트/콜드스타트, 차원 무관). **PASS**.
  - **검토**: **code-reviewer APPROVE**(CRITICAL/HIGH 0; MEDIUM 1=DBAP 가드 → 즉시 반영; LOW 3 중 stale 주석 1건 반영, VAP under-assert·cmake DRY 비차단). 반영 후 재빌드 양 빌드 126/126 재확인.
  - **⚠ 발견(기록·후속)**: VAP가 합성 돔(2링)에서 고도(el=+35°) 소스를 하부링 스피커로 오라우팅(argmax 하부) — 버퍼 리프트와 무관(VAP 패닝 정확성, convergence_vap_renderer가 별도 검증). 64 빌드(=개정전 동치)서도 동일 → 리프트가 도입한 결함 아님. VAP 고도 라우팅 정합성 별도 조사 필요.
- 2026-06-04: **⑥a 룸엔진 — 후기 FDN 코어 이식 완료 ✅** (커밋 f450b71)
  - **이식**: `core/src/render/ported/RoomFdn.{h,cpp}`(iae, juce-free, 출처 f2cb796) — 레퍼런스 RoomEngine 후기 FDN 신호경로 **바이트충실 이식**: 입력확산 allpass 2단(0.72/0.62, 256탭), SR스케일 8딜레이라인(601..1487@48k, clamp[64,8192], 버퍼 L+maxBlock+8), per-line 1-pole HF damping(a=exp(−2π fc/SR), bright=0.14+0.86·ratio), Sylvester-8 Hadamard 피드백(gLoop=exp(−ln1000·tMean/t60)·0.918, gH=gLoop/√8, inj=0.88/√8). 코어는 per-line 탭 d[k] 출력(레퍼런스가 패너에 넣는 신호). juce→std(jlimit→clamp 인자순서 검증, 2π 리터럴=MathConstants::twoPi 비트동일).
  - **범위 경계**: 공간분배(cube-corner VBAP+uniform-diffuse 블렌드)·early reflections·cluster·라이브 SpatialEngine 결선은 후속 ⑥b/⑥c. ⑥a=자기완결 DSP 코어.
  - **단위** `test_convergence_room_fdn`: 안정·유한성, 감쇠(eLate≪eEarly), **T60 순서(long/short 후기에너지비 ~1.2e7)**, HF damping(bright2.0 vs damped0.011), 8라인 디코릴, reset() 비트정확(diff=0). **양 빌드 ctest 127/127 green WERROR+RT_ASSERTS, 회귀0**. process() RT-safe(alloc는 prepare()만).
  - **검토**: **code-reviewer APPROVE** — DSP 무편차(clamp 인자순서·wrap off-by-one·상수 모두 일치). LOW 2(allpass 로컬 wrap 람다 중복=출처충실, params 디폴트=SpatialAudioPull 값 일치 확인) 비차단.
  - **다음(⑥b)**: RoomFdn 8라인 탭 → cube-corner VBAP(네이티브 `vbap_gain_into` 재사용, kLatePerLineGain=0.068) + SpatialEngine 룸버스 결선(per-object wet send → late bus → finishBlock 패턴 → outputBus 합산). 그 후 실바이너리 스모크. 레퍼런스 RoomEngine.cpp:520-592(late 방향바이어스/cube corner VBAP), 729-740(per-line 출력), AudioEngine.cpp:842/908(결선점). cluster+Shoebox early는 ⑥c.
- 2026-06-04: **⑥b 룸엔진 — 공간 후기 리버브 라이브 결선 완료 ✅** (커밋 aea0f11)
  - **결선**: OSC `/reverb/select ,s "room"`(active_reverb_==2) → 모노 reverb send이 `iae::RoomFdn` 구동 → 8 라인 탭을 cube-corner VBAP 게인(prepareToPlay서 사전계산: 8코너 {±1,±1,±1}/√3 mmhoa프레임 → az=atan2(x,z),el=asin(y) → 네이티브 `vbap_gain_into`)으로 스피커버스에 fan-out. kLatePerLineGain=0.068(레퍼런스).
  - **RT**: 게인 컨트롤스레드 계산·room_ready_ 가드로 오디오스레드 RT-safe 읽기. room_fdn_.process no-alloc, room_lines_ prepareToPlay서 kOrder*max_block 사전할당. room 모드선 모노 fdn_/ir 스킵 + uniform else 우회 → 단일 분배경로(모노 누수/중복 없음).
  - **범위**: cube-corner 고정방향만. 소스방향 바이어스(opp)+uniform-diffuse 블렌드(RoomEngine.cpp:543-583)·early reflections·cluster는 ⑥c. → 공간적이나 아직 소스 비상관 후기장.
  - **단위** `test_convergence_room_spatial`: 8코너가 3D돔서 7/8 distinct 스피커, +y→상부링/−y→하부링(고도 정상). **양 빌드 ctest 128/128 green WERROR+RT_ASSERTS, 회귀0, no-alloc green**.
  - **스모크** `scripts/smoke_room_reverb.py`(실바이너리): 하부 dry 객체는 상부링 에너지 **0**, room 모드는 FDN 후기를 **상부링 8스피커 전체(3.3e8)**로 fan(dry가 못 만드는 에너지), xruns=0. ⚠ 측정주의: dry가 시끄러워 per-spk diff는 run-jitter에 가려짐 → **dry 미도달 반구(상부링) 격리**로 깨끗이 검증.
  - **검토**: **code-reviewer APPROVE**(RT no-alloc·in-bounds·단일분배·cube수학 왕복 정확).
- 2026-06-04: **⑥b hardening 완료 ✅** (커밋 9f99cbf) — 리뷰 MEDIUM 2건 해소: (1) mode-switch(active_reverb_ 2로 진입, prev!=2)시 `room_fdn_.reset()`(no-alloc fills, RT-safe; stale-tail 제거) (2) `test_convergence_room_engine`(실제 SpatialEngine::audioBlock 구동 — 하부객체 upperOff≈9e-7 vs upperOn=0.32 상부 8스피커 전체; room_ready_ precompute+rev==2 게이팅+fan-out 라이브 커버). **양 빌드 ctest 129/129 green**.
- 2026-06-04: **⑥c Shoebox early-reflection 컴퓨트 코어 이식 완료 ✅** (커밋 a3efec4, 푸시됨)
  - **이식**: `core/src/render/ported/RoomEarly.{h,cpp}`(iae, juce-free, 출처 f2cb796) — firstOrderImage(6벽 ±x/±y/±z, RoomEngine.cpp:20-39) + earlySpreadDirection(Gram-Schmidt 콘 spread, 52-78) + per-reflection 컴퓨트(335-348,421-462): half clamp max(0.5,|h|), dDirect, erGlobal=0.62√(bal+0.05), extra, delaySmp=clamp(round(extra/343·sr),1,ringLen-2), tapGain=erGlobal·0.52/√(1+extra²·0.08). **바이트충실**(π 리터럴=MathConstants::pi 비트동일). 코어=벽당 {dir,delaySamples,gain,valid} 산출.
  - **범위 경계**: predelay/absorption-EQ/width-spread VBAP/ring-buffer 렌더링+라이브 결선은 ⑥d(RoomFdn ⑥a→⑥b 패턴).
  - **단위** `test_convergence_room_early`: 정확 image 지오메트리, 딜레이 순서(−x 1528>+x 1252), 거리감쇠, 룸크기 스케일, 라이브 ring 포화 clamp(510), width-spread(0=항등/>0 대칭). **양 빌드 ctest 130/130 green, code-reviewer APPROVE(무편차)**.
- 2026-06-04: **⑥d Shoebox early reflections 라이브 결선 완료 ✅** (커밋 7c9f0bf, 푸시됨)
  - **결선**: room 모드서 active 객체별 send-scaled dry(`dry_scratch_[i]×reverb_send`, late FDN per-obj send과 **비트동일**)→6 per-image ring-buffer(`computeFirstOrderReflections`, obj_cache_ az/el/dist→mmhoa pos) 지연 + width-spread VBAP(3샘플 `earlySpreadDirection`→`vbap_gain_into`)로 mix_buf_ fan, 삼각 {1,2,1}/4. RoomEngine.cpp:421-485 충실. `renderRoomEarly`.
  - **RT**: er_rings_[MAX_OBJECTS*6*512] 사전할당, VBAP per-block-per-reflection 호이스트, per-sample은 ring+MAC, no-alloc. mode-switch시 er_rings_+write_pos 클리어(FDN reset 대칭).
  - **검증**: **양 빌드 ctest 130/130 green**(RT no-alloc 커버=convergence_room_engine). 스모크 `smoke_room_early.py`: 하부 객체 ceiling 반사 방위각 추종(좌→상부 L/R=3.69, 우→0.17 반전; 고정코너 모노 late FDN 불가 → early 격리). xruns=0. **code-reviewer APPROVE**, MEDIUM 1(early ring reset) 즉시 반영.
- 2026-06-04: **⑥e-1 uniform-diffuse 블렌드 완료 ✅** (커밋 c5232c2, 푸시됨) — `iae::blendVbapWithUniformDiffuse`(RoomEngine.cpp:113-165 충실, participate 마스크 생략=전 스피커 spatial) → late cube-corner 게인(kLateDiffuseMin=0.64) + early width-spread 게인(kErDiffuseNonWfs=0.55)에 적용. VBAP를 uniform 1/√nSpk로 블렌드 후 pre-blend RMS 보존(scale clamp[0.4,2.5]). RT-safe(stack tmp[128], early는 per-reflection 호이스트). 단위(RMS=1.000, peak 0.944/0.737/0.445 단조, ≤1e-5 no-op). **양 빌드 130/130**, 룸 스모크 2종 directional 신호 유지(early L/R 1.44/0.66). code-reviewer APPROVE. WFS-fraction jmap(0.64..0.93)+per-obj 0.87은 ⑥e 후속.
- 2026-06-04: **⑥e-2 cluster 확산 코어 완료 ✅** (커밋 d190e0b, 푸시됨) — `iae::RoomCluster`(`core/src/render/ported/RoomCluster.{h,cpp}`), RoomEngine.cpp:605-647 byte-faithful: 16384탭 지연선 → 6탭 feedforward 삼각가중 {1,2,3,3,2,1}/12, geometric offset(charM=0.11·cbrt(V), d0=round(charM/343·sr·0.2) clamp[8,1200], step=max(2,d0/10)), clGain=(0.08+0.42·diff)·kClTri6Inv·0.5. **코어-우선 패턴**(⑥a/⑥c처럼): 버스 EQ·per-spk(opp-bias) 분배·OSC는 후속 결선 증분. 단위테스트가 **정확 FIR IR** 검증(탭 6개·1:2:3:3:2:1·등간격·clGain 일치·diffusion/volume 단조·reset 결정), d0=26 step=2 base=clGain=0.0117333@V630. charM 곱 전체 명시캐스트로 비트정합+-Wconversion clean. RT-safe(stack std::array). **양 빌드 131/131**, 회귀 0. code-reviewer APPROVE(LOW 1=charM cast 순서 → 비트정합 반영).
- 2026-06-04: **⑥e-3a absorption-EQ 비콰드 코어 완료 ✅** (커밋 4c7b52d, 푸시됨) — `iae::RoomBiquad`(`core/src/render/ported/RoomBiquad.{h,cpp}`), 레퍼런스 syncRoomEqCoeffsIfNeeded(RoomEngine.cpp:168-197)이 쓰는 absorption EQ 비콰드를 **벤더 JUCE 소스에 byte-faithful**: setLow/HighPass=`juce::dsp::IIR::Coefficients<float>::makeLow/HighPass`(Q form, juce_IIRFilter.cpp:75-117, LP (1-nSq)/HP (nSq-1) 부호+HP c1*-2), a0==1이라 5계수 {b0,b1,b2,a1,a2}=JUCE a0정규화값 비트동일(assignImpl :33-49). processSample=JUCE **TDF-II**(juce_IIRFilter_Impl.h:212-217, 피연산자순서/부호 동일). kPiF=MathConstants<float>::pi, Q=inverseRootTwo, float 전산(double 승격 0). snapToZero 생략=JUCE 단일샘플 processSample도 미적용(블록경로만)→충실. 코어-우선(per-obj/bus 인스턴스화·coeff 캐시·결선은 후속). 단위 `test_convergence_room_biquad` **비순환 2축**: 해석적 |H(e^jw)|(DC/Nyquist/−3.0103dB Butterworth 코너) 계수검증 + 사인 RMS 게인 TDF-II 토폴로지 교차검증. 측정 LP@10k DC=1.0/Nyq=0/코너−3.010dB(b0=0.220195,a1=−0.307566,a2=0.188345), HP@120 DC=0/Nyq=1.0. RT-safe. **양 빌드 132/132**, 회귀0, code-reviewer APPROVE(5축 byte-faithful, LOW 2 doc 반영). 레퍼런스 EQ 코너 기본값 earlyCluster HP=120/LP=10000, late HP=45/LP=16000, Q=1/√2.
- 2026-06-04: **⑥e-3b early predelay + per-obj absorption EQ 라이브 결선 완료 ✅** (커밋 a09c06e, 푸시됨) — renderRoomEarly에서 객체별 send-scaled 모노가 6 image ring 전에 **predelay(~20ms) + EQ(HP120→LP10000)** 통과: `xdel[n]=LP(HP(predelay(dry*send)))` 객체당 1회 계산해 6탭 공유, ring이 dry*send 대신 xdel 읽음. RoomEngine.cpp:374-406 byte-faithful(predelay 라인 :374-395 read-before-write+pds clamp[0,max-1] 단일분기 wrap, EQ HP→LP :397-406 via iae::RoomBiquad, 라인크기 :220-222 max=clamp(round(0.1·sr)+1,1,19200)/stride=max+MAX_BLOCK+8, mode-switch reset :260-268 대칭). RT-safe(xdel=stack float[MAX_BLOCK] 2KB, 버퍼는 prepare 할당, SPE_RT_NO_ALLOC_SCOPE). **실바이너리 스모크** `scripts/smoke_room_predelay_eq.py`: 객체 비활성화 edge에서 direct 즉시 끊김 vs early-asym 꼬리 = predelay+ring; upper-ring L/R 비대칭이 early 격리(late FDN=대칭 mono 무비대칭). **tail=56.0ms(3회 안정) ≫ ring ceiling 10.7ms ≪ FDN T60 → predelay 확정**, 방위추종 L/R=1.56, xruns=0. **양 빌드 132/132**, 회귀0, code-reviewer APPROVE(predelay math/EQ순서/RT-safety/reset/per-obj 격리 검증, MEDIUM 1 스모크견고성+LOW 2 반영: 윈도우 edge검출+wrap유효성 주석). EQ corner/predelay-ms는 기본값 고정(OSC=⑥e-4). ⚠ 단일분기 ring wrap은 pds<stride 전제 → ⑥e-4 OSC가 pds를 max-1로 clamp 유지해야 함.
- 2026-06-04: **⑥e late opp 소스바이어스 라이브 결선 완료 ✅** (커밋 4ace7ac, 푸시됨) — 후기 FDN 8 Hadamard 라인 게인을 **정적 cube-corner → 블록마다 소스에너지 반대축 동적 바이어스**. byte-faithful RoomEngine.cpp:535-583 / :491-503. per-object 루프(`SpatialEngine.cpp:917-968`)가 블록당 late 소스에너지 중심 누적: `late_w_sum += normalized(pos)*Σ|reverb_tap|`, `late_w_denom`, algo별 `wet_wfs/wet_nonwfs`(srcAxis pos식은 renderRoomEarly와 동일 프레임, 누적자=블록당 로컬=레퍼런스 reset 충실). room 분기(`:1049-1067`)가 `opp=normalized(normalized(avg)*-1)`(기본{0,1,0}) + `lateDiffuse=jmap(wfsFraction,0.64,0.93)` 계산 후 `computeLateFdnGains()`로 라인 게인 재계산. 신규 pure static `lateFdnLineDirection(k,opp)`: `u=cubeCorner(k)*0.5+opp*0.5` 정규화(kLateCornerTowardOpposite=0.5, corner 순서=cubeCornerDirection 일치) → az=atan2(x,z)/el=asin(y) → native vbap_gain_into → blend(lateDiffuse), ⑥b/⑥d와 동일 az/el 적응. prepare seed도 같은 `computeLateFdnGains(opp=+up,min)` 경로(DRY, control-seed/RT-recompute 단일경로). RT-safe(stack scratch, no-alloc under SPE_RT_NO_ALLOC_SCOPE). 적응(의도): computeSpatialVbap(3D)→vbap_gain_into(az,el); late 가중치=sum|reverb_tap|(단일 포트 send)=레퍼런스 wetMono 대용. **단위 = opp 스티어링 수식 직접 검증**(unit-length; opp=+up 전 라인 상승; opp=±x 전 라인 해당 반구로 스티어). 실바이너리 L/R 방향은 per-obj early(ceiling tap=소스 az)가 소스반대 late와 상충해 confound → 스모크는 RT/sanity. `smoke_room_reverb.py` 8/8 upper-ring, xruns=0, upperOn 0.702→0.672(바이어스 라이브 확인). **양 빌드 132/132**(build-128 flake=ambi_decoder_type_swap_concurrent, 무관·격리시 통과), 회귀0, code-reviewer APPROVE(byte-faithful 라인대조+RT-safety+프레임+결정성, LOW 3=의도적 적응/도달불가 edge).
- 2026-06-04: **⑥e-2-wire cluster(중간장) 확산 라이브 결선 완료 ✅** (커밋 22b129b, 푸시됨) — 포팅된 `iae::RoomCluster`(6탭 feedforward, ⑥e-2 코어)를 라이브 룸 경로에 결선. byte-faithful RoomEngine.cpp:414-419/:553-565/:594-647. renderRoomEarly가 공유 mono cluster 버스에 객체별 `xdel*cSend`(cSend=clamp(roomClusterSend01,0,1)·0.48) 누적(버스는 블록당 per-object 패스 상단서 0클리어). room 분기(FDN 분배+renderRoomEarly 후): cluster 버스 → absorption EQ(HP120→LP10000=per-obj early EQ와 동일 코너=ref cEcHp/cEcLp) → RoomCluster::process(6탭 라인) → opp-biased `clusterU=normalized{opp.x·0.93,opp.y·0.93+0.35·0.07,opp.z·0.93}` VBAP 게인(FDN과 동일 블록 lateDiffuse blend)으로 어레이 분배. opp/lateDiffuse=⑥e 블록값. cluster params=레퍼런스 기본값 고정(send01=0.4/diff=0.48/vol=630) until ⑥e-4 OSC. `room_cluster_send01_`=atomic<float>+`setRoomClusterSend01()` control-thread setter(RT-safe; ⑥e-4 OSC가 라우팅 예정; A/B 테스트가 사용). mode-switch INTO room이 cluster 라인+버스EQ reset(⑥b/⑥d/⑥e-3b 선례). 적응(의도): computeSpatialVbap(3D)→vbap_gain_into(az,el); cluster가 포트에선 FDN/early 후 실행(레퍼런스는 전)—둘 다 mix_buf_ 가산, 교환법칙; |outC|<1e-9 skip+per-spk add=포트 FDN/early 스타일. **단위 A/B**(cluster off vs on max send, reverb-only upper-ring; FDN/early 동일경로→델타=cluster 확정): phase-robust `Σ|Δspk|`=baseline의 10.5%(cluster-off upper=0.6716=⑥e pre-cluster 값과 정확 일치→send=0 경로 완전 비활성). **양 빌드 132/132**, smoke_room_reverb 8/8 xruns=0 + smoke_room_predelay_eq 회귀(tail56ms/L:R=2.02/xruns=0). code-reviewer APPROVE(1 MEDIUM=테스트마진→phase-robust 메트릭으로 수정, 2 LOW=⑥e-4 EQ코너 lockstep 메모+방어적 zero-fill, 비차단).
- 2026-06-05: **⑥e-4 /room/* OSC 컨트롤 네임스페이스 완료 ✅** (커밋 fc65d2c, 푸시됨) — 룸 엔진 외부 제어면. **하이브리드 스킴**(per-param granular + atomic bundle + enable)으로 mmhoa를 프리셋전용 콘솔(d&b/L-ISA) **추월** + SPAT급(granular+bundle) **동급** = superset. 와이어=**단일 선행태그(,ii seq/id 헤더 無)** → 디코더 payload_int_offset 트랩 미발동. 주소: `/room/enable ,i`(=/reverb/select room(2)/fdn(0) 별칭+tail reset), `/room/set ,f×13`(원샷 atomic 번들: t60 sx sy sz earlyW earlyBal clSend clDiff clVol eqHP eqLP hfCorner hfRatio), `/room/t60 ,f`(RoomFdn t60), `/room/size ,fff`(halfExtents), `/room/early/{width,balance} ,f`, `/room/cluster/{send,diffusion,volume} ,f`, `/room/eq/early ,ff`(HP/LP — **LOCKSTEP**), `/room/late/hf ,ff`(hfDecayCornerHz+Ratio01). **3대 불변식 검증완**: ①EQ lockstep=applyEqEarly가 cluster_eq_hp_/lp_ + 전 객체 er_eq_hp_/lp_[0..MAX_OBJECTS)를 한 패스로 동일 코너 재coeff(레퍼런스 syncRoomEqCoeffs 보장); ②RT-safety=모든 룸 변이가 **오디오스레드 FIFO drain(applyRoomCtl)**, 룸 렌더와 동일 스레드 → RoomFdn/RoomCluster setParams=inline 구조체저장(딜레이라인 prepare고정)·비콰드 재coeff=float연산 = **무할당**, /room/set=단일 FIFO엔트리=atomic, 교차스레드=2 relaxed atomic(블록당 1회 read); ③/room/preset·/room/eq/late=**의도적 보류**(preset-store/late-bus-EQ 인프라 필요)→Unknown 거부(no-op 아님). 구현: 단일 `CommandTag::RoomCtl`+op선택 `PayloadRoomCtl`, decode+encode 라운드트립, POD QueuedCmd, **MAX_ARGS 8→16**(13f 번들 수용, 파서 슬롯 전부 경계). `resetRoomState()` ReverbSelect drain서 추출(enable 공유). 엔진소유 shadow param 구조체(room_fdn_params_/room_cluster_params_)로 per-field 갱신 권위화. **predelay-ms 미노출**(early-ring 단일분기 wrap pds<stride 불변식 보존). RoomBiquad 내부 Nyquist clamp 추가(defense-in-depth; in-range 코너 불변→byte-faithful). 테스트: `test_osc_room_ctl_roundtrip`(전 op decode+encode→decode, 부분-/room/set·미지leaf 거부), `test_p_room_ctl_apply`(EQ-lockstep **계수동일성** 직접검증+독립재유도, RT no-alloc rt_alloc_violations()==0 on 130비콰드 재coeff·/room/set, t60/cluster/enable 거동, enable==/reverb/select **비트동일**), 실바이너리 `scripts/smoke_room_ctl.py`(/room/enable 룸 engage, /room/t60 5.0 tail 2.03×, /room/set 번들 clean, xruns=0). **양 빌드 134/134**, 회귀0, code-reviewer APPROVE(0 CRITICAL/HIGH; MEDIUM 비콰드 self-clamp+LOW 3 반영=halfExtents floor 0.1→0.5 정렬·enable ,i계약 주석).
- **현재 안정 지점**: VAP+VBAP3D+MDAP+WFS + **128 리프트** + **룸: 후기 FDN(⑥a/b)+opp바이어스(⑥e)+early(⑥c/d)+diffuse(⑥e-1)+early predelay/EQ(⑥e-3b)+cluster 결선(⑥e-2-wire) 라이브 + EQ비콰드 코어(⑥e-3a) + /room/* OSC 컨트롤(⑥e-4)** + 회귀 0 + WERROR=ON **양 빌드 134/134**. push됨 **main +40**.
- 2026-06-05: **⑥e-4-A late-bus EQ(Phase-5) + /room/eq/late 완료 ✅** (커밋 3ab8deb, 푸시됨) — xlsx 05.룸엔진 Phase-5 + 레퍼런스 SpatialSessionState.h가 **ER/Cluster/Late 3 흡음 EQ 버스** 요구하나 ⑥e-4는 early/cluster(lockstep)만—late 버스는 미인스턴스화였음. 추가로 메움. **DSP byte-faithful RoomEngine.cpp:650-658**: 모노 late send를 단일 HP→LP(late_eq_hp_→lp_, 기본 HP45/LP16000=kRoomLateHpfHz/Lpf)로 late_in_buf_에 필터 후 **room_fdn_.process() 입력**(raw send 대신). late FDN 꼬리만 셰이핑—early ring·cluster bus는 자기 un-late-EQ 탭 읽음(레퍼런스가 lateBusHp/Lp를 late 버스에만 적용하는 것과 일치). resetRoomState서 reset. OSC `/room/eq/late ,ff`(Op::EqLate)=**별도 필터쌍, early/cluster와 비-lockstep**(late 버스 고유 코너). `/room/set` 13→15 float 확장(eq_late_hp/lp append; /room/set이 직전 커밋이라 외부 소비자 없음). applyEqLate=clampHz Nyquist-safe. RT-safe(recoeff+필터=오디오스레드 float연산, late_in_buf_ prepare할당, rt_alloc_violations()==0). 테스트: roundtrip(/room/eq/late + set f×15 + 13f 거부), apply(late HP→fresh setHighPass(80) 일치·기본값서 변화·early/cluster lockstep 유지·late가 early/cluster와 **비융합** 독립). 스모크 `/room/eq/late HP=800`이 late FDN 꼬리 ~9.5% 감쇠(부분적=설계상 cluster/early가 late 버스 우회, 레퍼런스 충실), set f×15 clean, xruns=0. **양 빌드 134/134**, 회귀0, code-reviewer APPROVE(LOW 1 stale 주석 반영). **룸 파라미터 커버리지 13/22**(+late EQ).
- 2026-06-05: **⑥f 거리-게인 커브 이식 + OSC 완료 ✅** (커밋 e6bb1f9 ⑥f-1 코어 / 606c303 ⑥f-2 결선, 푸시됨) — 포트가 쓰던 간이 게인공식을 레퍼런스 near/far 거리모델로 교체(DSP 충실도 갭 메움). **⑥f-1 코어** `iae::RoomDistanceGain.h`: `normalizedRoomDistance01`+`roomDistanceGainDbLinear` byte-faithful AudioEngine.cpp:29-62(window clamp, exponent 1+(1-lin)·2, dB lerp, [0,1.5] clamp, decibelsToGain→pow). 단위=독립 재구현 대조(거리×linearity 그리드). **⑥f-2 결선**: ①early=객체별 predelay 입력에 earlyMul 곱(`pline=dry·send·earlyMul`)→6 ring+cluster 둘다 상속(레퍼런스 RoomEngine.cpp:386 충실). ②late=전용 `room_late_send_buf_`=Σ reverb_tap·lateMul, late EQ→FDN이 shared reverb_send_buf_ 대신 이걸 읽음(non-room FDN/IR은 unscaled 유지=레퍼런스 별도 lateBusInput). ⚠ lateSend 밸런스인자(0.52√…)는 의도적 제외(mmhoa earlyLateBalance 별도경로, 거리게인만 추가). OSC `/room/distance ,fff`·`/room/early/gain ,ff`·`/room/late/gain ,ff`(Op Distance/EarlyGain/LateGain). /room/set 15→**22 float**, MAX_ARGS 16→24. applyDistance clamp near[0.05,40]/far[0.1,120]∧≥near+0.1/lin[0,1]/gain[-48,12]=SpatialSessionState.cpp:386-398 일치. RT-safe(per-obj pow/clamp 무할당, buf prepare+블록클리어, 7 param=오디오스레드 전용 plain). 테스트=core 대조+roundtrip(3 op+set f×22+15f 거부)+apply(late/gain +6 vs −40dB=3.2× upper). 스모크 late/gain 5.8×, set f×22 clean, xruns=0(eq/late 에너지 단정은 거리게인이 late버스 축소→cluster/early 지배로 confound되어 제거, eq/late는 단위테스트가 권위). **양 빌드 135/135**, 회귀0, code-reviewer APPROVE(MEDIUM far 120m cap+LOW 3 stale-doc 반영). **룸 21/22 param**(잔여=roomEarlyPredelayMs만).
- 2026-06-05: **⑥g room early predelay 노출 완료 ✅ — 룸 param 22/22** (커밋 806eb87, 푸시됨) — renderRoomEarly 고정 kRoomEarlyPredelayMs→멤버 `room_early_predelay_ms_`(상수서 single-source). pds=min(er_predelay_max_-1, lround(ms·sr/1000)), stride=er_predelay_max_+MAX_BLOCK+8 → pds<stride **모든 predelay서 성립(상수 521샘플 마진, sr무관)**=단일분기 ring-wrap 전제 구조적 보장(이전 보류=과보수). OSC `/room/predelay ,f`(Op::Predelay), applyPredelay clamp[0,100]ms(pds clamp와 이중방어). /room/set 22→23 float. RT-safe(plain 멤버, 오디오스레드 drain write/render read 동일스레드, ring read in-bounds). **양 빌드 135/135**(build-128는 알려진 parallel-load flake ambi_decoder_type_swap_concurrent 제외, 격리 3/3). roundtrip(+predelay+set f×23+22f 거부)+smoke f×23 clean xruns=0. code-reviewer APPROVE(predelay 불변식 4 sub-claim 전부 검증, LOW 2 DRY/stale 반영). **룸 22/22 param=레퍼런스 룸 파라미터 집합 완전 superset.**
- 2026-06-05: **⑥h room state in scenes + /room/preset 명명 recall 완료 ✅ — 룸 superset 완결** (커밋 3079c52, 푸시됨) — 룸 설정을 이름으로 저장/recall. **SceneSnapshot**에 `RoomSnapshot`(23 param+enabled+present) 추가, toJson은 present일 때만 `"room":{}` emit(pre-room scene byte-compat), fromJson backward-compat(없으면 present=false→/room/preset no-op). **FIX**: objects 파싱 루프가 무경계(`find '{'`)였음→array `]`로 bound(trailing room block을 object로 오파싱 방지, legacy scene 테스트 136/136로 무회귀 입증). **snapshotRoom()**=live 룸 상태 capture(멤버+atomic+active_reverb_==2). EQ 코너 4개(이전 biquad coeff에만 있어 unreadable)→scalar 멤버(room_eq_early/late_hp/lp_) applyEqEarly/Late서 기록. **SceneController** RoomStateProvider(대칭) SceneSave서 capture. **CommandTag::RoomPreset**+PayloadRoomPreset{string}, `/room/preset ,s` decode(빈이름 거부)/encode, inbound 메일박스 라우팅(isCueOrSceneTag). **bin** drain switch가 scene 로드→room block 있으면 RoomCtl SetAll+Enable 2× dispatchCommand 적용(objects 미변경=룸 프리셋, 미지명/no-room=no-op). 스레딩=control loop capture/apply→lock-free FIFO→audio applyRoomCtl(RT위반·할당 0). 테스트=room JSON 라운드트립(23+enabled)+backward compat+/room/preset decode/encode+live snapshotRoom+full save→load loop. 실바이너리 `smoke_room_preset.py`: /scene/save가 room block capture, /room/preset이 short-T60 엔진에 long T60 recall→upper 1.60×. **양 빌드 136/136**, 회귀0, code-reviewer APPROVE(MEDIUM null-backend smoke-xrun flake→tolerant check, LOW 2 반영).
- **⑥ 룸 마감 = 완료 ✅ (superset 완결)**: 레퍼런스 22 룸 param 전부 개별 OSC + `/room/set` atomic 번들 + `/room/preset` 명명 recall(scene 통합). per-param granular + atomic bundle + named preset = d&b/L-ISA(프리셋전용) 추월 + SPAT급(granular+snapshot) 동급 + DSP 충실도(거리모델·ER/Cluster/Late EQ 삼총사) → **레퍼런스 룸 superset 달성**.
- 2026-06-06: **⑦ 스피커 디코릴레이션 완료 ✅** (커밋 ca2448d ⑦-1 코어 / 545ded5 ⑦-2 결선, 푸시됨) — IACC 감소로 공간 확산 개선(xlsx 07.디코릴레이션). **⑦-1 코어** `iae::SpeakerDecorrelationBank`(`core/src/render/ported/SpeakerDecorrelation.{h,cpp}`) byte-faithful: per-channel Schroeder 1차 allpass 캐스케이드(1–8단)+결정론적 per-speaker 마이크로딜레이(Murmur hash→frac→delay clamp[1,4094])+에너지보존 dry/wet(dry=√(1−mix²),wet=mix)+cfgHash lazy reconfig. **유일 적응**: per-channel delay store array→vector(128ch×4096 inline=~2MB 스택오버플로 위험; ring math 동일=DSP 비트충실). 단위=impulse[0]=dryAmt·energy(mix=1)=1.000000·per-speaker 결정성·reset·stages 효과. **⑦-2 결선**: decorr_bank_ + 6 plain param(enabled/mix/spread/ap/stages/seed), deinterleave 후(스피커 gain+delay 후, noise 전, 레퍼런스 AudioEngine.cpp:937-954 충실) per-channel in-place 적용. OSC `CommandTag::DecorrCtl`+PayloadDecorrCtl, `/decorr/{enable ,i|set ,fffiii|mix ,f|spread ,f|ap ,f|stages ,i|seed ,i}`. set 번들=혼합타입(decoder가 int/float 별도 배열→tag 순서 무관, interleaved ,ififif 동일 decode=테스트로 락인), seed int32→uint32 정확, /set은 enabled 권위(Command.h 주석). RT-safe(plain 멤버 audio-thread, processChannel 무할당, clamp=코어+레퍼런스 일치). 테스트 `test_p_decorr_ctl`(decode/encode 전 op+interleaved+reject, live apply, rt_alloc 0, bus 68.5% 변화), 실바이너리 `smoke_decorr.py`(/decorr/set이 인접채널 |corr| 1.000→0.552 Δ0.448, xruns=0; tone는 phase offset로 제한, broadband 증명은 단위테스트). **양 빌드 138/138**, 회귀0, code-reviewer APPROVE(MEDIUM /set-enabled foot-gun→doc, LOW interleaved 하드닝 반영).
- **다음 증분(순서)**: ⑧ 헤드트래킹/바이노럴 ⑨ ADM ⑩ per-obj EQ. xlsx 권위 룸계약=05.룸엔진+02.세션상태(SpatialSessionState.h 22 room param), per-param 룸 OSC 표는 xlsx에 **없음**(/room/*=mmhoa 고유, superset). ⑦ 디코릴 계약=07.디코릴레이션(kMaxStages=8, kDelayCapacity=4096).
  - **③' (분리·보류)**: ported `computeHorizontalVbap/Horizontal·SpatialMdap` std::vector→고정버퍼 no-alloc(ported-프레임 렌더러 채택 시 필요) — DoD §9 충족용.
- **후속 메모(증분 중 발견)**:
  - **(⑤ 발견) VAP 고도 라우팅 정합성 조사** — VAP가 2링 돔에서 +el 소스를 하부링으로 오라우팅. ported `computeVolumetricAmplitudePanning`의 고도/프레임 처리 또는 mmhoa→ported 어댑터 조합 의심. ⑧(바이노럴/헤드트래킹) 또는 별도 위생 증분서 추적.
  - (리뷰 LOW) cmake `{64,128}` 검증이 top+core 중복 → `spe_validate_pow2_cap()` 함수화 가능(MAX_OBJECTS도 동일 패턴이라 일관·비차단).
  - **(④ 발견) smoke_vap.py / smoke_mdap.py를 `/obj/algo` 4-int(`[seq,id,obj,algo]`)로 수정** — 현재 2-int는 algo가 VBAP로 폴백돼 VAP/MDAP를 실제로 검증하지 못함(right>left가 VBAP로도 성립해 spurious PASS). MDAP는 width 경로라 일부 유효하나 algo 자체는 미검증. WFS no-alloc 센티넬(아래)과 함께 후속.
  - no-alloc 센티넬(`test_p1_rt_no_alloc`)에 비정상 고도/width `/obj/move`+`/adm/obj/N/width` 케이스 추가 → 신규 VBAP3D 마스크·MDAP 빌드 경로를 `rt_alloc_violations` 센티넬로 직접 커버(현재는 스모크 xrun + 정적분석으로만 보증). WFS processBlock 경로도 동일 센티넬에 추가.
  - (리뷰 LOW) VBAPRenderer width 디스패치 2블록(주/폴백) 중복·매직넘버(1e-4f, 180/π) → private 헬퍼+명명 상수로 정리 가능(비차단; 폴백은 구조상 도달 불가).
