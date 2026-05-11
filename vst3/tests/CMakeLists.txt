# vst3/tests/CMakeLists.txt — Phase C C2 Step 1 test targets
# In-process host fixture (no dlopen — links plugin objects directly).

cmake_minimum_required(VERSION 3.20)

set(VST3_SDK_ROOT "${CMAKE_SOURCE_DIR}/core/JUCE/modules/juce_audio_processors/format_types/VST3_SDK")

set(PLUGIN_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/../SpatialEngineProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SpatialEngineController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SpatialEnginePluginFactory.cpp"
    "${VST3_SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp"
    "${VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp"
    "${VST3_SDK_ROOT}/pluginterfaces/base/coreiids.cpp"
    "${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp"
    "${VST3_SDK_ROOT}/public.sdk/source/common/readfile.cpp"
)

set(VST3_TEST_INCLUDES
    "${VST3_SDK_ROOT}"
    "${CMAKE_SOURCE_DIR}/core/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

# ---- test_vst3_host_fixture -------------------------------------------------
add_executable(test_vst3_host_fixture
    test_vst3_host_fixture.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_host_fixture PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_host_fixture PRIVATE spe_core)
target_compile_definitions(test_vst3_host_fixture PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_host_fixture PRIVATE -UNDEBUG)
add_test(NAME vst3_host_fixture COMMAND test_vst3_host_fixture)

# ---- test_vst3_audio_smoke --------------------------------------------------
add_executable(test_vst3_audio_smoke
    test_vst3_audio_smoke.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_audio_smoke PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_audio_smoke PRIVATE spe_core)
target_compile_definitions(test_vst3_audio_smoke PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_audio_smoke PRIVATE -UNDEBUG)
add_test(NAME vst3_audio_smoke COMMAND test_vst3_audio_smoke)

# ---- test_vst3_param_layout (Step 2.6 — 10 assertions) ---------------------
add_executable(test_vst3_param_layout
    test_vst3_param_layout.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_param_layout PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_param_layout PRIVATE spe_core)
target_compile_definitions(test_vst3_param_layout PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_param_layout PRIVATE -UNDEBUG)
add_test(NAME vst3_param_layout COMMAND test_vst3_param_layout)

# ---- test_vst3_param_roundtrip (Step 2.6 — 24 assertions) ------------------
add_executable(test_vst3_param_roundtrip
    test_vst3_param_roundtrip.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_param_roundtrip PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_param_roundtrip PRIVATE spe_core)
target_compile_definitions(test_vst3_param_roundtrip PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_param_roundtrip PRIVATE -UNDEBUG)
add_test(NAME vst3_param_roundtrip COMMAND test_vst3_param_roundtrip)

# ---- test_vst3_dispatch_rt_safety (Step 2.6 — 1 alloc assertion) -----------
add_executable(test_vst3_dispatch_rt_safety
    test_vst3_dispatch_rt_safety.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_dispatch_rt_safety PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_dispatch_rt_safety PRIVATE spe_core)
target_compile_definitions(test_vst3_dispatch_rt_safety PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_dispatch_rt_safety PRIVATE -UNDEBUG)
add_test(NAME vst3_dispatch_rt_safety COMMAND test_vst3_dispatch_rt_safety)

# ---- test_vst3_bypass (Step 3.4 — 7 assertions: 6 base + 1 RT-safety) -----
add_executable(test_vst3_bypass
    test_vst3_bypass.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_bypass PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_bypass PRIVATE spe_core)
target_compile_definitions(test_vst3_bypass PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_bypass PRIVATE -UNDEBUG)
add_test(NAME vst3_bypass COMMAND test_vst3_bypass)

# ---- test_vst3_state_persist (Step 3.4 — 12 assertions) -------------------
add_executable(test_vst3_state_persist
    test_vst3_state_persist.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_state_persist PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_state_persist PRIVATE spe_core)
target_compile_definitions(test_vst3_state_persist PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_state_persist PRIVATE -UNDEBUG)
add_test(NAME vst3_state_persist COMMAND test_vst3_state_persist)

# ---- test_p_instances_registry (A.5) ----------------------------------------
add_executable(test_p_instances_registry
    test_p_instances_registry.cpp
    ../osc/PluginInstanceRegistry.cpp
)
target_include_directories(test_p_instances_registry PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_compile_options(test_p_instances_registry PRIVATE -UNDEBUG)
add_test(NAME p_instances_registry COMMAND test_p_instances_registry)

# ---- test_p_instances_registry_corruption (A.14) ----------------------------
add_executable(test_p_instances_registry_corruption
    test_p_instances_registry_corruption.cpp
    ../osc/PluginInstanceRegistry.cpp
)
target_include_directories(test_p_instances_registry_corruption PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_compile_options(test_p_instances_registry_corruption PRIVATE -UNDEBUG)
add_test(NAME p_instances_registry_corruption COMMAND test_p_instances_registry_corruption)

# ---- test_vst3_bind_collision (A.13) ----------------------------------------
add_executable(test_vst3_bind_collision
    test_vst3_bind_collision.cpp
    ../SpatialEnginePluginUdp.cpp
    ../osc/PluginInstanceRegistry.cpp
)
target_include_directories(test_vst3_bind_collision PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(test_vst3_bind_collision PRIVATE spe_core)
target_compile_definitions(test_vst3_bind_collision PRIVATE SPATIAL_ENGINE_VST3_OSC=1)
target_compile_options(test_vst3_bind_collision PRIVATE -UNDEBUG)
add_test(NAME vst3_bind_collision COMMAND test_vst3_bind_collision)

# ---- test_vst3_state_v3_reader_only (A.15a — D3-γ early-reader) -------------
add_executable(test_vst3_state_v3_reader_only
    test_vst3_state_v3_reader_only.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_state_v3_reader_only PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_state_v3_reader_only PRIVATE spe_core)
target_compile_definitions(test_vst3_state_v3_reader_only PRIVATE
    SPATIAL_ENGINE_VST3_BUILD=1
    CMAKE_CURRENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_options(test_vst3_state_v3_reader_only PRIVATE -UNDEBUG)
add_test(NAME vst3_state_v3_reader_only COMMAND test_vst3_state_v3_reader_only)

# ---- test_vst3_performedit_threadsafe (S2.6 — 1000-iter strategy-a harness) --
add_executable(test_vst3_performedit_threadsafe
    test_vst3_performedit_threadsafe.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_performedit_threadsafe PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_performedit_threadsafe PRIVATE spe_core)
target_compile_definitions(test_vst3_performedit_threadsafe PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_performedit_threadsafe PRIVATE -UNDEBUG)
add_test(NAME vst3_performedit_threadsafe COMMAND test_vst3_performedit_threadsafe)

# ---- ctest dependency chain (AM-R3-9) --------------------------------------
set_tests_properties(vst3_param_layout vst3_param_roundtrip vst3_dispatch_rt_safety
                     vst3_bypass vst3_state_persist vst3_state_v3_reader_only
                     vst3_performedit_threadsafe
                     PROPERTIES DEPENDS vst3_host_fixture)
