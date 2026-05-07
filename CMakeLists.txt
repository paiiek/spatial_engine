cmake_minimum_required(VERSION 3.20)
project(spatial_engine
    VERSION 0.1.0
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

# VST3 plugin (Option B — direct hand-roll, no juce_add_plugin).
# OFF by default to preserve byte-identical core artifacts (Principle 5).
option(SPATIAL_ENGINE_VST3 "Build VST3 plugin (Option B hand-roll)" OFF)

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
