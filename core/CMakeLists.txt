# core/CMakeLists.txt — JUCE-based native audio core.
#
# At P1 the core compiles two static libraries:
#   spe_util  — JUCE-free utilities (RT-assert override, xrun counter, trace
#               ring, click-detector header).
#   spe_core  — engine modules (audio_io, core, ...). Links spe_util; links
#               JUCE modules when SPE_HAVE_JUCE is set by the JUCE submodule
#               wiring below.
#
# When the JUCE submodule is initialized at core/JUCE/, this CMake file
# add_subdirectory()'s it and the executable links juce_audio_devices, etc.

cmake_minimum_required(VERSION 3.20)

# --- JUCE wiring ---------------------------------------------------------
# SPATIAL_ENGINE_NO_JUCE forces a JUCE-free build even when the submodule is
# present. Used by CI / RT-assert harnesses on hosts that lack JUCE's Linux
# system deps (libasound2-dev, libx11-dev, ...). NullBackend + SpatialEngine
# do not require JUCE; only DanteBackend does.
option(SPATIAL_ENGINE_NO_JUCE "Force JUCE-free build (skip DanteBackend, juce::* link)" OFF)

if(SPATIAL_ENGINE_NO_JUCE)
    set(SPE_HAVE_JUCE OFF)
    message(STATUS "spatial_engine: SPATIAL_ENGINE_NO_JUCE=ON — JUCE-free build (NullBackend only).")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
    set(SPE_HAVE_JUCE ON)
    message(STATUS "spatial_engine: JUCE submodule found, building with juce_audio_devices")
else()
    set(SPE_HAVE_JUCE OFF)
    message(STATUS "spatial_engine: JUCE submodule NOT initialized — building stub binary only.")
    message(STATUS "spatial_engine: Run 'just bootstrap' to fetch JUCE 7.x.")
endif()

# --- spe_util ------------------------------------------------------------
set(SPE_UTIL_SOURCES
    src/util/RtAssertNoAlloc.cpp
    src/util/ClickDetectorFFT.cpp
)
add_library(spe_util STATIC ${SPE_UTIL_SOURCES})
target_include_directories(spe_util PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(spe_util PUBLIC
    SPE_VERSION_MAJOR=0
    SPE_VERSION_MINOR=1
    SPE_VERSION_PATCH=0
)
if(SPATIAL_ENGINE_RT_ASSERTS)
    target_compile_definitions(spe_util PUBLIC SPE_RT_ASSERTS=1)
endif()

# --- yaml-cpp via FetchContent (used only by spe_core / LayoutLoader) ----
include(FetchContent)
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    GIT_SHALLOW    TRUE
    OVERRIDE_FIND_PACKAGE
)
# yaml-cpp 0.8.0 declares cmake_minimum_required(2.8) which CMake 4.x rejects.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# Suppress yaml-cpp's own install targets and tests.
set(YAML_CPP_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL       OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

# --- spe_core ------------------------------------------------------------
set(SPE_CORE_SOURCES
    src/audio_io/AudioBackend.cpp
    src/audio_io/NullBackend.cpp
    src/core/SpatialEngine.cpp
    src/geometry/SpeakerLayout.cpp
    src/geometry/LayoutLoader.cpp
    src/render/LayoutCompatibilityChecker.cpp
    src/render/AlgorithmAnalyticReference.cpp
    src/render/VBAPRenderer.cpp
)
if(SPE_HAVE_JUCE)
    list(APPEND SPE_CORE_SOURCES src/audio_io/DanteBackend.cpp)
endif()

add_library(spe_core STATIC ${SPE_CORE_SOURCES})
target_link_libraries(spe_core PUBLIC spe_util yaml-cpp::yaml-cpp)

if(SPE_HAVE_JUCE)
    target_link_libraries(spe_core PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_osc
    )
    target_compile_definitions(spe_core PUBLIC
        SPE_HAVE_JUCE=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
    )
endif()

# --- spatial_engine_core executable --------------------------------------
add_executable(spatial_engine_core src/bin/spatial_engine_core.cpp)
target_link_libraries(spatial_engine_core PRIVATE spe_core)

# --- CTest scaffold ------------------------------------------------------
add_subdirectory(tests/core_unit)
