# v0.3.0 — DAW Hands-on Validation Log (S8)

> **Status**: PENDING — 사용자 실측 큐. 하드웨어(Reaper/Bitwig + 콘솔/`oscchief`) 필요.
> 본 체크리스트는 `.omc/plans/spatial-engine-v0.3.md` §3 S8 spec을 그대로 옮긴 실측 기록 양식이다.
> 코드/검증 완료 항목 (S1~S7): commit 6f4db50 (Phase 4 validation followups) 까지 모두 통과.
> 본 S8만 남았고, 자율 실행 불가 (실 DAW + 오디오 IO 필요).

## Build prerequisites

```bash
cd core/build
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON \
         -DSPATIAL_ENGINE_VST3=ON \
         -DSPATIAL_ENGINE_VST3_OSC=ON \
         -DSPATIAL_ENGINE_RT_ASSERTS=ON
make -j$(nproc)
# 산출물: vst3/spatial_engine_vst3.so
```

VST3 디렉토리에 심볼릭 링크:
```bash
mkdir -p ~/.vst3/spatial_engine_vst3.vst3/Contents/x86_64-linux
ln -sf $(pwd)/vst3/spatial_engine_vst3.so \
  ~/.vst3/spatial_engine_vst3.vst3/Contents/x86_64-linux/spatial_engine_vst3.so
```

---

## Per-host checklist

### Host 1: Reaper 7.x (Linux)

| # | Step | Y/N | Notes |
|---|------|-----|-------|
| 1 | `spatial_engine_vst3.so` 가 stereo track에 로드됨 | ☐ | |
| 2 | 8 params 모두 DAW UI에 노출: kPanAz, kPanEl, kSourceWidth, kMasterGain, kAmbiOrder, kRoomPreset, kBypass, **kMute** | ☐ | `docs/release/v0.3.0/reaper-8params.png` 첨부 |
| 3 | kPanAz를 4 bar @ 120 BPM 자동화 → 엔진 출력 반영 | ☐ | |
| 4 | kMute toggle → 무음 (kBypass와 구분: bypass는 dry pass-through) | ☐ | `docs/release/v0.3.0/reaper-kmute.wav` 첨부 (RMS < -120 dBFS 검증) |
| 5 | Save/close/reopen → 8 params 모두 state v3 reader로 복원 | ☐ | |
| 6 | v0.2 프로젝트 (state v2) 로드 → 3-way fork reader 동작, kMute 기본 0 | ☐ | |
| 7 | Direct reverse-path: standalone (:9100) 띄우고 `oscchief --port 9100 --send "/adm/obj/0/azim 90.0"` → DAW automation lane에 ≤50ms 내 반영 | ☐ | A.9 target |
| 8 | kBypass=on + kMute=off → dry pass-through (audible) ; kMute=on → 무음 (bypass 무관) | ☐ | |

**자유 형식 메모**:
- crashes 발견 횟수: __
- stuck params: __
- audio glitches @ block 512: __

---

### Host 2: Bitwig 5.x (Linux)

| # | Step | Y/N | Notes |
|---|------|-----|-------|
| 1 | `spatial_engine_vst3.so` 가 stereo track에 로드됨 | ☐ | |
| 2 | 8 params 모두 DAW UI에 노출 | ☐ | `docs/release/v0.3.0/bitwig-8params.png` 첨부 |
| 3 | kPanAz 4 bar @ 120 BPM 자동화 | ☐ | |
| 4 | kMute toggle → 무음 | ☐ | `docs/release/v0.3.0/bitwig-kmute.wav` 첨부 |
| 5 | Save/close/reopen 복원 | ☐ | |
| 6 | v0.2 (state v2) 호환 | ☐ | |
| 7 | Direct reverse-path ≤50ms | ☐ | |
| 8 | kBypass+kMute 분리 | ☐ | |

**자유 형식 메모**:
- crashes: __ / stuck: __ / glitches: __

---

## Acceptance gate (A.12)

본 sprint 완료 조건 (`.omc/plans/spatial-engine-v0.3.md` §3 S8):

- [ ] **2 hosts × 1 screenshot** = 2 screenshots (8 params UI)
- [ ] **2 hosts × 1 WAV** = 2 WAVs (kMute audible distinction)
- [ ] **2 hosts × 8 Y/N** = 16 checklist entries
- [ ] All 0 crashes, 0 stuck params, 0 audio glitches at block size 512
- [ ] State save/load round-trip OK (state v3)
- [ ] v0.2 (state v2) project loads cleanly, kMute defaults to 0

## Falsifiable per-param table

| Param | Pass criterion |
|-------|---------------|
| kPanAz, kPanEl, kSourceWidth, kMasterGain, kAmbiOrder, kRoomPreset, kBypass | inherits v0.2 verified criteria (§R3) |
| **kMute** | kMute=on → recorded WAV RMS < -120 dBFS (digital silence); kMute=off → audible audio matches non-muted reference. Distinct from kBypass dry pass-through. |
| **Direct reverse path** | `/adm/obj/0/azim 90.0` → DAW lane reflects within ≤50ms (A.9); DAW save captures this value. |

## References

- spec: `.omc/plans/spatial-engine-v0.3.md` §3 S8 (L368–402)
- kMute integration: `vst3/SpatialEngineProcessor.cpp:421-447` (bypass dry pass-through, must remain distinct)
- state v3 reader: `vst3/SpatialEngineProcessor.cpp:201-323` (S2.5)
- state v3 writer: commit `72f9cbb feat(C4-S7): state v3 writer + kMute=7 activation`
- Phase 4 validation followups: commit `f6dde86`, `6f4db50`

## Completion procedure

본 체크리스트 완료 시:
1. screenshots/WAVs를 `docs/release/v0.3.0/` 에 첨부
2. 각 Y/N 칸 ☐ → ☑ 또는 ☒
3. commit: `docs(v0.3.0-release): S8 DAW hands-on log — Reaper+Bitwig PASS/FAIL`
4. v0.3.0 release tag는 사용자 명시 요청 시만 (프로젝트 CLAUDE.md 방침)
