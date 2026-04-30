# Onboarding — spatial_engine

Target: **clone → build → run → modify a UI element ≤60 min** on a clean Ubuntu 22.04 box
(acceptance #10). Measured times live in `docs/onboarding_timing.md` (filed at P12).

## Prerequisites

- Ubuntu 22.04 LTS (or compatible Debian-based distro).
- `sudo` rights (apt installs JUCE Linux deps + clang-format + clang-tidy + yamllint).
- ~3 GB free for build + JUCE source.
- **Lab machine only** for P10 latency / P11 soak / P6 Dante I/O — see `docs/lab_setup.md`.

## One-shot bootstrap

```bash
git clone <repo> spatial_engine && cd spatial_engine
just bootstrap   # apt deps + JUCE submodule + uv sync + pre-commit
just build       # cmake + ninja (or Make fallback) → ./build/core/spatial_engine_core
just test        # ctest core_unit + pytest ui/tests
```

## Smoke after build

```bash
just run         # spawns core stub + UI stub; prints schema_version + ports
just sofa-inspect  # validates KEMAR SOFA at /home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa
```

## Ephemeral-port discovery (Risk #4)

The defaults are 9100 (cmd) / 9101 (state). To run multiple cores side-by-side:

```bash
./build/core/spatial_engine_core --osc-cmd-port 0    # OS picks; core prints chosen port to stderr
uv run python -m spatial_engine_ui --osc-cmd-port <printed-port>
```

## Lab gotchas

- **PREEMPT_RT vs commodity 6.x**: P0 pins one. See `docs/lab_setup.md` for the active choice.
- **PipeWire-JACK vs JACK1/JACK2**: PipeWire-JACK is the v0 baseline. If `pw-jack jack_lsp`
  doesn't list Dante channels, the Dante driver isn't loaded — see `docs/lab_setup.md`.
- **Dante PCIe driver**: Digigram ALP-Dante driver version pinned in `docs/lab_setup.md`.
  Mismatched kernel + driver = no Dante channels exposed; `just sofa-inspect` is unaffected
  (works on any machine).
- **P3.5 listener-blind smoke is non-skippable**: technique imported from vid2spatial archive,
  catches L/R inversions before P9/P12. Skipping it has cost a week of debugging in the past
  (see MEMORY.md sibling-repo notes).

## Three concrete UI-only modifications (acceptance #10 target)

These ship as worked examples in `docs/ui_modification_examples.md` (P12). At onboarding time,
new contributors can pick one to verify their toolchain end-to-end:

1. Add a per-object peak-meter widget next to the gain slider.
2. Change drag sensitivity / smoothing tau via a panel.
3. Recolor / reicon objects by category.

## When something is wrong

- Build fails missing JUCE dep → re-run `just bootstrap`; check `bootstrap.log`.
- Audio thread dropouts under steady load → check `cpu_pct_audio_thread` in `/sys/metrics`,
  cross-reference `docs/lab_setup.md` `cpufreq governor`.
- UI sees no `/sys/state` heartbeat → core probably exited; check `logs/spatial_engine_core.log`.
- `pre-commit` complains about clang-format on a header → ensure clang-format-18 is on PATH
  (`bootstrap.sh` installs the apt version; Conda Python's clang-format may differ).
