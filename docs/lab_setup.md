# Lab Setup — spatial_engine v0

**P0 first-class deliverable (A3)**. Pins kernel, distro, audio driver, governor, JACK config, SOFA file
metadata. Updated through P12. Deviation from these pins triggers `kernel-driver-deviation` /
`sofa-format-deviation` issues before downstream measurement phases (P6 / P9 / P10 / P11).

---

## 1. Operating system & kernel

| Field | Value | Source / Decision |
|-------|-------|-------------------|
| Distro | **Ubuntu 22.04 LTS** | Default disposition; revisit if Digigram's Linux compatibility matrix forces a different LTS |
| Kernel default disposition | **PREEMPT_RT** (linux-image-rt-amd64 or distribution-supplied RT kernel) | A3 — latency table shows commodity 6.x barely fits 5 ms p99 |
| Kernel fallback | Linux 6.x generic | If Digigram's certified driver matrix forecloses RT for the chosen ALP-Dante driver version |
| Specific kernel point version | **TBD — filled by P0 spike** | Confirm with Digigram support / `getdante.com/product/alp-dante/` Linux requirements |
| CPU governor | `cpufreq=performance` | Pinned via `cpupower frequency-set -g performance` at boot (systemd unit at P0 deliverable) |
| C-state policy | `intel_idle.max_cstate=1` (or AMD equivalent) | Lower C-state wake jitter |
| `tuned` profile | `latency-performance` | Default for all lab boots |

> **Open spike**: contact Digigram support to confirm certified ALSA-driver-version vs kernel-version
> matrix for ALP-Dante. If unknown after the spike, document the gap, proceed with the closest-feasible
> PREEMPT_RT-compatible kernel, and file `digigram-driver-matrix-confirmation` issue to be resolved
> before P6 Dante I/O verification.

## 2. Audio I/O — Digigram ALP-Dante PCIe (Linux)

| Field | Value | Notes |
|-------|-------|-------|
| Hardware | Digigram ALP-Dante PCIe (3U-rack target) | Spec §1, §11 |
| Driver | **Digigram-certified ALSA driver** — version TBD per P0 spike | Pinned per A3 |
| Mixer | PipeWire-JACK (default) | Spec R12 named JACK; PipeWire-JACK on Ubuntu 22.04 is JACK API-compatible |
| Period size | **64 frames @ 48 kHz** | 1.33 ms one-way buffer; matches latency budget Stage 4 |
| Sample rate | 48,000 Hz | Locked across SpeakerArray + BinauralMonitor + KEMAR SOFA |
| Channels | 4–8ch (configurable per `configs/lab_*.yaml`) | Spec §1 |
| JACK fallback | F2 — `juce::AudioIODeviceType::createAudioIODeviceType_ALSA()` | Selectable via `configs/default.yaml::audio.backend` |

## 3. KEMAR SOFA file (BinauralMonitor sub-budget — A2)

Inspected via `tools/sofa_inspector.py` against
`/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa` on 2026-04-30.

| Field | Value |
|-------|-------|
| Path | `/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa` |
| Size | 162.8 MB |
| Conventions | SOFA 2.0, `SimpleFreeFieldHRIR` 1.0, `DataType=FIR`, `RoomType=hemi-anechoic` |
| Title | KEMAR HRTF (RWTH Aachen, Braren & Fels) |
| License | CC-BY-4.0 |
| Date created | 2020-10-14 |
| **Sample rate** | **48,000 Hz** ✓ matches engine baseline |
| **IR length** | **384 samples (8.0 ms)** |
| Measurements | 64,800 (high-resolution grid) |
| Receivers | 2 (binaural; left + right ears) |

### A2 partition strategy decision (P9 input)

Given **384-sample IR @ 48 kHz**, BinauralMonitor uses `juce::dsp::Convolution` zero-latency
partitioned uniform-partition with:
- Head partition: 64 frames (matches block size; zero added latency)
- Tail partitions: one or two partitions of 128 / 256 frames covering the remaining 320 samples

Expected p99 add-on: **+1.33 to +5 ms** (per latency table). Asterisk on spec criterion #11 holds.

### Falsifier check (A2)

If P9 measurement reports BinauralMonitor end-to-end p99 > 12 ms on the lab target with this
KEMAR-actual IR length, switch to uniform-partition with zero-latency front + 64-frame predelay
on SpeakerArray to align both backends. Document in `docs/latency_budget.md`.

### Engine startup gate

`BinauralMonitor::initialize` MUST validate the actual file matches:
- `sample_rate == 48000.0` (else `IRSampleRateMismatch`)
- `receiver_count == 2` (else `IRChannelCountMismatch`)
- `ir_length_samples ∈ [128, 8192]` (else `IRLengthOutOfRange`)
- `Conventions == "SOFA"` and `SOFAConventions == "SimpleFreeFieldHRIR"`

## 4. Software toolchain pins

| Tool | Version | Source |
|------|---------|--------|
| CMake | ≥ 3.20 | `apt install cmake` (Ubuntu 22.04 ships 3.22) or `mamba install cmake` |
| Ninja | recommended | `apt install ninja-build`; Make fallback supported |
| GCC | 11.x or 13.x | C++20 baseline |
| Clang | ≥ 15 | optional toolchain |
| JUCE | **7.0.12** (pinned submodule) | `core/JUCE/` git submodule |
| uv (Astral) | latest | `curl -LsSf https://astral.sh/uv/install.sh \| sh` |
| Python | 3.11+ | `uv` manages via `pyproject.toml` |
| pre-commit | ≥ 3.7 | `pipx install pre-commit` |
| clang-format | 18.x | apt or mirror; rev pinned in `.pre-commit-config.yaml` |

## 5. Contributor reminders

- v0 is **GPL-3.0-or-later** (JUCE GPL clause); see `LICENSE.md` and
  `docs/license_procurement_plan.md` for the commercial-license trigger event (C5).
- PRs must be GPL-compatible.
- The audio thread is **alloc-free, lock-free, syscall-free, log-free** in steady state
  (Principle 1). `RT_ASSERT_NO_ALLOC` enabled in `build-test/` profile.

## 6. Verification log (append-only)

- **2026-04-30** — `tools/sofa_inspector.py` confirmed KEMAR @ 48 kHz / 384 samples / 8 ms /
  64,800 measurements / 2 receivers. A2 partition strategy parameters frozen above.
- **2026-04-30** — toolchain on dev machine: cmake (mamba), gcc 13.3.0, no ninja yet.
  Lab machine pins TBD pending Digigram driver matrix.
