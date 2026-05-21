# relacy Promotion Gate — v0.7.0

## Why relacy is gated behind `-DSPATIAL_ENGINE_BUILD_RELACY_TESTS=ON`

relacy is a header-only race-detector library that replaces `std::atomic` and
`std::thread` with instrumented versions at compile time.  This substitution is
incompatible with production code; including relacy headers in the global build
would silently alter memory-model semantics for all atomics in spe_core and
spe_util.

The flag isolates the include path strictly to the `test_osc_outbound_relacy`
target.  Production code (`spe_core`, `spe_util`) is never compiled with relacy
headers.  The dedicated build dir convention is:

```bash
mkdir build_relacy
cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_BUILD_RELACY_TESTS=ON
make -j$(nproc) test_osc_outbound_relacy
./test_osc_outbound_relacy
ctest --output-on-failure   # 118 default + 1 relacy = 119 total
```

## Promotion order (Critic §C.4)

| Priority | Target                        | Gate condition                              |
|----------|-------------------------------|---------------------------------------------|
| P0       | Linux ARM64 CI (`cross-platform.yml`) | 5 consecutive green runs → promote to required |
| P1       | relacy CI (`relacy.yml`)      | ARM64 P0 stable **AND** 5 consecutive green runs |

Rationale: ARM64 hardware-race surface outranks synthetic-verifier confidence.
relacy is an upstream early-warning tool, not a merge gate until P1 promotion.

## 5-green soak requirement

The relacy ctest is `continue-on-error: true` in the `.github/workflows/relacy.yml`
job for the entire v0.7 cycle.  Promotion to required (removing
`continue-on-error`) requires:

1. Linux ARM64 already promoted to required (P0 done).
2. 5 consecutive green runs of the relacy job on the main branch.
3. PR updating `relacy.yml` to remove `continue-on-error: true`.
4. `osc_outbound_relacy` added to branch-protection required status checks
   (GitHub UI step; repo-admin executes).

## License audit checklist (Critic §B.6 — mandatory at vendoring)

1. **Upstream URL pinned**: `https://github.com/dvyukov/relacy`
2. **Commit SHA pinned**: `third_party/relacy/UPSTREAM_PIN.txt` — SHA `599c788cab890b1d18507a87827f5f2fc729512a`
3. **License file verbatim**: `third_party/relacy/LICENSE` — BSD-3-Clause (upstream verbatim). Note: upstream is BSD-3-Clause (has no-endorsement clause), not BSD-2-Clause as originally estimated in the plan spec. Documented in `third_party/relacy/LICENSE-NOTES.md`. ADR 0016 amendment NOT required — BSD-3-Clause is pre-approved.
4. **Transitive deps audit**: zero non-stdlib dependencies found. See `third_party/relacy/TRANSITIVE_DEPS.md`.
5. **CMake integration**: `option(SPATIAL_ENGINE_BUILD_RELACY_TESTS ... OFF)` in `core/CMakeLists.txt`. Include path is target-scoped to `test_osc_outbound_relacy` only.

## Transitive-dep grep recipe (Architect §B — starting set + extensions)

Re-run after any `UPSTREAM_PIN.txt` SHA bump:

```bash
# Starting set
grep -rE '#include.*(boost|tbb|absl|tbb_iterators|abseil|google::|btree)' third_party/relacy/

# Extended patterns (Architect §B)
grep -rE '#include.*(gtest|gmock|catch2|doctest|fmt|spdlog|eigen|opencv)' third_party/relacy/

# Any non-stdlib <> include that is not a well-known std header
grep -rE '#include <[a-z_]+>' third_party/relacy/relacy/*.hpp | grep -v \
  -e 'algorithm' -e 'array' -e 'atomic' -e 'cassert' -e 'cstddef' \
  -e 'cstdint' -e 'cstring' -e 'functional' -e 'iostream' -e 'map' \
  -e 'memory' -e 'mutex' -e 'set' -e 'stdexcept' -e 'string' \
  -e 'thread' -e 'type_traits' -e 'utility' -e 'vector' -e 'climits' \
  -e 'cstdlib' -e 'cerrno' -e 'condition_variable' -e 'chrono' \
  -e 'sstream' -e 'list' -e 'deque' -e 'unordered_map' -e 'tuple' \
  -e 'limits' -e 'new' -e 'bitset' -e 'fstream' -e 'iomanip'
```

If any match is found: document license compatibility in `TRANSITIVE_DEPS.md`
and file an ADR 0016 amendment if the new license is outside MIT/BSD/Apache-2.0.
Flag any GPL/LGPL match as a blocker.
