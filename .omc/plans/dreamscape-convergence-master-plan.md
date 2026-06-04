# Dreamscape Convergence — 마스터 플랜

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
- **현재 안정 지점**: VAP + VBAP3D + MDAP + WFS 라이브 + **128 스피커 리프트** + **FDN 코어(⑥a)** + 전체 회귀 0 + WERROR=ON **양 빌드(64/128) 127/127**. 브랜치 `feat/dreamscape-convergence`.
- **다음 증분(순서)**: **⑥b FDN 공간분배+라이브결선+스모크** → ⑥c Shoebox early+cluster → ⑦ 디코릴레이션 ⑧ 헤드트래킹/바이노럴 ⑨ ADM 확장 ⑩ per-object EQ·딜레이. **③' (분리·보류)**: ported `computeHorizontalVbap/Horizontal·SpatialMdap` std::vector→고정버퍼 no-alloc(ported-프레임 렌더러 채택 시 필요) — DoD §9 충족용.
- **후속 메모(증분 중 발견)**:
  - **(⑤ 발견) VAP 고도 라우팅 정합성 조사** — VAP가 2링 돔에서 +el 소스를 하부링으로 오라우팅. ported `computeVolumetricAmplitudePanning`의 고도/프레임 처리 또는 mmhoa→ported 어댑터 조합 의심. ⑧(바이노럴/헤드트래킹) 또는 별도 위생 증분서 추적.
  - (리뷰 LOW) cmake `{64,128}` 검증이 top+core 중복 → `spe_validate_pow2_cap()` 함수화 가능(MAX_OBJECTS도 동일 패턴이라 일관·비차단).
  - **(④ 발견) smoke_vap.py / smoke_mdap.py를 `/obj/algo` 4-int(`[seq,id,obj,algo]`)로 수정** — 현재 2-int는 algo가 VBAP로 폴백돼 VAP/MDAP를 실제로 검증하지 못함(right>left가 VBAP로도 성립해 spurious PASS). MDAP는 width 경로라 일부 유효하나 algo 자체는 미검증. WFS no-alloc 센티넬(아래)과 함께 후속.
  - no-alloc 센티넬(`test_p1_rt_no_alloc`)에 비정상 고도/width `/obj/move`+`/adm/obj/N/width` 케이스 추가 → 신규 VBAP3D 마스크·MDAP 빌드 경로를 `rt_alloc_violations` 센티넬로 직접 커버(현재는 스모크 xrun + 정적분석으로만 보증). WFS processBlock 경로도 동일 센티넬에 추가.
  - (리뷰 LOW) VBAPRenderer width 디스패치 2블록(주/폴백) 중복·매직넘버(1e-4f, 180/π) → private 헬퍼+명명 상수로 정리 가능(비차단; 폴백은 구조상 도달 불가).
