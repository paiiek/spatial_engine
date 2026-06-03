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
- **다음(D4 재평가 지점)**: ① Phase 1 정식 통합 — VAP를 `ipc/Command.h` Algorithm enum + AudioEngine 디스패치에 결선(라이브 선택 가능), MDAP/VBAP3D-5단레이어/WFS 전모드 이식, 2D-VBAP/MDAP `std::vector`→고정버퍼(no-alloc) ② Phase 0.5 128 리프트(`RenderingAlgorithm.h gains[64]`→128). PoC가 성공했으므로 통합 리스크 낮음.
