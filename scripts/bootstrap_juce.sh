#!/usr/bin/env bash
# Phase C C2 §15.H — JUCE 7.0.12 idempotent fetch with SHA pinning.
# Re-running this script is a no-op if core/JUCE/ already exists at the
# expected SHA; mismatch fails loudly with a manual remediation hint.

set -euo pipefail
TARGET=core/JUCE
JUCE_VERSION=7.0.12
EXPECTED_SHA=4f43011b96eb0636104cb3e433894cda98243626

if test -f "$TARGET/CMakeLists.txt"; then
    actual=$(git -C "$TARGET" rev-parse HEAD 2>/dev/null || echo "")
    if [[ -n "$EXPECTED_SHA" && "$actual" != "$EXPECTED_SHA" ]]; then
        echo "ERROR: JUCE SHA mismatch (got $actual, expected $EXPECTED_SHA)"
        echo "Manual: rm -rf $TARGET && rerun this script"
        exit 1
    fi
    echo "JUCE already bootstrapped at $TARGET ($actual)"
    exit 0
fi

git clone --depth 1 --branch "$JUCE_VERSION" --single-branch \
    https://github.com/juce-framework/JUCE.git "$TARGET"
test -f "$TARGET/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp" \
    || (echo "JUCE bootstrap incomplete (missing juce_audio_plugin_client VST3)"; exit 1)
echo "JUCE bootstrapped: $(git -C "$TARGET" rev-parse HEAD)"
