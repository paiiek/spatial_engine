# vst3/CMakeLists.txt — Phase C C2 Option-B
# Direct add_library(MODULE) — juce_add_plugin NOT used (X11 sysdep avoidance).
# Enable: cmake -B build -DSPATIAL_ENGINE_VST3=ON
# Default OFF preserves byte-identical core artifacts (Principle 5 dual-gate).

cmake_minimum_required(VERSION 3.20)

set(VST3_SDK_ROOT "${CMAKE_SOURCE_DIR}/core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK")

# Verify the SDK is present
if(NOT EXISTS "${VST3_SDK_ROOT}/pluginterfaces/base/funknown.h")
    message(FATAL_ERROR "VST3 SDK not found at ${VST3_SDK_ROOT}. Run scripts/bootstrap_juce.sh first.")
endif()

# ---- Plugin shared library --------------------------------------------------
add_library(spatial_engine_vst3 MODULE
    # Our sources
    SpatialEnginePluginFactory.cpp
    SpatialEngineProcessor.cpp
    SpatialEngineController.cpp

    # SDK class IID definitions — vstinitiids.cpp emits ClassName::iid via
    # DEF_CLASS_IID. Compiled WITHOUT INIT_CLASS_IID (SDK standard pattern):
    # the header's DECLARE_CLASS_IID emits only the constexpr TUID xxx_iid
    # (ODR-safe), and DEF_CLASS_IID in this TU emits the FUID definition once.
    "${VST3_SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp"

    # SDK base layer — FUnknown impl, FUnknownPrivate::atomicAdd, base IIDs
    # (FUnknown / IBStream). Required by memorystream.cpp's queryInterface.
    # BSD-3 file-level header verified.
    "${VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp"
    "${VST3_SDK_ROOT}/pluginterfaces/base/coreiids.cpp"

    # State IO helpers — used from Step 3 onward; included now so the
    # translation units are compiled and symbol table is stable.
    "${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp"
    "${VST3_SDK_ROOT}/public.sdk/source/common/readfile.cpp"
)

target_include_directories(spatial_engine_vst3 PRIVATE
    "${VST3_SDK_ROOT}"
    "${CMAKE_SOURCE_DIR}/core/src"
)

target_link_libraries(spatial_engine_vst3 PRIVATE spe_core)

target_compile_definitions(spatial_engine_vst3 PRIVATE
    SPATIAL_ENGINE_VST3_BUILD=1
)


set_target_properties(spatial_engine_vst3 PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "spatial_engine_vst3"
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ---- Tests ------------------------------------------------------------------
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
