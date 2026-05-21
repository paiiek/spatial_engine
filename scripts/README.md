# scripts/

Helper scripts for the Spatial Engine project.

---

## audit_release_p_tags.sh

Scans release notes, weekly progress reports, ADR docs, and Korean manual
files for P-tag patterns (`P[0-9]+-[0-9]+`) and produces an audit report.

**Run:**

```bash
# audit-only (default) — never modifies source docs
bash scripts/audit_release_p_tags.sh

# explicit flag (same behaviour)
bash scripts/audit_release_p_tags.sh --audit-only

# help
bash scripts/audit_release_p_tags.sh --help
```

**Output:** `audit_reports/p_tags_audit_<YYYYMMDD_HHMMSS>.md` with four sections:

| Section | Contents |
|---------|----------|
| 1 — Inventory | Every P-tag occurrence: file, line, nearest status indicator |
| 2 — Inconsistencies | Tags present in ≥2 docs with conflicting status (✅ vs ⏳ etc.) |
| 3 — Orphans | Occurrences with no status indicator within ±3 lines |
| 4 — Summary stats | Totals, % with status, % conflicts |

**Exit codes:** `0` = no cross-doc conflicts; `1` = conflicts found.

**Docs scanned:**
- `docs/release/*/RELEASE_NOTES_EN.md` and `RELEASE_NOTES_KR.md`
- `docs/weekly_progress_report_*.md`
- `docs/adr/*.md`
- `docs/manual_kr/*.md`

**CI integration (v0.8 candidate):** The script can be wired into CI as a
required gate once the orphan/conflict baseline is accepted.  Out-of-scope
for v0.7; add as a step in `.github/workflows/` when ready.

---

## bootstrap_juce.sh

Idempotent fetch of JUCE 7.0.12 at the pinned SHA into `core/JUCE/`.
Re-running is a no-op if already at the expected commit.

```bash
bash scripts/bootstrap_juce.sh
```

---

## bootstrap_pluginval.sh

Fetches the pluginval binary for DAW-host validation smoke tests.

```bash
bash scripts/bootstrap_pluginval.sh
```

---

## check_test_ndebug.sh

Verifies every `CMakeLists.txt` under `core/tests/` and `vst3/tests/`
carries `-UNDEBUG` to prevent the NDEBUG assert-stripping bug (commit
5a60720).  Run before release builds.

```bash
bash scripts/check_test_ndebug.sh
```

---

## fetch_ir.py / verify_byte_baseline.py

Python helpers for IR fixture management and byte-level baseline
verification.  See inline docstrings for usage.
