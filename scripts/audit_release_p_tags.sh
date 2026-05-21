#!/usr/bin/env bash
# scripts/audit_release_p_tags.sh
#
# Audit P-tag (P[0-9]+-[0-9]+) consistency across release docs, weekly
# progress reports, and ADR/manual files.  First-run is AUDIT-ONLY — this
# script never modifies source documents.
#
# Usage:
#   scripts/audit_release_p_tags.sh [--audit-only] [--help]
#
# Output:
#   audit_reports/p_tags_audit_<YYYYMMDD_HHMMSS>.md
#
# Exit codes:
#   0  — no cross-doc status inconsistencies found (Section 2 empty)
#   1  — inconsistencies detected (see Section 2 of the report)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat <<EOF
Usage: $(basename "$0") [--audit-only] [--help]

Scan release notes, weekly progress reports, ADR docs, and manuals for
P-tag (P[0-9]+-[0-9]+) patterns and produce an audit report.

Options:
  --audit-only   (default) Produce report only; never modify source docs.
  --help         Print this help and exit.

Output file: audit_reports/p_tags_audit_<YYYYMMDD_HHMMSS>.md
  Section 1 — P-tag inventory   : every occurrence with file/line/status
  Section 2 — Inconsistencies   : tags with conflicting status across docs
  Section 3 — Orphans           : tags with no status indicator in ±3 lines
  Section 4 — Summary stats

Exit code: 0 = no inconsistencies; 1 = inconsistencies found.

Integration note:
  Could be added as a CI gate in a future v0.8 step.  Out-of-scope now.
EOF
}

# ── argument parsing ──────────────────────────────────────────────────────────
AUDIT_ONLY=1
for arg in "$@"; do
    case "$arg" in
        --help|-h) usage; exit 0 ;;
        --audit-only) AUDIT_ONLY=1 ;;
        *) echo "Unknown argument: $arg" >&2; usage; exit 2 ;;
    esac
done

# ── output setup ─────────────────────────────────────────────────────────────
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
REPORT_DIR="${REPO_ROOT}/audit_reports"
REPORT_FILE="${REPORT_DIR}/p_tags_audit_${TIMESTAMP}.md"
mkdir -p "${REPORT_DIR}"

# ── collect doc paths ─────────────────────────────────────────────────────────
shopt -s nullglob
DOC_PATHS=()

# Release notes
for f in "${REPO_ROOT}"/docs/release/*/RELEASE_NOTES_EN.md \
          "${REPO_ROOT}"/docs/release/*/RELEASE_NOTES_KR.md; do
    [[ -f "$f" ]] && DOC_PATHS+=("$f")
done

# Weekly progress reports
for f in "${REPO_ROOT}"/docs/weekly_progress_report_*.md; do
    [[ -f "$f" ]] && DOC_PATHS+=("$f")
done

# ADR docs
for f in "${REPO_ROOT}"/docs/adr/*.md; do
    [[ -f "$f" ]] && DOC_PATHS+=("$f")
done

# Manual (Korean)
for f in "${REPO_ROOT}"/docs/manual_kr/*.md; do
    [[ -f "$f" ]] && DOC_PATHS+=("$f")
done

DOCS_SCANNED="${#DOC_PATHS[@]}"

# ── status indicator detection ────────────────────────────────────────────────
# Returns the first status symbol/keyword found within the same line or ±3 lines
# around the given line number in the given file.
get_status() {
    local file="$1"
    local lineno="$2"
    local total
    total=$(wc -l < "$file")

    local start=$(( lineno - 3 ))
    local end=$(( lineno + 3 ))
    (( start < 1 )) && start=1
    (( end > total )) && end=$total

    local context
    context=$(sed -n "${start},${end}p" "$file")

    # Priority: emoji status first, then keywords
    if echo "$context" | grep -q "✅"; then echo "✅"; return; fi
    if echo "$context" | grep -q "⏳"; then echo "⏳"; return; fi
    if echo "$context" | grep -q "✋"; then echo "✋"; return; fi
    if echo "$context" | grep -q "❌"; then echo "❌"; return; fi
    if echo "$context" | grep -iEq "closed|done"; then echo "closed"; return; fi
    if echo "$context" | grep -iEq "open|pending"; then echo "pending"; return; fi
    echo ""
}

# ── scan all docs ─────────────────────────────────────────────────────────────
# tmp arrays stored as lines: "PTAG|FILE|LINENO|STATUS"
TMP_INVENTORY="$(mktemp)"
trap 'rm -f "${TMP_INVENTORY}"' EXIT

for doc in "${DOC_PATHS[@]}"; do
    rel_path="${doc#${REPO_ROOT}/}"
    # grep returns "lineno:line" for each match
    while IFS=: read -r lineno line; do
        # extract all P-tags on this line
        while read -r ptag; do
            status="$(get_status "$doc" "$lineno")"
            echo "${ptag}|${rel_path}|${lineno}|${status}" >> "${TMP_INVENTORY}"
        done < <(echo "$line" | grep -oE "P[0-9]+-[0-9]+" | sort -u)
    done < <(grep -nE "P[0-9]+-[0-9]+" "$doc" 2>/dev/null || true)
done

TOTAL_PTAGS=$(wc -l < "${TMP_INVENTORY}" || echo 0)
TOTAL_PTAGS="${TOTAL_PTAGS// /}"

# ── build inventory table ─────────────────────────────────────────────────────
inventory_table() {
    echo "| P-tag | File | Line | Status |"
    echo "|-------|------|------|--------|"
    sort "${TMP_INVENTORY}" | while IFS='|' read -r ptag file lineno status; do
        echo "| ${ptag} | \`${file}\` | ${lineno} | ${status:-—} |"
    done
}

# ── detect inconsistencies ────────────────────────────────────────────────────
# For each P-tag that appears in ≥2 docs, check if the status indicators conflict.
TMP_INCONS="$(mktemp)"
trap 'rm -f "${TMP_INVENTORY}" "${TMP_INCONS}"' EXIT

# Collect unique P-tags
mapfile -t UNIQUE_TAGS < <(awk -F'|' '{print $1}' "${TMP_INVENTORY}" | sort -u)

for ptag in "${UNIQUE_TAGS[@]}"; do
    # gather distinct (file, status) pairs
    mapfile -t STATUSES < <(grep "^${ptag}|" "${TMP_INVENTORY}" | awk -F'|' '{print $4}' | grep -v '^$' | sort -u)
    mapfile -t FILES    < <(grep "^${ptag}|" "${TMP_INVENTORY}" | awk -F'|' '{print $2}' | sort -u)
    if [[ "${#FILES[@]}" -ge 2 && "${#STATUSES[@]}" -ge 2 ]]; then
        # conflicting statuses across docs
        echo "${ptag}" >> "${TMP_INCONS}"
    fi
done

INCONS_COUNT=$(wc -l < "${TMP_INCONS}" | tr -d ' ')

incons_section() {
    if [[ "${INCONS_COUNT}" -eq 0 ]]; then
        echo "_No cross-doc status conflicts detected._"
        return
    fi
    echo "| P-tag | Files | Statuses found |"
    echo "|-------|-------|----------------|"
    while read -r ptag; do
        files=$(grep "^${ptag}|" "${TMP_INVENTORY}" | awk -F'|' '{print "`"$2"`"}' | sort -u | paste -sd ', ')
        statuses=$(grep "^${ptag}|" "${TMP_INVENTORY}" | awk -F'|' '{print $4}' | grep -v '^$' | sort -u | paste -sd ', ')
        echo "| ${ptag} | ${files} | ${statuses} |"
    done < "${TMP_INCONS}"
}

# ── detect orphans ────────────────────────────────────────────────────────────
# P-tag occurrences where status is empty string
ORPHAN_COUNT=0
orphan_section() {
    local found=0
    echo "| P-tag | File | Line |"
    echo "|-------|------|------|"
    while IFS='|' read -r ptag file lineno status; do
        if [[ -z "$status" ]]; then
            echo "| ${ptag} | \`${file}\` | ${lineno} |"
            (( found++ )) || true
        fi
    done < "${TMP_INVENTORY}"
    ORPHAN_COUNT=$found
    if [[ $found -eq 0 ]]; then
        echo "_No orphan P-tags (all have status indicator within ±3 lines)._"
    fi
}

# ── summary stats ─────────────────────────────────────────────────────────────
summary_section() {
    local with_status=0
    local total="${TOTAL_PTAGS}"
    while IFS='|' read -r ptag file lineno status; do
        [[ -n "$status" ]] && (( with_status++ )) || true
    done < "${TMP_INVENTORY}"

    local pct_status=0
    if [[ "$total" -gt 0 ]]; then
        pct_status=$(( with_status * 100 / total ))
    fi

    local pct_conflict=0
    if [[ "${#UNIQUE_TAGS[@]}" -gt 0 ]]; then
        pct_conflict=$(( INCONS_COUNT * 100 / ${#UNIQUE_TAGS[@]} ))
    fi

    echo "| Metric | Value |"
    echo "|--------|-------|"
    echo "| Total P-tag occurrences | ${total} |"
    echo "| Unique P-tags | ${#UNIQUE_TAGS[@]} |"
    echo "| Docs scanned | ${DOCS_SCANNED} |"
    echo "| Occurrences with status indicator | ${with_status} (${pct_status}%) |"
    echo "| Unique tags with cross-doc conflict | ${INCONS_COUNT} (${pct_conflict}% of unique tags) |"
}

# ── write report ──────────────────────────────────────────────────────────────
{
cat <<HEADER
# P-tag Audit Report

Generated: $(date -u '+%Y-%m-%d %H:%M:%S UTC')
Script: \`scripts/audit_release_p_tags.sh\`
Mode: audit-only (no source docs modified)

---

## Section 1 — P-tag Inventory

HEADER

inventory_table

cat <<S2

---

## Section 2 — Inconsistencies

> P-tags appearing in ≥2 docs with conflicting status indicators.

S2

incons_section

cat <<S3

---

## Section 3 — Orphans

> P-tag occurrences with no status indicator (✅ ⏳ ✋ ❌ / closed / open / done / pending) within ±3 lines.

S3

orphan_section

cat <<S4

---

## Section 4 — Summary Stats

S4

summary_section

} > "${REPORT_FILE}"

# ── done ─────────────────────────────────────────────────────────────────────
echo "Audit report written: ${REPORT_FILE}"
echo "  Docs scanned      : ${DOCS_SCANNED}"
echo "  P-tag occurrences : ${TOTAL_PTAGS}"
echo "  Inconsistencies   : ${INCONS_COUNT}"

if [[ "${INCONS_COUNT}" -gt 0 ]]; then
    echo "WARNING: ${INCONS_COUNT} cross-doc status conflict(s) found — see Section 2 of the report."
    exit 1
fi
exit 0
