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

# ---- test_vst3_intra_plugin_spsc_drain (S3 A.6) ----------------------------
# rt_alloc_probe.hpp requires no LTO (R10 Layer 3); -fno-lto is set explicitly.
add_executable(test_vst3_intra_plugin_spsc_drain
    test_vst3_intra_plugin_spsc_drain.cpp
)
target_include_directories(test_vst3_intra_plugin_spsc_drain PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_compile_options(test_vst3_intra_plugin_spsc_drain PRIVATE -UNDEBUG -fno-lto)
add_test(NAME vst3_intra_plugin_spsc_drain COMMAND test_vst3_intra_plugin_spsc_drain)

# ---- test_vst3_spsc_ring_overrun (S3 overrun policy) -----------------------
add_executable(test_vst3_spsc_ring_overrun
    test_vst3_spsc_ring_overrun.cpp
)
target_include_directories(test_vst3_spsc_ring_overrun PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_compile_options(test_vst3_spsc_ring_overrun PRIVATE -UNDEBUG)
add_test(NAME vst3_spsc_ring_overrun COMMAND test_vst3_spsc_ring_overrun)

# ---- test_vst3_no_feedback_loop (S4 A.7 forward-loop guard) ----------------
add_executable(test_vst3_no_feedback_loop
    test_vst3_no_feedback_loop.cpp
    ../SpatialEnginePluginUdp.cpp
    ../osc/PluginInstanceRegistry.cpp
)
target_include_directories(test_vst3_no_feedback_loop PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(test_vst3_no_feedback_loop PRIVATE spe_core)
target_compile_definitions(test_vst3_no_feedback_loop PRIVATE SPATIAL_ENGINE_VST3_OSC=1)
target_compile_options(test_vst3_no_feedback_loop PRIVATE -UNDEBUG)
add_test(NAME vst3_no_feedback_loop COMMAND test_vst3_no_feedback_loop)

# ---- test_vst3_reverse_path (S4 A.7 reverse-path latency — short 5s) ------
# Full 60s soak deferred to S6 (SPATIAL_ENGINE_SOAK=ON env var).
# Marked LONG_TEST label; default ctest runs it with 30s timeout.
add_executable(test_vst3_reverse_path
    test_vst3_reverse_path.cpp
    ${PLUGIN_SRCS}
    ../SpatialEnginePluginUdp.cpp
    ../osc/PluginInstanceRegistry.cpp
)
target_include_directories(test_vst3_reverse_path PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_reverse_path PRIVATE spe_core)
target_compile_definitions(test_vst3_reverse_path PRIVATE
    SPATIAL_ENGINE_VST3_BUILD=1
    SPATIAL_ENGINE_VST3_OSC=1
)
target_compile_options(test_vst3_reverse_path PRIVATE -UNDEBUG)
add_test(NAME vst3_reverse_path COMMAND test_vst3_reverse_path)
set_tests_properties(vst3_reverse_path PROPERTIES TIMEOUT 30 LABELS "LONG_TEST")

# ---- test_vst3_e2e_console_to_plugin (S6 A.9 — reverse-path E2E soak) ------
# Default run: 5s mini-soak (completes inside 30s ctest timeout).
# Full 60s soak: SPATIAL_ENGINE_SOAK=ON ctest -R e2e_console_to_plugin -V
add_executable(test_vst3_e2e_console_to_plugin
    test_vst3_e2e_console_to_plugin.cpp
    ${PLUGIN_SRCS}
    ../SpatialEnginePluginUdp.cpp
    ../osc/PluginInstanceRegistry.cpp
)
target_include_directories(test_vst3_e2e_console_to_plugin PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_e2e_console_to_plugin PRIVATE spe_core)
target_compile_definitions(test_vst3_e2e_console_to_plugin PRIVATE
    SPATIAL_ENGINE_VST3_BUILD=1
    SPATIAL_ENGINE_VST3_OSC=1
)
target_compile_options(test_vst3_e2e_console_to_plugin PRIVATE -UNDEBUG)
add_test(NAME vst3_e2e_console_to_plugin COMMAND test_vst3_e2e_console_to_plugin)
set_tests_properties(vst3_e2e_console_to_plugin PROPERTIES
    TIMEOUT 30
    LABELS "perf"
)

# ---- soak_vst3_console_flood (S6 A.10 — forward-path 8-instance soak) -----
# Default run: 5s mini-soak. Full 60s: SPATIAL_ENGINE_SOAK=ON ctest -R vst3_console_flood -V
# rt_alloc_probe.hpp requires -fno-lto (R10 Layer 3).
add_executable(soak_vst3_console_flood
    soak_vst3_console_flood.cpp
    ${PLUGIN_SRCS}
    ../SpatialEnginePluginUdp.cpp
    ../osc/PluginInstanceRegistry.cpp
)
target_include_directories(soak_vst3_console_flood PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(soak_vst3_console_flood PRIVATE spe_core)
target_compile_definitions(soak_vst3_console_flood PRIVATE
    SPATIAL_ENGINE_VST3_BUILD=1
    SPATIAL_ENGINE_VST3_OSC=1
)
target_compile_options(soak_vst3_console_flood PRIVATE -UNDEBUG -fno-lto)
add_test(NAME vst3_console_flood COMMAND soak_vst3_console_flood)
set_tests_properties(vst3_console_flood PROPERTIES
    TIMEOUT 30
    LABELS "perf"
)

# ---- test_vst3_state_v3_persist (A.15b — C4-S7: writer + kMute=7) -----------
add_executable(test_vst3_state_v3_persist
    test_vst3_state_v3_persist.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_state_v3_persist PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_state_v3_persist PRIVATE spe_core)
target_compile_definitions(test_vst3_state_v3_persist PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_state_v3_persist PRIVATE -UNDEBUG)
add_test(NAME vst3_state_v3_persist COMMAND test_vst3_state_v3_persist)

# ---- v0.4 P0: state v4 sectioned TLV round-trip ----------------------------
add_executable(test_vst3_state_v4_persist
    test_vst3_state_v4_persist.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_state_v4_persist PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_state_v4_persist PRIVATE spe_core)
target_compile_definitions(test_vst3_state_v4_persist PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_state_v4_persist PRIVATE -UNDEBUG)
add_test(NAME vst3_state_v4_persist COMMAND test_vst3_state_v4_persist)

# ---- v0.5 P5: state v4 binaural_state (0x0004) extended payload ------------
add_executable(test_vst3_state_v4_binaural_section
    test_vst3_state_v4_binaural_section.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_state_v4_binaural_section PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_state_v4_binaural_section PRIVATE spe_core)
target_compile_definitions(test_vst3_state_v4_binaural_section PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_state_v4_binaural_section PRIVATE -UNDEBUG)
add_test(NAME vst3_state_v4_binaural_section COMMAND test_vst3_state_v4_binaural_section)

# ---- v0.4 P0 MERGE GATE: v3 loads byte-identical under v4 writer -----------
add_executable(test_state_v3_loads_byte_identical_under_v4_writer
    test_state_v3_loads_byte_identical_under_v4_writer.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_state_v3_loads_byte_identical_under_v4_writer
    PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_state_v3_loads_byte_identical_under_v4_writer
    PRIVATE spe_core)
target_compile_definitions(test_state_v3_loads_byte_identical_under_v4_writer
    PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_state_v3_loads_byte_identical_under_v4_writer
    PRIVATE -UNDEBUG)
add_test(NAME vst3_state_v3_to_v4_byte_identical
    COMMAND test_state_v3_loads_byte_identical_under_v4_writer)

# ---- v0.4 P0: v3 reader fall-through via v4-capable reader -----------------
add_executable(test_vst3_state_v3_back_compat
    test_vst3_state_v3_back_compat.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_state_v3_back_compat PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_state_v3_back_compat PRIVATE spe_core)
target_compile_definitions(test_vst3_state_v3_back_compat PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_state_v3_back_compat PRIVATE -UNDEBUG)
add_test(NAME vst3_state_v3_back_compat COMMAND test_vst3_state_v3_back_compat)

# ---- v0.4 P1: two-bus speaker + binaural arrangement negotiation ----------
add_executable(test_vst3_bus_arrangement_speaker_binaural
    test_vst3_bus_arrangement_speaker_binaural.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_bus_arrangement_speaker_binaural PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_bus_arrangement_speaker_binaural PRIVATE spe_core)
target_compile_definitions(test_vst3_bus_arrangement_speaker_binaural PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_bus_arrangement_speaker_binaural PRIVATE -UNDEBUG)
add_test(NAME vst3_bus_arrangement_speaker_binaural
    COMMAND test_vst3_bus_arrangement_speaker_binaural)

# ---- v0.4 P1: A7 -6 dB binaural-bus downmix placeholder --------------------
add_executable(test_v04_binaural_bus_placeholder
    test_v04_binaural_bus_placeholder.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_v04_binaural_bus_placeholder PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_v04_binaural_bus_placeholder PRIVATE spe_core)
target_compile_definitions(test_v04_binaural_bus_placeholder PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_v04_binaural_bus_placeholder PRIVATE -UNDEBUG)
add_test(NAME v04_binaural_bus_placeholder COMMAND test_v04_binaural_bus_placeholder)

# ---- v0.4 P2: state persists layout path ------------------------------------
add_executable(test_vst3_state_persists_layout_path
    test_vst3_state_persists_layout_path.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_state_persists_layout_path PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_state_persists_layout_path PRIVATE spe_core)
target_compile_definitions(test_vst3_state_persists_layout_path PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_state_persists_layout_path PRIVATE -UNDEBUG)
add_test(NAME vst3_state_persists_layout_path
    COMMAND test_vst3_state_persists_layout_path)

# ---- v0.4 P2: setupProcessing applies restored layout path ------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/configs")
    set(_SPE_CONFIGS_DIR "${CMAKE_SOURCE_DIR}/configs")
else()
    set(_SPE_CONFIGS_DIR "${CMAKE_SOURCE_DIR}/../configs")
endif()

add_executable(test_vst3_setup_processing_applies_layout
    test_vst3_setup_processing_applies_layout.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_setup_processing_applies_layout PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_setup_processing_applies_layout PRIVATE spe_core)
target_compile_definitions(test_vst3_setup_processing_applies_layout PRIVATE
    SPATIAL_ENGINE_VST3_BUILD=1
    SPE_CONFIGS_DIR="${_SPE_CONFIGS_DIR}")
target_compile_options(test_vst3_setup_processing_applies_layout PRIVATE -UNDEBUG)
add_test(NAME vst3_setup_processing_applies_layout
    COMMAND test_vst3_setup_processing_applies_layout "${_SPE_CONFIGS_DIR}")

# ---- v0.4 P3: state persists sofa path + binaural enable --------------------
add_executable(test_vst3_state_persists_sofa_path
    test_vst3_state_persists_sofa_path.cpp
    ${PLUGIN_SRCS}
)
target_include_directories(test_vst3_state_persists_sofa_path PRIVATE ${VST3_TEST_INCLUDES})
target_link_libraries(test_vst3_state_persists_sofa_path PRIVATE spe_core)
target_compile_definitions(test_vst3_state_persists_sofa_path PRIVATE SPATIAL_ENGINE_VST3_BUILD=1)
target_compile_options(test_vst3_state_persists_sofa_path PRIVATE -UNDEBUG)
add_test(NAME vst3_state_persists_sofa_path COMMAND test_vst3_state_persists_sofa_path)

# ---- ctest dependency chain (AM-R3-9) --------------------------------------
set_tests_properties(vst3_param_layout vst3_param_roundtrip vst3_dispatch_rt_safety
                     vst3_bypass vst3_state_persist vst3_state_v3_reader_only
                     vst3_performedit_threadsafe vst3_state_v3_persist
                     vst3_state_v4_persist vst3_state_v3_to_v4_byte_identical
                     vst3_state_v4_binaural_section
                     vst3_state_v3_back_compat
                     vst3_bus_arrangement_speaker_binaural
                     v04_binaural_bus_placeholder
                     vst3_state_persists_layout_path
                     vst3_setup_processing_applies_layout
                     vst3_state_persists_sofa_path
                     PROPERTIES DEPENDS vst3_host_fixture)
