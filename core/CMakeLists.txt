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
project(spatial_engine_core CXX)

set(CMAKE_CXX_STANDARD 20)
enable_testing()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Threads — required by NullBackend's std::thread worker and the C1.b
# SpscRing two-thread roundtrip test. find_package(Threads) materialises
# Threads::Threads which carries the right -pthread / -lpthread flag.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# --- JUCE wiring ---------------------------------------------------------
# SPATIAL_ENGINE_NO_JUCE forces a JUCE-free build even when the submodule is
# present. Used by CI / RT-assert harnesses on hosts that lack JUCE's Linux
# system deps (libasound2-dev, libx11-dev, ...). NullBackend + SpatialEngine
# do not require JUCE; only DanteBackend does.
option(SPATIAL_ENGINE_NO_JUCE "Force JUCE-free build (skip DanteBackend, juce::* link)" OFF)
option(SPATIAL_ENGINE_VST3    "Build optional VST3 plugin scaffold (Phase C will integrate Steinberg vst3sdk)" OFF)
# D-S4: relacy race-detector dev-dep (header-only). Default OFF.
# When ON, builds test_osc_outbound_relacy (ctest +1) using third_party/relacy.
# Production code never includes relacy headers — include path is isolated to
# the new ctest target. Use build_relacy/ as a dedicated build dir:
#   mkdir build_relacy && cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_BUILD_RELACY_TESTS=ON
option(SPATIAL_ENGINE_BUILD_RELACY_TESTS "Build optional relacy race-detector ctest (dev-dep, header-only)" OFF)

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
target_link_libraries(spe_util PUBLIC Threads::Threads)
target_compile_definitions(spe_util PUBLIC
    SPE_VERSION_MAJOR=0
    SPE_VERSION_MINOR=2
    SPE_VERSION_PATCH=0
)
if(SPATIAL_ENGINE_RT_ASSERTS)
    target_compile_definitions(spe_util PUBLIC SPE_RT_ASSERTS=1)
endif()
# v0.9 Lane C — compile-time object cap. PUBLIC on spe_util so it reaches every
# Constants.h consumer transitively (spe_core links spe_util PUBLIC). Defaults
# to 64 via the cache var in the top-level CMakeLists; bare builds stay
# byte-identical. SPATIAL_ENGINE_MAX_OBJECTS may also be passed directly when
# configuring core/ standalone.
if(NOT DEFINED SPATIAL_ENGINE_MAX_OBJECTS)
    set(SPATIAL_ENGINE_MAX_OBJECTS 64)
endif()
if(NOT SPATIAL_ENGINE_MAX_OBJECTS STREQUAL "64" AND NOT SPATIAL_ENGINE_MAX_OBJECTS STREQUAL "128")
    message(FATAL_ERROR "SPATIAL_ENGINE_MAX_OBJECTS must be 64 or 128 (got '${SPATIAL_ENGINE_MAX_OBJECTS}')")
endif()
target_compile_definitions(spe_util PUBLIC SPE_MAX_OBJECTS=${SPATIAL_ENGINE_MAX_OBJECTS})

# Phase 0.5 (128 lift) — speaker (output-channel) cap, same plumbing/contract.
if(NOT DEFINED SPATIAL_ENGINE_MAX_SPEAKERS)
    set(SPATIAL_ENGINE_MAX_SPEAKERS 64)
endif()
if(NOT SPATIAL_ENGINE_MAX_SPEAKERS STREQUAL "64" AND NOT SPATIAL_ENGINE_MAX_SPEAKERS STREQUAL "128")
    message(FATAL_ERROR "SPATIAL_ENGINE_MAX_SPEAKERS must be 64 or 128 (got '${SPATIAL_ENGINE_MAX_SPEAKERS}')")
endif()
target_compile_definitions(spe_util PUBLIC SPE_MAX_SPEAKERS=${SPATIAL_ENGINE_MAX_SPEAKERS})

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
    src/render/DBAPRenderer.cpp
    src/render/WFSRenderer.cpp
    src/render/AmbisonicRenderer.cpp
    # Dreamscape convergence — ported panning kernels (namespace iae, juce-free)
    src/render/ported/Vbap.cpp
    src/render/ported/Vap.cpp
    src/render/ported/VbapMask.cpp
    src/render/ported/Wfs.cpp
    src/render/ported/RoomFdn.cpp
    src/render/ported/RoomEarly.cpp
    src/render/ported/RoomCluster.cpp
    src/render/VAPRenderer.cpp
    # P4: IPC layer
    src/ipc/ProtocolVersion.cpp
    src/ipc/CommandDecoder.cpp
    src/ipc/StateModel.cpp
    src/ipc/HeartbeatPublisher.cpp
    src/ipc/HeartbeatMonitor.cpp
    src/ipc/OSCBackend.cpp
    src/ipc/EchoSubscriber.cpp
    src/ipc/VST3ControlStub.cpp
    # US-001: Scene snapshot serialisation + controller
    src/ipc/SceneSnapshot.cpp
    src/ipc/SceneController.cpp
    # M4: Time-based scene crossfade
    src/scene/SceneCrossfade.cpp
    # E-M2: Cue list data model + serialisation
    src/scene/CueList.cpp
    # E-M3: Cue firing engine + dwell auto-advance
    src/scene/CueEngine.cpp
    # M7: SMPTE LTC biphase decoder
    src/sync/LTCDecoder.cpp
    # C1.c: control-rate LTC consumer (audio→ring→control-thread decoder)
    src/sync/LtcChase.cpp
    # P5: input chain
    src/input/FileInput.cpp
    src/input/SynthInput.cpp
    # P6: live mic input (silence fallback in NO_JUCE builds)
    src/input/LiveMicInput.cpp
    # P6: DanteBackend stub API (always compiled; JUCE-only class guarded internally)
    src/audio_io/DanteBackend.cpp
    # P7: reverb
    src/reverb/FdnReverb.cpp
    src/reverb/IRConvolutionStub.cpp
    src/reverb/IRConvReverb.cpp
    src/reverb/ReverbEngine.cpp
    # P9 / v1c: BinauralMonitor + pure-C++ HRTF pipeline
    src/output_backend/BinauralMonitor.cpp
    src/hrtf/SofaBinReader.cpp
    src/hrtf/HrtfLookup.cpp
    src/hrtf/OlaConvolver.cpp
    src/hrtf/KdTree3D.cpp
    src/hrtf/HrtfCatalog.cpp
    # F2: Ambisonics 1st-order encoder + M2 decoder
    src/ambi/AmbisonicEncoder.cpp
    src/ambi/AmbiDecoder.cpp
    # ADR 0019 PR1: POSIX shm region (Linux + macOS only)
    $<$<BOOL:${UNIX}>:src/audio_io/shm/SharedMemoryRegion.cpp>
    # ADR 0019 PR2: input-only shared-ring backend (Linux + macOS only)
    $<$<BOOL:${UNIX}>:src/audio_io/SharedRingBackend.cpp>
    # M2-HOA: 4 new decoder algorithms (HOA Decoder Diversification sprint)
    src/ambi/MaxREDecoder.cpp
    src/ambi/InPhaseDecoder.cpp
    src/ambi/EPADDecoder.cpp
    src/ambi/AllRADDecoder.cpp
    src/ambi/AllRADTDesigns.cpp
)

add_library(spe_core STATIC ${SPE_CORE_SOURCES})
target_link_libraries(spe_core PUBLIC spe_util yaml-cpp::yaml-cpp)
# shm_open / shm_unlink live in librt on Linux; nothing extra needed on macOS.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(spe_core PUBLIC rt)
endif()

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
add_executable(spatial_engine_core
    src/bin/spatial_engine_core.cpp
    src/bin/WavWriter.cpp)
target_link_libraries(spatial_engine_core PRIVATE spe_core)

# VST3 plugin is now added from the top-level CMakeLists.txt (Phase C C2 Option-B).
# Removed from here to prevent duplicate target registration.

# --- CTest scaffold ------------------------------------------------------
add_subdirectory(tests/core_unit)
add_subdirectory(tests/perf)

# --- relacy race-detector ctest (optional, D-S4) -------------------------
# Isolated behind SPATIAL_ENGINE_BUILD_RELACY_TESTS to avoid polluting the
# default build with relacy's instrumented std::atomic replacements.
if(SPATIAL_ENGINE_BUILD_RELACY_TESTS)
    add_executable(test_osc_outbound_relacy
        tests/relacy/test_osc_outbound_relacy.cpp)
    target_include_directories(test_osc_outbound_relacy PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/relacy")
    target_compile_options(test_osc_outbound_relacy PRIVATE
        -UNDEBUG
        -std=c++20)
    # relacy headers use POSIX threading internally
    target_link_libraries(test_osc_outbound_relacy PRIVATE Threads::Threads)
    add_test(NAME osc_outbound_relacy COMMAND test_osc_outbound_relacy)
    message(STATUS "spatial_engine: SPATIAL_ENGINE_BUILD_RELACY_TESTS=ON — "
                   "test_osc_outbound_relacy registered as ctest.")

    # v0.9 Lane B (B-M2) — SOFA hot-swap race detector. Models the HrtfLookup
    # 2-slot table/tree double buffer + the single-slot B2 VS-bank
    # vs_rebuild_in_progress_ handshake. Non-vacuous (>=2 publishes/block,
    # dereferences slot DATA) with two #ifdef-toggleable negative controls
    # (NEG_REMOVE_CADENCE_GUARD, NEG_REMOVE_FORCE_DIRECT).
    add_executable(test_hrtf_sofa_swap_race
        tests/relacy/test_hrtf_sofa_swap_race.cpp)
    target_include_directories(test_hrtf_sofa_swap_race PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/relacy")
    target_compile_options(test_hrtf_sofa_swap_race PRIVATE
        -UNDEBUG
        -std=c++20)
    target_link_libraries(test_hrtf_sofa_swap_race PRIVATE Threads::Threads)
    add_test(NAME hrtf_sofa_swap_race COMMAND test_hrtf_sofa_swap_race)
    message(STATUS "spatial_engine: test_hrtf_sofa_swap_race registered as ctest.")
endif()
