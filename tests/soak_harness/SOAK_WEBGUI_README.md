# WebGUI 48h Soak Harness — G7 Governance

Plan: `.omc/plans/spatial-engine-webgui-v1.md` §3 S5 / §10 G7
Phase owner: build-runner (S5) — implementation + dry-run only.
**Real 48 h wall-clock run is user-queued.**

---

## Files

| File | Purpose |
|---|---|
| `run_soak_webgui.py` | Drive uvicorn + mock OSC sink + 60 Hz WS drag client + sampler + fault injector |
| `extract_asyncio_slope.py` | Sentinel — `asyncio.all_tasks()` slope (tasks/h) |
| `rss_slope.py` | Sentinel — RSS slope (MB/h), day-1→day-2 threshold derivation |
| `check_fd_leak.py` | Sentinel — fd count delta |
| `test_soak_webgui_schema.py` | CI schema + sentinel test (~9 s, runs an 8 s soak) |

`server.py` exposes `GET /api/_debug/asyncio_tasks` only when env
`SPE_DEBUG_ENDPOINTS=1`. The harness sets this automatically; the endpoint
is OFF in production.

---

## Quick check (dry-run, 10 min)

```bash
python3 tests/soak_harness/run_soak_webgui.py \
    --duration 600 \
    --reconnect-interval 300 \
    --sample-interval-hz 1 \
    --report-path /tmp/soak_webgui_dryrun.json \
    --dry-run

# Sentinels (all should exit 0):
python3 tests/soak_harness/extract_asyncio_slope.py \
    --report /tmp/soak_webgui_dryrun.json \
    --threshold-tasks-h 1.0 \
    --skip-warmup-s 60
python3 tests/soak_harness/rss_slope.py \
    --report /tmp/soak_webgui_dryrun.json \
    --threshold-mb-h 50 \
    --skip-warmup-s 60
python3 tests/soak_harness/check_fd_leak.py \
    --report /tmp/soak_webgui_dryrun.json \
    --threshold-fd 5
```

---

## Day-1 pilot (24 h)

```bash
# Run in tmux / nohup — 86400 s wall clock.
nohup python3 tests/soak_harness/run_soak_webgui.py \
    --duration 86400 \
    --reconnect-interval 300 \
    --sample-interval-hz 1 \
    --report-path /tmp/soak_webgui_day1.json \
    > /tmp/soak_webgui_day1.log 2>&1 &

# When complete, derive day-2 RSS threshold:
python3 tests/soak_harness/rss_slope.py \
    --report /tmp/soak_webgui_day1.json \
    --derive-day2-threshold \
    --skip-warmup-s 60
# → prints e.g. "day2_threshold_mb_per_h=15.23 (formula=min(max(day1_slope*3,1),50.0))"

# Day-1 acceptance probe (lax — measurement only):
python3 tests/soak_harness/rss_slope.py \
    --report /tmp/soak_webgui_day1.json \
    --print-median --skip-warmup-s 60
python3 tests/soak_harness/extract_asyncio_slope.py \
    --report /tmp/soak_webgui_day1.json \
    --print-median --skip-warmup-s 60
python3 tests/soak_harness/check_fd_leak.py \
    --report /tmp/soak_webgui_day1.json \
    --threshold-fd 5
```

Formula recap (plan §10 G7): day-2 threshold = `min(max(day1_slope × 3, 1), 50)` MB/h.

* Floor 1 MB/h prevents a flat day-1 from yielding a degenerate near-zero threshold.
* Hard cap 50 MB/h is the plan's "보수 상한" — day-2 never permits > 50 regardless of how noisy day-1 was.

---

## Day-2 (24 h, fixed threshold)

```bash
DAY2_THRESHOLD=$(python3 tests/soak_harness/rss_slope.py \
    --report /tmp/soak_webgui_day1.json \
    --derive-day2-threshold --skip-warmup-s 60 \
    | awk -F'=' '{split($2,a," "); print a[1]}')

nohup python3 tests/soak_harness/run_soak_webgui.py \
    --duration 86400 \
    --reconnect-interval 300 \
    --sample-interval-hz 1 \
    --report-path /tmp/soak_webgui_day2.json \
    --threshold-rss-mb-h $DAY2_THRESHOLD \
    > /tmp/soak_webgui_day2.log 2>&1 &

# After 24h — G7 final acceptance (all 4 must exit 0):
python3 tests/soak_harness/rss_slope.py \
    --report /tmp/soak_webgui_day2.json \
    --threshold-mb-h $DAY2_THRESHOLD --skip-warmup-s 60
python3 tests/soak_harness/extract_asyncio_slope.py \
    --report /tmp/soak_webgui_day2.json \
    --threshold-tasks-h 1.0 --skip-warmup-s 60
python3 tests/soak_harness/check_fd_leak.py \
    --report /tmp/soak_webgui_day2.json \
    --threshold-fd 5

# WS reconnect counter — sanity:
python3 -c "
import json,sys
d=json.load(open('/tmp/soak_webgui_day2.json'))
n=d['ws_counters']['reconnects']
print(f'reconnects={n} (plan threshold: <1000)')
sys.exit(0 if n < 1000 else 1)
"
```

---

## Single 48 h run (alternative to day-1/day-2 split)

If you trust the day-1 threshold derivation from prior production telemetry, run a single 48 h pass instead of two 24 h passes:

```bash
nohup python3 tests/soak_harness/run_soak_webgui.py \
    --duration 172800 \
    --reconnect-interval 300 \
    --sample-interval-hz 1 \
    --report-path /tmp/soak_webgui_48h.json \
    > /tmp/soak_webgui_48h.log 2>&1 &
```

The plan prefers the 24+24 split because it lets day-1 calibrate the threshold to the actual host's noise floor. Single-pass mode is documented only as an escape.

---

## Acceptance (plan §10 G7)

| Sentinel | Threshold |
|---|---|
| RSS slope | ≤ `derive_day2_threshold(day1)` MB/h, hard cap 50 |
| asyncio slope | ≤ 1 task/h |
| fd delta | ≤ 5 |
| WS reconnect count | < 1000 |

All four must pass for G7 to be green.

---

## Schema

Every report JSON contains:

```
{
  "schema_version": 1,
  "domain": "webgui",
  "duration_s": int,
  "reconnect_interval_s": int,
  "sample_interval_hz": int,
  "timestamp_utc_start": str,
  "timestamp_utc_end": str,
  "duration_actual_s": float,
  "rss_series":    [{"t": float, "rss_mb": float}, ...],
  "fd_series":     [{"t": float, "fd": int}, ...],
  "asyncio_series":[{"t": float, "n_tasks": int}, ...],
  "fault_injections": int,
  "ws_counters": {"sent": int, "received": int, "reconnects": int, "last_error": ...},
  "osc_sink_recv_count": int,
  "dry_run": bool,
  ...
}
```

Schema is enforced by `tests/soak_harness/test_soak_webgui_schema.py` (14 tests, ~9 s).

---

## Operational notes

* Harness sets `_bridge_mode=low_latency` after each uvicorn (re)start so the WebGUI is the 9100 producer in the soak. Production default `ai` mode suppresses obj_pos per ADR 0013.
* OSC sink binds UDP 9100 BEFORE uvicorn starts so `osc_bridge.SimpleUDPClient` never sees ICMP-unreachable.
* SIGKILL fault-injection picks a new free port on restart; WS client reconnect loop adapts via `port_ref`.
* On fault injection the fd-count series will discontinuously jump (new PID baseline). For final acceptance, prefer a soak window without a kill in the last 60 s.
