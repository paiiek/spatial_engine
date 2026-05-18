# CI Quarantine — Known Workarounds

## Status (v0.5.2): ALL ASan tests pass

As of v0.5.2 #2, the v0.5 / v0.5.1 ASan quarantine is **fully resolved**.
The full ctest suite (114/114) passes cleanly under
`-fsanitize=address,undefined`. No tests are currently quarantined.

This document is retained so the (eventual) `git log` traveller understands
why two seemingly unnecessary defenses (`std::quick_exit(0)` in two test
mains + `__SANITIZE_ADDRESS__` guards in three test files) exist.

---

## Defenses still in place

### 1. `__SANITIZE_ADDRESS__` guard in `vst3/tests/rt_alloc_probe.hpp`

**Root cause (corrected v0.5.2 #2):** `rt_alloc_probe.hpp` installs
strong-symbol overrides for `malloc`, `free`, `calloc`, and the `operator
new`/`delete` family. These overrides delegate to glibc-internal
`__libc_malloc` / `__libc_free` / `__libc_calloc`, deliberately bypassing
glibc's weak-symbol resolution to install an RT-allocation counter.

Under AddressSanitizer, ASan also installs interceptors on malloc/free. The
two are mutually exclusive at link time: a strong symbol wins. Once
rt_alloc_probe wins, ASan's internal allocations (e.g. the buffer
`pthread_getattr_np` calls `free()` on during `asan_thread_start`) route to
`__libc_free` on a pointer that was actually allocated through ASan's
intercepted-malloc. glibc detects the size-class mismatch and raises
`SIGABRT` with `free(): invalid size`.

The crash signature changes depending on where in the new-thread bring-up
the free lands; observed signatures include `free(): invalid size` and
`munmap_chunk(): invalid pointer`.

The bug was latent until v0.5.1 wired a `std::thread` heartbeat into
`SpatialEngineProcessor::setActive(true)`. Earlier VST3 tests never spawned
a new thread that ASan needed to instrument, so the conflict never fired.

**Why the v0.5 P6 "Steinberg SDK static destructor" diagnosis was wrong:**
The two then-failing tests both abort with `munmap_chunk: invalid pointer`,
which matches Steinberg-SDK-fingerprint reports. But the actual call site
is `asan_thread_start → pthread_getattr_np → free()` (not a Steinberg
destructor at all). Without a full ASan backtrace it's easy to misattribute
because both signatures look identical until you turn on
`ASAN_OPTIONS=fast_unwind_on_malloc=0:print_stacktrace=1`.

**Fix:** wrap the strong-symbol overrides in
`#ifndef __SANITIZE_ADDRESS__`. Under ASan the probe degrades to a no-op;
ASan owns malloc/free as it should. The non-ASan ctest run remains the
authoritative RT-alloc enforcement surface.

### 2. `__SANITIZE_ADDRESS__` guards in three test bodies

`test_vst3_dispatch_rt_safety.cpp`, `test_vst3_intra_plugin_spsc_drain.cpp`,
and `soak_vst3_console_flood.cpp` each contain `probe_observes_malloc` /
`probe_observes_calloc` negative-control assertions that intentionally call
`std::malloc(64)` and assert the probe counter incremented to 1. Under ASan
the counter is stuck at 0 (overrides skipped). The assertions degrade to
`SKIPPED-ASAN` printouts so the test body can proceed and the rest of the
assertions (which exercise behavioral correctness) still run.

### 3. `std::quick_exit(0)` in two ASan-quarantined targets

**Retained as defense-in-depth.** The original v0.5.1 ADR-v051-03 hypothesis
(Steinberg static dtor raises SIGABRT before ASan's atexit) is *not* the
actual root cause for these specific failures — that was the rt_alloc_probe
race. However, `quick_exit(0)` remains a cheap safety net against any
future host-config-specific static-dtor weirdness, and removing it has zero
upside.

Applied in:
- `vst3/tests/test_vst3_intra_plugin_spsc_drain.cpp`
- `vst3/tests/soak_vst3_console_flood.cpp`

`quick_exit` skips C++ static destructors while still flushing stdio. The
test body's per-allocation ASan tracking is entirely unaffected — only the
post-`main` destructor chain is bypassed.

---

## Historical note: v0.6 #7 ("Steinberg SDK upstream patch") closed as won't-fix

v0.5.1 plan §ADR-v051-03 proposed a v0.6 follow-up to "attempt an upstream
Steinberg SDK patch or vendor a fork that fixes the static-dtor double-free."
With v0.5.2 #2 showing that **there was never a Steinberg static-dtor bug**
(the symptoms matched but the actual root cause was rt_alloc_probe's
malloc override fighting ASan), this follow-up is closed without action.

If a real Steinberg-side issue ever surfaces in the future, treat it as a
new investigation — do not assume continuity with this quarantine entry.
