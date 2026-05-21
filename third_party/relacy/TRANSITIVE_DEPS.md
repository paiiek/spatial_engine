# relacy Transitive Dependency Audit

## Audit command (starting set per Architect §B)

```bash
grep -rE '#include.*(boost|tbb|absl|tbb_iterators|abseil|google::|btree)' third_party/relacy/
```

## Result

**Zero matches.** relacy depends only on the C++ standard library and OS platform
headers (pthreads on POSIX, windows.h on Win32). No Boost, TBL, abseil, or other
non-stdlib third-party library is included.

## Additional patterns scanned (Architect §B — beyond starting set)

```bash
grep -rE '#include.*(gtest|gmock|catch|doctest|fmt|spdlog|eigen|opencv)' third_party/relacy/
```

Result: zero matches.

## Conclusion

relacy dev-dep surface is limited to BSD-3-Clause (relacy itself) + stdlib. ADR 0016
amendment NOT required. No GPL/LGPL exposure.

## Periodic re-scan recommendation

Re-run the above grep recipes after any `UPSTREAM_PIN.txt` SHA bump. Also scan for:
- `tbb_iterators`, `abseil`, `google::`, `btree` (Architect §B patterns)
- Any new `#include <` that resolves outside `<>` standard headers
