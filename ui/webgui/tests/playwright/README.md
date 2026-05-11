# Playwright FPS Harness — WGUI-S3 (G2 desktop)

Browser-driven test harness for **gate G2** (and, in later phases, G4) of
the plan at `.omc/plans/spatial-engine-webgui-v1.md`.

## What it gates

| Test | Gate | Threshold |
|---|---|---|
| `test_fps_desktop.py::test_fps_desktop_min_of_5_windows_p10_ge_60` | G2 desktop | `min(p10 of 5 × 5 s × 250 ms windows)` ≥ **60 fps** under 64-active-object + drag synth load |

Methodology (R2 plan §2 G2):

- Spawn `uvicorn ui.webgui.server:app` on a random loopback port (module-scoped fixture, `/health` polled).
- Headless chromium 1280×800 viewport, navigate to `/`.
- Inject 64 active objects via `page.evaluate` (no production code changes — plan §3 S3 Option B).
- Hold left mouse + circle around canvas centre at ~50 Hz for the entire measurement (drag synth concurrent with fps sampling).
- Sample `window.__fps` every 250 ms across 5 × 5 s windows → 100 samples / 20 per window.
- Per-window p10 = 2nd smallest sample (lower-bound).
- Assert `min(p10₀, …, p10₄) ≥ 60`.

## Install

```sh
# 1) Python deps (project root)
pip install -r requirements-dev.txt
# or, on Debian/Ubuntu PEP-668 systems:
pip install --break-system-packages -r requirements-dev.txt

# 2) Chromium binary for playwright
python -m playwright install chromium
# Optional, requires sudo on Debian/Ubuntu:
sudo python -m playwright install-deps chromium
```

If `install-deps` is unavailable (no sudo, restricted host), chromium may
still run headless on most distros. Required system libraries (manual
install path):

```sh
sudo apt-get install -y \
  libnss3 libnspr4 libatk1.0-0 libatk-bridge2.0-0 libcups2 \
  libxkbcommon0 libxcomposite1 libxdamage1 libxfixes3 libxrandr2 \
  libgbm1 libpango-1.0-0 libcairo2 libasound2
```

If `playwright` is not installed, the test module **skips** cleanly
(uses `pytest.importorskip`). If chromium is missing at launch time, the
single test inside also skips with a clear message — the rest of the
pytest suite is unaffected.

## Run

```sh
# Just G2 desktop
python3 -m pytest ui/webgui/tests/playwright/test_fps_desktop.py -v

# All playwright tests (future: G4 mobile / coord / multi-client)
python3 -m pytest ui/webgui/tests/playwright/ -v -m playwright

# Skip playwright (default for fast CI lane)
python3 -m pytest ui/webgui/tests/ -q --ignore=ui/webgui/tests/playwright
```

## Threshold override

For environments where headless chromium consistently underperforms wall
clock (CPU-throttled CI runners, slow VMs), the test honours an
environment variable for diagnostics:

```sh
G2_DESKTOP_THRESHOLD_FPS=50 python3 -m pytest ui/webgui/tests/playwright/test_fps_desktop.py -v
```

**Do not override in main CI** — the 60 fps gate is the contract.

## Files

```
ui/webgui/tests/playwright/
├── __init__.py                  # package marker + docstring
├── conftest.py                  # uvicorn_server fixture + marker registration
├── test_fps_desktop.py          # G2 desktop test
└── README.md                    # this file
```

## Phase plan link

Implementation: **WGUI-S3** in `.omc/plans/spatial-engine-webgui-v1.md`.
Follow-ups (later phases):

- **S4** — mobile emulation (iPhone 13 / Pixel 5) + coord precision (±2°) + 2-client concurrent (G4).
- **S6** — wire bytes hash + RTT triple path (independent of playwright).
