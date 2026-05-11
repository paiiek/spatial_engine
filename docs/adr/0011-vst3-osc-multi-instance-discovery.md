# ADR 0011 — VST3 plugin multi-instance discovery (file-based registry)

**Status**: Draft (v0.2.0 ships this design contract; v0.3.0 implements)
**Date**: 2026-05-10
**Spec commit pin**: `v0.3.0-c4-draft` — frozen post-RALPLAN Round-2 APPROVE
**Related**: ADR 0003 (single OSC schema), ADR 0010 (binding model — A1-ε per-instance UDP)

---

## Context

ADR 0010 binds **one UDP socket per plugin instance** at an auto-assigned
ephemeral port. ADM-OSC senders (Korean live-venue consoles, L-ISA
Controller, Spat Revolution) need to know **which port belongs to which
plugin instance** to route `/adm/obj/N/...` packets to the correct
endpoint. Without a discovery mechanism, the sender cannot guess the
ephemeral port.

This ADR defines the v0.3 discovery surface as a **file-based JSON
registry** at `~/.config/spatial_engine/instances.json`, written by each
plugin instance at startup and consulted by the standalone
`spatial_engine_core` (and ADM-OSC senders) to determine routing.

---

## Decision (frozen post-RALPLAN Round-2): A3-β (file-based registry)

A single JSON file at a well-known path is the source of truth for
plugin-instance routing in v0.3.

### File location

`~/.config/spatial_engine/instances.json` (XDG-compliant). Permissions:
`0644` (owner write, world read).

### File schema (v1)

```jsonc
{
  "schema_version": 1,             // Increment on breaking schema change.
  "spec_commit": "v0.3.0-c4-draft", // Mirror of ADR 0010 spec pin.
  "instances": [
    {
      "pid": 12345,                // OS process ID of the DAW process hosting the plugin instance
      "instance_id": "spe-vst3-1c4f8e",  // Stable per-instance UUID (16 hex chars)
      "bind_port": 49271,          // Kernel-assigned ephemeral port (recv-only, A1-ε + A2-α)
      "obj_id_subset": [0, 1, 2, 3], // Which ADM-OSC obj_ids this instance accepts
      "started_at_unix": 1715380800, // Plugin instance ctor time (epoch seconds)
      "last_heartbeat_unix": 1715381234 // Updated every 10s by the dedicated thread
    }
  ]
}
```

`obj_id_subset` is the consumer-facing piece: a sender that wants to
address `/adm/obj/3/aed` looks up which instance has `3 ∈ obj_id_subset`
and sends to that instance's `bind_port`. The subset is configured per
instance via plugin parameter or default range (v0.3 default: each
instance accepts all `obj_id`s; advanced per-instance partitioning is a
v1.0 follow-up).

### Hardening rules (frozen post-RALPLAN Round-2)

These rules address Architect Round-1 blocker #2 + Critic concurrence:
file-locking, GC, EPIPE handling, `schema_version` semantics.

#### 1. Atomic write via `tmpfile + rename(2)`

Writers do NOT modify the file in place. Pattern:

```cpp
const std::string final_path = "~/.config/spatial_engine/instances.json";
const std::string tmp_path   = final_path + ".tmp.{pid}";

// 1. Build the new JSON content in memory.
// 2. Write to tmp_path (open with O_CREAT|O_WRONLY|O_TRUNC).
// 3. fsync(tmp_fd) to ensure durable.
// 4. close(tmp_fd).
// 5. rename(tmp_path, final_path) — POSIX-atomic on the same filesystem.
```

This guarantees readers never observe a torn file. Rationale: `rename(2)`
is atomic per POSIX; readers either see the old file or the new file,
never a partial write.

#### 2. `flock(LOCK_EX)` advisory lock during write

Multiple writers (multiple plugin instances starting concurrently) must
not interleave their `tmpfile + rename` sequences. Acquire
`flock(final_path_fd, LOCK_EX)` before step 1 of the atomic write
sequence; release after `rename`. Readers do NOT need to lock (they read
the post-`rename` file directly).

Lock timeout: writers retry `flock` with `LOCK_EX | LOCK_NB` up to 10
times with 50ms backoff; if still locked, log a WARN and skip the
update (writer's own process state is preserved; next heartbeat will
retry).

#### 3. `/proc/{pid}/comm` PID liveness check (GC)

Stale entries (plugin instances that crashed without removing their
record) must be garbage-collected. On every write:

```cpp
for (auto& entry : registry.instances) {
  std::ifstream comm("/proc/" + entry.pid + "/comm");
  if (!comm.is_open() || comm.peek() == EOF) {
    // PID is dead or recycled — drop entry.
    continue;
  }
  // Optional: verify the process command name matches the expected
  // DAW pattern (e.g. /reaper|bitwig|.*audio.*/i) to defend against
  // PID recycling. v0.3 baseline: skip this check; revisit if a
  // false-positive is observed in the wild.
  fresh_entries.push_back(entry);
}
```

Liveness check runs in the writer (the next instance to update its
own heartbeat). Readers do not GC; they tolerate stale entries gracefully
(see rule 4).

#### 4. Reader tolerance: stale-entry handling

Readers (the standalone `spatial_engine_core` and ADM-OSC senders) MUST
tolerate stale entries:

- Try `sendto(bind_port)`; on `ECONNREFUSED` or `EPIPE` (no listener),
  log WARN at most once per `(pid, bind_port)` pair per minute. Do NOT
  retry indefinitely.
- Skip the entry on the next `obj_id` lookup; allow the next writer's
  GC sweep to clean it up.
- A reader-side cache invalidation: re-read `instances.json` every 5
  seconds (cheap; <1KB file). Live registry entries dominate stale ones
  in steady state.

Rationale: making readers responsible for GC creates write-conflict
contention; the writer-side GC pattern is asymmetric but simpler.

#### 5. EPIPE / ECONNREFUSED handling

The sidecar relay (deferred A1-δ contingency) and standalone direct
relay both encounter dead sockets when a plugin instance exits without
GC. Behaviour:

- Drop the packet silently for the failing instance.
- Continue to other instances normally.
- Log to stderr at WARN level (not ERROR — this is expected on shutdown).
- Increment `osc_drop_count` metric (visible via `/sys/state` poll).

#### 6. `schema_version` field semantics

- `schema_version: 1` is the v0.3 initial value.
- Bumps occur on **breaking** schema changes (e.g. adding required
  fields, changing field types, removing fields).
- Readers MUST refuse to operate on a `schema_version` higher than they
  understand (forward-incompat is fail-closed); they MUST tolerate a
  lower `schema_version` only if the missing fields have defined
  defaults.
- v0.3 readers (the standalone `spatial_engine_core`) understand `1`
  only; future v0.4+ readers might add support for `2` while still
  honouring `1`.

This mirrors the `Processor.cpp:267-289` multi-version reader pattern
already proven in C2B postmortem.

---

## Drivers

1. **Console routing problem**: ADM-OSC senders cannot guess ephemeral
   bind ports. A discovery mechanism is mandatory under A1-ε.
2. **Zero new dependencies**: filesystem-only, no mDNS daemon, no
   broadcast multicast.
3. **Debuggability**: `cat ~/.config/spatial_engine/instances.json |
   jq` is the entire diagnostic surface.

---

## Alternatives considered

### A3-α — mDNS / Bonjour service discovery (rejected for v0.3)

- **Steelman**: standard discovery; cross-host scaling.
- **Rejected because**: new dependency (`avahi`, `mDNSResponder`); not
  every Linux live-venue install has it enabled by default; debugging
  requires `avahi-browse`. Reserved for v1.0 if cross-host plugin
  hosting becomes a real use case.

### A3-γ — `/sys/state` poll (rejected)

- **Steelman**: re-use existing OSC channel; no new surface.
- **Rejected because**: chicken-and-egg — to learn an instance's
  bind_port, the sender must already know that the standalone speaks
  for the plugin, but the standalone has no authoritative view of plugin
  instances unless they register somewhere first. Registry comes first.

### A3-δ — Single-instance only (rejected as scope cut)

- **Steelman**: v0.3 ships with `obj_id_subset = [0..63]` for the
  single instance; no registry needed.
- **Rejected because**: A1-ε's whole point is multi-instance support;
  scope-cutting to single defeats the design driver. Held as
  emergency fallback if A3-β implementation slips badly.

### A3-β with shm registry instead of file (rejected)

- **Steelman**: faster reads; no file syscall.
- **Rejected because**: shm objects are not human-debuggable;
  filesystem-based is the v0 inspection-friendly choice; performance
  is irrelevant (registry reads are ~10/s, not 10000/s).

---

## Why chosen

File-based JSON registry with `tmpfile+rename+flock` atomicity:

- Survives writer crashes (atomic rename guarantees consistent reads).
- Survives reader concurrency (no lock needed on read path).
- Survives multi-writer concurrency (advisory `flock` serializes
  registrations).
- Inspectable by humans (`jq`), by tools (`cat`, `grep`).
- Zero new dependencies.
- Honest about its limitations (no cross-host; rely on filesystem
  semantics; advisory lock can be ignored by malicious writers — but
  we trust co-tenant DAW processes on the same machine).

---

## Consequences

### Positive
- Plugin instances are discoverable by the standalone and by ADM-OSC
  senders without configuration.
- Stale entries self-heal via writer-side GC.
- Debugging reduces to `cat` + `jq`.

### Negative
- New surface: filesystem path; XDG-compliance assumption.
- Multi-writer concurrency requires `flock` discipline.
- PID recycling on long-running systems (e.g. years of uptime) might
  cause false-positive liveness if `/proc/{pid}/comm` matches a
  recycled PID. Documented as known limitation; rare in practice.

### Neutral
- File grows linearly with instance count; for typical N≤8, file size
  stays under 4KB. No GC pressure.

---

## File path conventions (XDG-compliance)

Per the XDG Base Directory Specification:

```
$XDG_CONFIG_HOME/spatial_engine/instances.json
```

Default: `~/.config/spatial_engine/instances.json` if `$XDG_CONFIG_HOME`
is unset.

Plugin and standalone agree on this path via a shared constant in
`core/src/util/RegistryPath.h` (NEW header for v0.3).

---

## Follow-ups (v0.3 implementation contract)

- **S2 (v0.3)**: `vst3/sidecar_bridge/PluginInstanceRegistry.{h,cpp}` —
  C++ writer/reader implementing the rules above.
- **S2 (v0.3)**: `core/tests/core_unit/test_p_instances_registry.cpp` —
  unit tests for atomic write, concurrent writers (via fork), reader
  tolerance, GC of dead PIDs, schema_version refusal.
- **v1.0 candidate**: A3-α mDNS upgrade for cross-host plugin hosting.
- **v1.0 candidate**: per-instance `obj_id_subset` configuration UI
  (depends on plugin editor view shipping in Phase D6).
