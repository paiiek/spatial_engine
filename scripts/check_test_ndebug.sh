#!/usr/bin/env bash
# scripts/check_test_ndebug.sh
#
# Prevent recurrence of the NDEBUG strip bug fixed in commit 5a60720.
# Release builds inject -DNDEBUG which strips assert(); without -UNDEBUG
# at test target scope, every assertion in C++ tests becomes a no-op and
# tests print PASS regardless of actual failure.
#
# Verifies every CMakeLists.txt under core/tests carries an -UNDEBUG entry
# (e.g. add_compile_options(-UNDEBUG) at directory scope).

set -e
fail=0
shopt -s nullglob
files=$(find core/tests -name CMakeLists.txt 2>/dev/null)
if [[ -z "$files" ]]; then
    echo "check_test_ndebug: no core/tests CMakeLists.txt found"
    exit 0
fi
for f in $files; do
    if ! grep -q -- '-UNDEBUG' "$f"; then
        echo "MISSING -UNDEBUG: $f"
        fail=1
    fi
done
exit $fail
