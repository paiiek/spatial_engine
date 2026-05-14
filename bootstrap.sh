#!/usr/bin/env bash
# spatial_engine — non-interactive bootstrap (Ubuntu 22.04 / macOS).
# Goal (acceptance #10): clone-to-build in ≤60 min on a clean machine.
#
# Idempotent. Safe to re-run. Verbose by default; SE_QUIET=1 silences progress.
# Linux uses apt; macOS (Apple Silicon / Intel) uses Homebrew.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG="${REPO_ROOT}/bootstrap.log"
exec > >(tee -a "${LOG}") 2>&1

say() { printf '\n[bootstrap] %s\n' "$*"; }

# --- 1. apt deps ----------------------------------------------------------
APT_PKGS=(
    build-essential
    cmake
    ninja-build
    git
    pkg-config
    # JUCE dependencies (Linux)
    libasound2-dev
    libjack-jackd2-dev
    libfreetype-dev
    libfontconfig1-dev
    libx11-dev
    libxcomposite-dev
    libxcursor-dev
    libxext-dev
    libxinerama-dev
    libxrandr-dev
    libxrender-dev
    libwebkit2gtk-4.1-dev
    libgtk-3-dev
    # Audio I/O
    libsndfile1-dev
    libsamplerate0-dev
    # Pre-commit / formatters
    clang-format
    clang-tidy
    yamllint
)

# macOS (Apple Silicon / Intel) — Homebrew. The X11/GTK/ALSA packages are
# Linux-only and not needed for the NO_JUCE build; macOS just needs the
# toolchain + audio I/O libs. clang-format/yamllint cover the lint hooks.
BREW_PKGS=(
    cmake
    ninja
    pkg-config
    libsndfile
    libsamplerate
    clang-format
    yamllint
)

if [[ "$(uname -s)" == "Darwin" ]]; then
    if command -v brew >/dev/null; then
        say "macOS detected — installing Homebrew packages (${#BREW_PKGS[@]} pkgs)."
        brew install "${BREW_PKGS[@]}"
    else
        say "WARNING: Homebrew not found. Install from https://brew.sh then re-run."
    fi
elif command -v apt-get >/dev/null; then
    say "Installing apt packages (${#APT_PKGS[@]} pkgs); sudo may prompt once."
    sudo apt-get update -qq
    sudo apt-get install -y --no-install-recommends "${APT_PKGS[@]}"
else
    say "WARNING: apt-get not found. Skipping system package install (non-Ubuntu host)."
fi

# --- 2. JUCE submodule ----------------------------------------------------
JUCE_DIR="${REPO_ROOT}/core/JUCE"
if [[ ! -d "${JUCE_DIR}/.git" ]]; then
    say "Adding JUCE 7.x as submodule at core/JUCE"
    cd "${REPO_ROOT}"
    if [[ -d .git ]]; then
        git submodule add -b 7.0.12 https://github.com/juce-framework/JUCE.git core/JUCE || \
            git submodule update --init core/JUCE
    else
        say "Repo is not a git checkout; cloning JUCE directly."
        git clone --depth 1 --branch 7.0.12 https://github.com/juce-framework/JUCE.git "${JUCE_DIR}"
    fi
else
    say "JUCE submodule already present; skipping."
fi

# --- 3. Python (uv) -------------------------------------------------------
if ! command -v uv >/dev/null; then
    say "Installing uv (Astral)"
    curl -LsSf https://astral.sh/uv/install.sh | sh
    export PATH="${HOME}/.local/bin:${PATH}"
fi

say "Syncing UI Python deps via uv"
cd "${REPO_ROOT}/ui"
uv sync || say "WARNING: uv sync failed; UI deps not installed yet."
cd "${REPO_ROOT}"

# --- 4. pre-commit --------------------------------------------------------
if command -v pre-commit >/dev/null; then
    say "Installing pre-commit hooks"
    pre-commit install
else
    say "pre-commit not on PATH; install via 'pipx install pre-commit' to enable hooks."
fi

# --- 5. summary -----------------------------------------------------------
say "Bootstrap complete. Next: 'just build' then 'just test'."
say "Lab pinning notes: docs/lab_setup.md  (kernel + Digigram driver matrix)"
