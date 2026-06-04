cmake_minimum_required(VERSION 3.20)
project(spatial_engine
    VERSION 0.2.0
    DESCRIPTION "Real-time object-based immersive audio rendering engine"
    LANGUAGES CXX)

# Workspace-level settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# macOS / Apple Silicon: pin the target arch to the host so the build never
# silently falls back to x86_64 under Rosetta. arm64 on Apple Silicon hosts,
# x86_64 on Intel Macs. Override with -DCMAKE_OSX_ARCHITECTURES=... if needed
# (e.g. "arm64;x86_64" for a universal binary). Apple-only — does not touch
# the Linux byte-baseline (scripts/verify_byte_baseline.py).
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}"
        CACHE STRING "macOS target architecture" FORCE)
    message(STATUS "spatial_engine: macOS arch pinned to ${CMAKE_OSX_ARCHITECTURES}")
endif()

# Baseline diagnostic flags
# (RTTI stays ON: JUCE 7.x relies on dynamic_cast in several modules.)
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# Warnings-as-errors only on dedicated profile (CI gate)
option(SPATIAL_ENGINE_WERROR "Treat warnings as errors" OFF)
if(SPATIAL_ENGINE_WERROR)
    add_compile_options(-Werror)
endif()

# Test-build-only RT-asserts and click-detector
option(SPATIAL_ENGINE_RT_ASSERTS "Enable RT_ASSERT_NO_ALLOC and ClickDetectorFFT in test build" OFF)

# v0.9 Lane C — compile-time max simultaneous spatial objects (64 or 128).
# Default STAYS 64 (Option A): a bare cmake .. is byte-identical to today.
# Propagated to spe_util (PUBLIC) as SPE_MAX_OBJECTS so every Constants.h
# consumer sees the configured value. Allowed values: {64, 128}.
set(SPATIAL_ENGINE_MAX_OBJECTS 64 CACHE STRING "Max simultaneous spatial objects (64 or 128)")
if(NOT SPATIAL_ENGINE_MAX_OBJECTS STREQUAL "64" AND NOT SPATIAL_ENGINE_MAX_OBJECTS STREQUAL "128")
    message(FATAL_ERROR "SPATIAL_ENGINE_MAX_OBJECTS must be 64 or 128 (got '${SPATIAL_ENGINE_MAX_OBJECTS}')")
endif()

# Phase 0.5 (128 lift) — compile-time max speaker/output channels (64 or 128).
# Same Option-A contract as MAX_OBJECTS: default STAYS 64 so a bare build is
# byte-identical. Propagated to spe_util (PUBLIC) as SPE_MAX_SPEAKERS.
set(SPATIAL_ENGINE_MAX_SPEAKERS 64 CACHE STRING "Max speaker output channels (64 or 128)")
if(NOT SPATIAL_ENGINE_MAX_SPEAKERS STREQUAL "64" AND NOT SPATIAL_ENGINE_MAX_SPEAKERS STREQUAL "128")
    message(FATAL_ERROR "SPATIAL_ENGINE_MAX_SPEAKERS must be 64 or 128 (got '${SPATIAL_ENGINE_MAX_SPEAKERS}')")
endif()

# VST3 plugin (Option B — direct hand-roll, no juce_add_plugin).
# OFF by default to preserve byte-identical core artifacts (Principle 5).
option(SPATIAL_ENGINE_VST3 "Build VST3 plugin (Option B hand-roll)" OFF)

# Per-instance UDP OSC recv inside the plugin process (ADR 0010 A1-ε / ADR 0011).
# Default OFF — OFF byte-baseline must remain identical to pre-S2 artifacts.
option(SPATIAL_ENGINE_VST3_OSC "Enable per-instance UDP OSC recv in plugin" OFF)

# Option B requires JUCE-free build: JUCE's juce_audio_devices/juce_gui_extra
# pull X11/freetype/alsa transitive deps via juceaide, defeating sysdep-free
# (Principle 1, §6 Step 1.1, §16.1 D+2 trigger). Force NO_JUCE before
# add_subdirectory(core) so core/CMakeLists.txt skips add_subdirectory(JUCE).
if(SPATIAL_ENGINE_VST3)
    set(SPATIAL_ENGINE_NO_JUCE ON CACHE BOOL "Auto-forced by SPATIAL_ENGINE_VST3" FORCE)
    # Plugin .so (MODULE) requires PIC across all archive deps (spe_core / spe_util).
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

enable_testing()
add_subdirectory(core)

if(SPATIAL_ENGINE_VST3)
    add_subdirectory(vst3)
endif()
