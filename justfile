# spatial_engine task runner — just <recipe>
# https://github.com/casey/just

set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

# Default: list recipes
default:
    @just --list

# One-shot bootstrap: apt deps + JUCE submodule + uv sync + pre-commit install
bootstrap:
    bash bootstrap.sh

# Build the C++ core (Release by default; uses Ninja if available, falls back to Make)
build profile="Release":
    #!/usr/bin/env bash
    set -eu
    GEN=$(command -v ninja >/dev/null && echo "Ninja" || echo "Unix Makefiles")
    cmake -S . -B build -DCMAKE_BUILD_TYPE={{profile}} -G "${GEN}"
    cmake --build build --parallel

# Build with RT-assert + click-detector (test/CI profile)
build-test:
    #!/usr/bin/env bash
    set -eu
    GEN=$(command -v ninja >/dev/null && echo "Ninja" || echo "Unix Makefiles")
    cmake -S . -B build-test -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DSPATIAL_ENGINE_RT_ASSERTS=ON \
        -DSPATIAL_ENGINE_WERROR=ON \
        -G "${GEN}"
    cmake --build build-test --parallel

# Run unit tests via CTest (NullBackend; CI-runnable)
test:
    cd build-test && ctest --output-on-failure
    cd ui && uv run pytest -q

# Spawn core + UI for manual smoke
run:
    ./build/core/spatial_engine_core --backend null &
    cd ui && uv run python -m spatial_engine_ui --osc-cmd-port 9100 --osc-state-port 9101

# Multi-hour soak (P11)
soak hours="4":
    cd tests/soak_harness && uv run python harness.py --hours {{hours}}

# Latency harness (P10) — requires Dante hardware
measure-latency:
    cd tests/latency_harness && uv run python harness.py

# Numerical accuracy harness (P9) — CI-runnable, NullBackend
accuracy:
    cd tests/accuracy_harness && uv run python harness.py

# LayoutCompatibilityChecker red/green pair tests (P3)
compat:
    cd tests/compat_harness && uv run python harness.py

# Inspect KEMAR SOFA metadata (P0 deliverable; feeds docs/lab_setup.md)
# Override the SOFA path with: just sofa-inspect sofa=/path/to/kemar.sofa
sofa-inspect sofa="/home/seung/mmhoa/text2hoa/renderer/hrtf/kemar.sofa":
    python3 tools/sofa_inspector.py {{sofa}}

# Pre-commit hooks across all files
lint:
    pre-commit run --all-files

# Wipe build artifacts
clean:
    rm -rf build build-test
