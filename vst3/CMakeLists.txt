# vst3/CMakeLists.txt — Phase B placeholder VST3 plugin target.
#
# Phase B goal: produce a build-system entry point so future Phase C work
# can plug in the official Steinberg vst3sdk without restructuring CMake.
# The current target builds a minimal shared library with the standard VST3
# bundle entry symbols (GetPluginFactory / ModuleEntry / ModuleExit) — they
# return inert values so a real host will cleanly skip the plugin instead
# of crashing.
#
# Enable with: cmake .. -DSPATIAL_ENGINE_VST3=ON
# Disable (default): the rest of the build is unaffected.
#
# Phase C TODO: clone https://github.com/steinbergmedia/vst3sdk and pass
#   -DVST3_SDK_DIR=/path/to/vst3sdk to inherit the proper IPluginFactory,
#   AudioEffect, and AudioProcessor base classes.

add_library(spatial_engine_vst3 SHARED SpatialEngineVST3.cpp)
target_link_libraries(spatial_engine_vst3 PRIVATE spe_core)
target_compile_definitions(spatial_engine_vst3 PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
set_target_properties(spatial_engine_vst3 PROPERTIES
    PREFIX ""
    SUFFIX ".vst3"
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(VST3_SDK_DIR)
    if(EXISTS "${VST3_SDK_DIR}/CMakeLists.txt")
        message(STATUS "spatial_engine_vst3: VST3_SDK_DIR=${VST3_SDK_DIR} detected — Phase C will wire full plugin classes.")
        target_include_directories(spatial_engine_vst3 PRIVATE "${VST3_SDK_DIR}")
    else()
        message(WARNING "spatial_engine_vst3: VST3_SDK_DIR=${VST3_SDK_DIR} not a valid SDK root.")
    endif()
endif()
