# ADR 0017 — Runtime-Demote Telemetry Channel (`/sys/binaural_diag`)

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-05-19 |
| **Authors** | Planner / Architect / Critic (v0.7 iter-3 consensus) |
| **Predecessor** | ADR 0016 (external distribution policy); v0.6 #5 runtime sticky-underrun auto-demote |

---

## Context

v0.6 introduced runtime sticky-underrun auto-demote (B2 → B1 Direct when the B2 fan-out
exceeds 90% of the block deadline for 8 consecutive blocks). The `ambivs_demoted_runtime`
warning is emitted over `/sys/binaural_warning ,s` exactly once per demote event.

That warning tells the host *that* a demote happened, but not *what* the observed CPU
pressure was at the moment of demote. Without ratio telemetry, operators cannot distinguish
a marginal (91%) demote from a catastrophic (200%) one, or tell whether the demote occurred
at 64-sample blocks vs. 512-sample blocks. Both affect whether a D-S1 reset is prudent.

v0.7 adds three demote-moment snapshot atomics (D-S3) and a new outbound diagnostic channel
to carry them to the host, enabling informed operator decisions about the reset hatch.

---

## Decision A — Event-driven channel, not periodic

`/sys/binaural_diag` is emitted **once per demote event**, immediately after the
`ambivs_demoted_runtime` warning on the same IO-thread heartbeat drain pass.

**Rationale:** periodic emission of ratio telemetry would require a continuous
accumulator path, sustained IO bandwidth, and a consumer protocol for aggregating
rolling windows. None of that complexity is justified before real telemetry shape
is observed. A single event-at-demote packet gives operators the data they need
(what was the pressure? what block size?) at zero steady-state overhead.

The slow-degradation pattern (ratio creeping upward over hours without crossing the
demote threshold) is acknowledged as undetected by this design. A pre-demote-window
summary channel (B-3) is deferred to v0.8, conditional on observing this pattern
in real deployments. See `docs/ipc_schema.md` §AS-2.

---

## Decision B — Dedicated `sendReplyImplIIF`, not flag-extension of v0.6 #8

The `/sys/binaural_diag ,iif` packet requires mixed-type arguments (int, int, float).
Two implementation options were evaluated:

**Option (a) — rejected:** Extend the existing `sendReplyImpl(addr, types, s, have_f, f,
have_i, i)` 7-parameter impl to 9 parameters with 3 boolean flags (`have_i1`, `have_i2`,
`have_f1`). Rejected because:

- Extending `sendReplyImpl` to 9 parameters with 3 boolean flags forces the 3 existing
  v0.6 #8 overloads (`sendReply(addr, types, s)`, `sendReply(addr, types, s, f)`,
  `sendReply(addr, types, i)`) to spell extra `false, 0` defaults at every call site,
  violating the "thin forwarder" invariant those overloads were introduced to enforce.
- Mixed-type packets (`,iif` here, possibly `,sif`/`,iiif` in future) are a different
  *shape* of packet from the simple typetags v0.6 #8 unified. Forcing them through the
  same impl creates more accidental coupling than it removes — the flag-dispatch table
  grows non-linearly with each new packet shape.
- A 7→9 parameter change is a non-local signature break: any out-of-tree caller linking
  `sendReplyImpl` (even via the vtable-free stub path) would silently pass stale argument
  counts at link time on ABI-compatible platforms.

**Option (b) — accepted:** A parallel `sendReplyImplIIF(addr, types, i1, i2, f1)` private
impl with a dedicated `encodeOscReplyIIF` encoder. The intentional duplication is bounded
(~40 LOC) and the coupling surface is zero: changes to v0.6 #8's flag logic cannot
accidentally affect the `,iif` path or vice versa. The public overload
`sendReply(addr, types, int32_t, int32_t, float)` is a 1-line forwarder.

The inline comment in `OSCBackend.cpp` at the new impl reads:
```
// v0.7 D-S3 — intentional duplication of sendReplyImpl. See ADR 0017 §B for
// rejection of flag-extension option (a).
```

---

## Decision C — Snapshot-at-latch, not live field read at drain

The three diagnostic fields (max ratio, block size, sample rate) are snapshotted at the
audio-thread CAS-success demote latch into dedicated `std::atomic<int>` fields
(`runtime_demote_max_ratio_at_event_x1000_`, `runtime_demote_block_size_at_event_`,
`runtime_demote_sample_rate_at_event_`), using release ordering.

The IO-thread heartbeat drain reads these snapshot fields (acquire ordering) rather than
querying live `BinauralMonitor` fields. This avoids the race where a subsequent
`prepareToPlay()` re-initialises `block_size_` and `sample_rate_` between the demote latch
and the drain, which could produce diagnostics that reflect the *post-demote* session
context rather than the *demote-moment* context. The snapshot is authoritative.

The `runtime_demote_max_ratio_x1000_` accumulator (the running max during the strike run)
uses the AM-2 relaxed-load-then-store pattern (NOT `store(max(…))`, which is two
non-atomic operations). The audio thread is the sole producer (verified per v0.6 retro
§A.2), so no concurrent writer races exist; CAS promotion is deferred to v0.8 conditional
on telemetry-informed precision need.

---

## Consequences

- One new outbound OSC address (`/sys/binaural_diag`) added to the wire surface.
  Existing clients that filter on address will ignore it silently.
- Four new `std::atomic<int>` fields added to `BinauralMonitor` (D-S1 reset now clears
  8 atomics rather than 4 — enumerated in `resetRuntimeDemoteFromUser`).
- `OSCBackend` gains one new private encoder (`encodeOscReplyIIF`) and one new public
  overload; existing overloads are unchanged.
- The `,iif` encoder is intentionally NOT wired through the v0.6 #8 unified `sendReplyImpl`
  — see Decision B. Future mixed-type packet shapes should follow the same parallel-impl
  pattern until a genuine generalisation is justified by ≥3 distinct shapes.
