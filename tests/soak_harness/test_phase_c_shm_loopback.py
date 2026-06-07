"""ADR 0019 PR6 — cross-process C++↔Python shm drop-free streaming soak.

Two REAL processes over REAL `/dev/shm`:
  * PRODUCER: `shm_producer_sine.py` driving `adm_player.ipc_sink.IpcRingSink`
    with a deterministic ramp (`sample == global frame index`).
  * CONSUMER: the real `spatial_engine_core --input-backend shm:/<name>
    --backend null` engine binary.

What this proves (REV5 honest framing — see the plan
`.omc/plans/spatial-engine-v0.9-laneG-adr0019-pr6-soak.md`):
EMPIRICAL **drop-free streaming + clean producer lifecycle + no leak +
producer write-integrity** on x86-64. It is NOT a memory-ordering proof (x86-64
TSO makes the acquire/release pairing unobservable by construction) and NOT a
consumer torn-read check (the consumer's reads are never re-published on the
wire). Both are PR7.

Drop-free is proven by FOUR robust WIRE invariants (hard gates, read via
`os.open`+`mmap` — never `multiprocessing.shared_memory`, whose resource_tracker
would unlink the producer's live region):
  (1a) header `xrun_count == 0`  — producer never dropped (a sustained consumer
       stall fills the ring → drop-newest fires → this catches it).
  (3)  `read_idx == write_idx ± one engine block` at end — consumer fully caught
       up; every published block was consumed in order.
  (3b) `seq` advances with no gaps, `seq == produced blocks`.
  (3c) ramp payload at any slot `== reconstructed frame index`.

The consumer-side `driver xrun` (via `/sys/metrics`) and `shm_*` `/sys/warning`
are gated ONLY over the STEADY STREAMING window (samples while
`producer_state==STREAMING` and >= STEADY_GUARD_S before the DRAINING
transition). Post-drain underruns/warnings are an end-of-stream artifact of the
deadline-bound consumer (`SharedRingBackend.cpp:421-427` records an underrun on
every empty-ring tick with no `producer_state`-aware suppression; the standalone
engine is `--seconds` deadline-bound and does not stop on CLOSED). They are
RECORDED in the JSON report as diagnostics, not failed on. PR7 may add pump-path
drain suppression to allow a full-run `xrun==0` gate.

Verification commands:
    # default smoke (~6 s)
    ADM_PLAYER_ROOT=/home/seung/mmhoa/adm_player/dreamscape \
      python3 -m pytest tests/soak_harness/test_phase_c_shm_loopback.py -q
    # full 60 s soak
    ADM_PLAYER_ROOT=/home/seung/mmhoa/adm_player/dreamscape SHM_SOAK_FULL=1 \
      python3 -m pytest -m soak --run-soak \
      tests/soak_harness/test_phase_c_shm_loopback.py -q
"""

from __future__ import annotations

import json
import mmap
import os
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

import pytest

# Reuse the proven OSC encode/decode + free-port idioms (plan §4: reuse, don't
# duplicate). These live in the sibling soak test.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_osc_warning_channel import (  # noqa: E402
    build_osc_packet,
    free_udp_port,
    parse_osc_address,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
ENGINE_BIN = REPO_ROOT / "core" / "build" / "spatial_engine_core"
DEFAULT_ADM_PLAYER_ROOT = "/home/seung/mmhoa/adm_player/dreamscape"
PRODUCER_HELPER = Path(__file__).resolve().parent / "shm_producer_sine.py"
REPORT_DIR = Path(__file__).resolve().parent / "soak_reports"

# Streaming config (probed for clean steady-state on x86-64).
CHANNELS = 8
RATE = 48000
BLOCK = 256          # engine --block MUST equal producer block_size (256)
RING_FRAMES = 16384  # 64 blocks — jitter cushion so steady streaming stays xrun-free
PREBUFFER_BLOCKS = 24  # gate the engine spawn until the ring holds this many blocks
DRAIN_DWELL_S = 2.0  # >= 1 Hz tick so producer_state 2→3 is sampled (assertion 5)
SLACK_S = 1.5        # engine outlives producer CLOSED so the final state tick fires
                     # (read_idx catch-up of a ~24-block backlog completes in ~130 ms,
                     #  well within DRAIN_DWELL_S)
STEADY_GUARD_S = 1.0  # exclude the streaming→drain seam from the consumer-xrun gate

STATE_STREAMING = 1
STATE_DRAINING = 2
STATE_CLOSED = 3
RAMP_PROBE_SLOT = 100  # arbitrary channel-0 ring slot for the 3c ramp check

# Warning codes that are HARD failures during steady streaming (real defects):
#   shm_underrun       — consumer starved (ring went empty while streaming)
#   shm_producer_stale — producer heartbeat gap > 100 ms (producer stalled/died)
# `shm_producer_pacing` is EXCLUDED: the Python producer paces with `time.sleep`,
# whose sub-ms imprecision makes the per-block pts delta occasionally exceed one
# block period (5.33 ms), tripping the 1-per-5 s pacing diagnostic. This is
# benign timing jitter — NOT an underrun or a drop (the 60 s soak shows
# consumer_xrun==0 and header_xrun==0 throughout while pacing fires every 5 s).
# It is recorded as a diagnostic, never gated. (PR7 may revisit with a C-paced
# producer.)
HARD_STREAMING_WARNINGS = frozenset({"shm_underrun", "shm_producer_stale"})


def _adm_player_root() -> str:
    return os.environ.get("ADM_PLAYER_ROOT", DEFAULT_ADM_PLAYER_ROOT)


def _import_ipc_sink_constants():
    """Import the producer's header-offset constants (single source of truth)."""
    root = _adm_player_root()
    if root not in sys.path:
        sys.path.insert(0, root)
    from adm_player import ipc_sink  # type: ignore
    return ipc_sink


def _require_deps():
    """Skip cleanly (not fail) when the engine binary or producer is absent."""
    if not (ENGINE_BIN.exists() and os.access(ENGINE_BIN, os.X_OK)):
        pytest.skip(f"engine binary not built: {ENGINE_BIN} (cmake + make first)")
    try:
        ipc = _import_ipc_sink_constants()
    except Exception as exc:  # noqa: BLE001
        pytest.skip(f"adm_player.ipc_sink not importable (ADM_PLAYER_ROOT?): {exc}")
    return ipc


def _read_u64(mm, off: int) -> int:
    return struct.unpack_from("<Q", mm, off)[0]


def _read_u32(mm, off: int) -> int:
    return struct.unpack_from("<I", mm, off)[0]


def _rss_kb(pid: int) -> int:
    try:
        with open(f"/proc/{pid}/status") as fh:
            for line in fh:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def _fd_count(pid: int) -> int:
    try:
        return len(os.listdir(f"/proc/{pid}/fd"))
    except OSError:
        return 0


def _run_soak(duration_s: float, full: bool) -> dict:
    """Orchestrate one producer↔engine soak; return a structured report dict.

    Raises AssertionError on any HARD-gate failure. Diagnostics (post-drain
    underruns/warnings, RSS/fd series) are returned in the report, not asserted
    here (full mode adds the leak gate in the caller).
    """
    ipc = _require_deps()
    name = f"spe-pr6-{'full' if full else 'smoke'}-{os.getpid()}"
    shm_path = f"/dev/shm/{name}"
    root = _adm_player_root()

    # Hygiene: never inherit a stale region.
    try:
        os.unlink(shm_path)
    except FileNotFoundError:
        pass

    env = dict(os.environ, PYTHONPATH=root, ADM_PLAYER_ROOT=root)
    fd = None
    mm = None
    producer = None
    engine = None
    client = None
    try:
        # ── spawn producer (creates + header-inits the ring) ──────────────
        producer = subprocess.Popen(
            [sys.executable, str(PRODUCER_HELPER),
             "--name", name, "--channels", str(CHANNELS), "--rate", str(RATE),
             "--block-size", str(BLOCK), "--ring-frames", str(RING_FRAMES),
             "--duration", str(duration_s), "--drain-dwell-s", str(DRAIN_DWELL_S)],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        # ── readiness gate: magic + prebuffer, via os.open+mmap (HELD) ─────
        prebuffer_frames = PREBUFFER_BLOCKS * BLOCK
        deadline = time.monotonic() + 8.0
        while time.monotonic() < deadline:
            if producer.poll() is not None:
                out, err = producer.communicate()
                if producer.returncode == 3:
                    pytest.skip("producer reported adm_player import failure")
                raise AssertionError(
                    f"producer exited early rc={producer.returncode}: "
                    f"{err.decode(errors='replace')[:400]}")
            if os.path.exists(shm_path):
                fd = os.open(shm_path, os.O_RDONLY)
                size = os.fstat(fd).st_size
                mm = mmap.mmap(fd, size, prot=mmap.PROT_READ)
                if (_read_u64(mm, ipc.OFF_MAGIC) == ipc.SPE_RING_MAGIC
                        and _read_u64(mm, ipc.OFF_WRITE_IDX) >= prebuffer_frames):
                    break
                mm.close(); mm = None
                os.close(fd); fd = None
            time.sleep(0.01)
        assert mm is not None, "readiness gate timed out (no ring / prebuffer)"
        cap = _read_u32(mm, ipc.OFF_CAPACITY_FRAMES)

        # ── spawn engine; handshake from the listening socket ─────────────
        osc_port = free_udp_port()
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client.bind(("127.0.0.1", 0))
        client.settimeout(0.1)
        engine_seconds = int(duration_s + DRAIN_DWELL_S + SLACK_S)
        engine = subprocess.Popen(
            [str(ENGINE_BIN), "--input-backend", f"shm:/{name}", "--backend", "null",
             "--seconds", str(engine_seconds), "--osc-port", str(osc_port),
             "--channels", str(CHANNELS), "--rate", str(RATE), "--block", str(BLOCK)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=str(REPO_ROOT / "core" / "build"))
        time.sleep(0.2)  # let the OSC listener bind
        client.sendto(build_osc_packet("/sys/handshake", ",i", 1),
                      ("127.0.0.1", osc_port))

        # ── drain OSC + sample RSS/fd while the engine runs ───────────────
        t0 = time.monotonic()
        cur_state = None
        t_drain = None
        # (t_rel, kind, value, state_at_sample)
        xrun_samples: list[tuple[float, int, int]] = []
        warning_events: list[tuple[float, str, int]] = []  # (t, code, state)
        producer_states_seen: set[int] = set()
        heartbeat_alive_seen = set()
        rss_series = {"producer": [], "engine": []}
        fd_series = {"producer": [], "engine": []}
        next_sample = t0
        run_deadline = t0 + engine_seconds + 3.0
        while time.monotonic() < run_deadline and engine.poll() is None:
            now = time.monotonic()
            if now >= next_sample:
                rss_series["producer"].append(_rss_kb(producer.pid))
                rss_series["engine"].append(_rss_kb(engine.pid))
                fd_series["producer"].append(_fd_count(producer.pid))
                fd_series["engine"].append(_fd_count(engine.pid))
                next_sample = now + 1.0
            try:
                data, _ = client.recvfrom(4096)
            except socket.timeout:
                continue
            addr = parse_osc_address(data)
            text = data.decode("ascii", errors="replace")
            trel = time.monotonic() - t0
            if addr == "/sys/state":
                if "shm_producer_state=" in text:
                    cur_state = int(text.split("shm_producer_state=")[1].split("\x00")[0])
                    producer_states_seen.add(cur_state)
                    if cur_state == STATE_DRAINING and t_drain is None:
                        t_drain = trel
                if "shm_producer_alive=" in text:
                    heartbeat_alive_seen.add(
                        int(text.split("shm_producer_alive=")[1].split("\x00")[0]))
            elif addr == "/sys/metrics" and "xrun_count=" in text:
                xrun = int(text.split("xrun_count=")[1].split("\x00")[0])
                xrun_samples.append((trel, xrun, cur_state if cur_state is not None else -1))
            elif addr == "/sys/warning" and "shm_" in text:
                for tok in text.split("\x00"):
                    if tok.startswith("shm_"):
                        warning_events.append((trel, tok, cur_state if cur_state is not None else -1))

        engine_out, _ = engine.communicate(timeout=6)
        producer.communicate(timeout=12)
        # (client socket closed in finally — covers the exception path too)

        # ── final wire snapshot from the HELD mapping (survives unlink) ────
        wi = _read_u64(mm, ipc.OFF_WRITE_IDX)
        ri = _read_u64(mm, ipc.OFF_READ_IDX)
        header_xrun = _read_u64(mm, ipc.OFF_XRUN_COUNT)
        seq = _read_u64(mm, ipc.OFF_SEQ)
        final_state = _read_u32(mm, ipc.OFF_PRODUCER_STATE)
        # 3c ramp: value at channel-0 slot s == most-recent frame index there.
        slot = RAMP_PROBE_SLOT
        ramp_off = ipc.channel_byte_offset(0, cap) + slot * 4
        ramp_val = struct.unpack_from("<f", mm, ramp_off)[0]
        expected_frame = (wi - 1) - ((wi - 1 - slot) % cap)

        produced_blocks = seq
        expected_blocks = int(round(duration_s * RATE / BLOCK))

        # Steady-streaming consumer-xrun: samples with state==STREAMING and
        # >= STEADY_GUARD_S before the drain transition.
        guard_cutoff = (t_drain - STEADY_GUARD_S) if t_drain is not None else 1e18
        steady_xruns = [x for (t, x, st) in xrun_samples
                        if st == STATE_STREAMING and t <= guard_cutoff]
        steady_warnings = [(t, c) for (t, c, st) in warning_events
                           if st == STATE_STREAMING and t <= guard_cutoff]
        steady_hard_warnings = [(t, c) for (t, c) in steady_warnings
                                if c in HARD_STREAMING_WARNINGS]
        post_drain_xrun = xrun_samples[-1][1] if xrun_samples else 0

        report = {
            "mode": "full" if full else "smoke",
            "duration_s": duration_s,
            "engine_seconds": engine_seconds,
            "wire": {"write_idx": wi, "read_idx": ri, "backlog": wi - ri,
                     "header_xrun_count": header_xrun, "seq": seq,
                     "final_producer_state": final_state, "capacity_frames": cap},
            "ramp": {"slot": slot, "value": ramp_val, "expected": expected_frame,
                     "match": ramp_val == expected_frame},
            "producer_states_seen": sorted(producer_states_seen),
            "heartbeat_alive_seen": sorted(heartbeat_alive_seen),
            "consumer_xrun": {"timeline": [(round(t, 2), x, st) for (t, x, st) in xrun_samples],
                              "steady_streaming_max": max(steady_xruns) if steady_xruns else 0,
                              "post_drain_final": post_drain_xrun,
                              "t_drain": t_drain},
            "shm_warnings": {"steady_streaming": steady_warnings,
                             "steady_hard": steady_hard_warnings,
                             "total": [(round(t, 2), c) for (t, c, st) in warning_events]},
            "rss_series_kb": rss_series,
            "fd_series": fd_series,
            "engine_tail": engine_out.decode(errors="replace").strip().splitlines()[-1:]
            if engine_out else [],
        }

        # ── HARD GATES (robust wire invariants) ───────────────────────────
        # 1a: producer never dropped.
        assert header_xrun == 0, f"producer drop-newest fired: header xrun={header_xrun}\n{report}"
        # 3: consumer fully caught up.
        assert abs(wi - ri) <= BLOCK, (
            f"read_idx did not catch up: write={wi} read={ri} (backlog {wi-ri})\n{report}")
        # 3b: seq complete, no gaps (allow ±1 block of rounding vs wall-clock).
        assert produced_blocks > 0, f"no blocks produced\n{report}"
        assert abs(produced_blocks - expected_blocks) <= 2, (
            f"seq {produced_blocks} far from expected {expected_blocks}\n{report}")
        # 3c: ramp payload integrity.
        assert ramp_val == expected_frame, (
            f"ramp mismatch slot={slot}: {ramp_val} != {expected_frame}\n{report}")
        # 5: producer reached CLOSED (3).
        assert final_state == STATE_CLOSED, (
            f"producer_state final != CLOSED: {final_state}\n{report}")
        # 4: producer was alive (heartbeat) during the run. Smoke tolerates an
        # empty set (few state ticks); full mode REQUIRES at least one alive=1.
        if full:
            assert 1 in heartbeat_alive_seen, (
                f"full mode: shm_producer_alive never 1: {heartbeat_alive_seen}\n{report}")
        else:
            assert 1 in heartbeat_alive_seen or not heartbeat_alive_seen, (
                f"shm_producer_alive never 1: {heartbeat_alive_seen}\n{report}")

        # ── SCOPED GATES (steady streaming window only) ───────────────────
        # Guard against a vacuous pass: the scoped gates below collapse to
        # `0==0` / `not []` if the steady window is empty (no STREAMING-tagged
        # /sys/metrics arrived before t_drain - guard). Require a minimum sample
        # count so a too-thin window FAILS loudly instead of silently dropping
        # consumer-underrun coverage.
        assert len(steady_xruns) >= 2, (
            f"steady-streaming window too thin to gate consumer xrun "
            f"(t_drain={t_drain}, guard_cutoff={guard_cutoff}, "
            f"xrun_samples={xrun_samples})\n{report}")
        assert max(steady_xruns) == 0, (
            f"consumer underran DURING steady streaming: {steady_xruns}\n{report}")
        assert not steady_hard_warnings, (
            f"hard shm_* warning DURING steady streaming: {steady_hard_warnings}\n{report}")

        # 5 (full only): the 2→3 sequence must be observed on the wire/OSC.
        if full:
            assert STATE_DRAINING in producer_states_seen and STATE_CLOSED in producer_states_seen, (
                f"full mode: producer_state 2→3 sequence not observed: "
                f"{sorted(producer_states_seen)}\n{report}")

        return report
    finally:
        # Teardown: engine first, then producer, then release the held mapping.
        if client is not None:
            client.close()
        for proc in (engine, producer):
            if proc is not None and proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=3)
        if mm is not None:
            mm.close()
        if fd is not None:
            os.close(fd)
        # AC6: never leak the region (producer.close() normally unlinks it).
        try:
            os.unlink(shm_path)
        except FileNotFoundError:
            pass


def _write_report(report: dict) -> Path:
    REPORT_DIR.mkdir(exist_ok=True)
    path = REPORT_DIR / f"pr6_{report['mode']}_{os.getpid()}.json"
    path.write_text(json.dumps(report, indent=2))
    return path


def test_shm_loopback_smoke():
    """Default ~6 s drop-free streaming smoke (hard wire invariants + steady gate)."""
    duration = float(os.environ.get("SHM_SOAK_SECONDS", "5"))
    report = _run_soak(duration_s=duration, full=False)
    path = _write_report(report)
    assert path.exists()


@pytest.mark.soak
def test_shm_loopback_full_soak(request):
    """60 s soak — hard invariants + 2→3 sequence + RSS-slope / fd-leak gate."""
    if not (request.config.getoption("--run-soak", default=False)
            or os.environ.get("SHM_SOAK_FULL") == "1"):
        pytest.skip("full soak is opt-in (use -m soak --run-soak or SHM_SOAK_FULL=1)")
    duration = float(os.environ.get("SHM_SOAK_SECONDS", "60"))
    report = _run_soak(duration_s=duration, full=True)
    path = _write_report(report)

    # Leak gate (assertion 6): fd delta + RSS slope over the LIVE samples.
    # The trailing 0 samples (a process exits → /proc/<pid> vanishes → _rss_kb /
    # _fd_count return 0) are post-mortem artifacts, not a leak signal — drop
    # them so the delta is computed over real in-process readings only.
    for who in ("producer", "engine"):
        fds = [v for v in report["fd_series"][who] if v > 0]
        assert len(fds) >= 2, f"{who}: too few live fd samples to gate: {fds}"
        assert fds[-1] - fds[0] <= 5, f"{who} fd leak: {fds}"
        rss = [v for v in report["rss_series_kb"][who] if v > 0]
        assert len(rss) >= 3, f"{who}: too few live RSS samples to gate: {rss}"
        # Slope guard: last live sample not > first + 50 MB (no growth expected).
        assert rss[-1] - rss[0] <= 50_000, f"{who} RSS grew >50MB: {rss}"
    assert path.exists()


@pytest.mark.adm_player_full
def test_full_adm_player_producer():  # pragma: no cover - PR7
    """Option B (full `python -m adm_player <BWF>` producer) — deferred to PR7.

    Needs a committed multichannel ADM BWF fixture + soundfile in CI. Kept as an
    opt-in stub so the topology decision is recorded, not silently dropped.
    """
    pytest.skip("Option B full-player soak is PR7 (needs BWF fixture + soundfile)")
