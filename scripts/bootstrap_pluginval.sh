#!/usr/bin/env bash
# Phase C C2 — Tracktion/pluginval clone+build for VST3 host validation.
# Idempotent: if scripts/_build/pluginval/build/pluginval already exists,
# the script reports the version and exits 0.

set -euo pipefail
TARGET=scripts/_build/pluginval
if test -x "$TARGET/build/pluginval"; then
    echo "pluginval already built at $TARGET/build/pluginval"
    "$TARGET/build/pluginval" --version || true
    exit 0
fi
mkdir -p "$(dirname "$TARGET")"
git clone --depth 1 https://github.com/Tracktion/pluginval.git "$TARGET"
cmake -B "$TARGET/build" -S "$TARGET" -DCMAKE_BUILD_TYPE=Release
cmake --build "$TARGET/build" -j"$(nproc)"
test -x "$TARGET/build/pluginval" || (echo "pluginval build failed"; exit 1)
"$TARGET/build/pluginval" --version
