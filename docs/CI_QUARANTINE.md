# CI Quarantine — Known Workarounds

## ASan `std::quick_exit(0)` Workaround

### Affected targets

| Binary | CTest name |
|--------|-----------|
| `test_vst3_intra_plugin_spsc_drain` | `vst3_intra_plugin_spsc_drain` |
| `soak_vst3_console_flood` | `vst3_console_flood` |

### Root cause

The Steinberg VST3 SDK registers static destructors that call glibc `free()` on
pointers that were not allocated via the standard allocator. When AddressSanitizer
(ASan) is active, glibc raises `SIGABRT` with `munmap_chunk: invalid pointer`
**before** ASan's own `atexit` handler runs. The result is a hard crash on exit
that looks like a test failure even when all assertions passed.

This is **not** interceptable via `ASAN_OPTIONS=exitcode=0` because the signal is
raised by glibc inside a static destructor, before ASan finalises its report and
calls `_exit()`.

### Workaround

Both binaries call `std::quick_exit(0)` (or `std::quick_exit(exit_code)` for the
drain test) immediately before `return` from `main()`. `quick_exit` skips C++
static destructors while still flushing stdio. ASan's per-allocation tracking
during the test body is entirely unaffected — only the post-`main` destructor
chain is bypassed.

The workaround is applied in:
- `vst3/tests/test_vst3_intra_plugin_spsc_drain.cpp`
- `vst3/tests/soak_vst3_console_flood.cpp`

CMakeLists comments cross-reference this file at both `add_test()` call sites
(`vst3/tests/CMakeLists.txt` lines for `vst3_intra_plugin_spsc_drain` and
`vst3_console_flood`).

### v0.6 follow-up

Attempt an upstream patch to the Steinberg SDK static destructors, or vendor a
fork that uses placement-new with explicit destruction to avoid the broken
allocator hand-off. Track under issue: "ASan clean exit for VST3 test binaries".
