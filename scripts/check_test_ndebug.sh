#!/usr/bin/env bash
# scripts/check_test_ndebug.sh
#
# Prevent recurrence of the NDEBUG strip bug fixed in commit 5a60720.
# Release builds inject -DNDEBUG which strips assert(); without -UNDEBUG
# at test target scope, every assertion in C++ tests becomes a no-op and
# tests print PASS regardless of actual failure.
#
# Verifies every CMakeLists.txt under core/tests / vst3/tests carries an
# -UNDEBUG entry (e.g. add_compile_options(-UNDEBUG) at directory scope).
# Phase C C2 v4 §6 Step 0.b 부수 작업 — vst3/tests/ 도 스캔 대상.

set -e
fail=0
shopt -s nullglob
scan_dirs=()
[[ -d core/tests ]] && scan_dirs+=(core/tests)
[[ -d vst3/tests ]] && scan_dirs+=(vst3/tests)
if [[ ${#scan_dirs[@]} -eq 0 ]]; then
    echo "check_test_ndebug: no test directories found (core/tests, vst3/tests)"
    exit 0
fi
files=$(find "${scan_dirs[@]}" -name CMakeLists.txt 2>/dev/null)
if [[ -z "$files" ]]; then
    echo "check_test_ndebug: no test CMakeLists.txt found in: ${scan_dirs[*]}"
    exit 0
fi
for f in $files; do
    if ! grep -q -- '-UNDEBUG' "$f"; then
        echo "MISSING -UNDEBUG: $f"
        fail=1
    fi
done
exit $fail
