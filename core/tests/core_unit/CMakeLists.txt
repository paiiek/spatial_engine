# core/tests/core_unit — CTest unit suite for the C++ audio core.

add_executable(test_p0_smoke test_p0_smoke.cpp)
target_link_libraries(test_p0_smoke PRIVATE spe_util)
add_test(NAME p0_smoke COMMAND test_p0_smoke)

add_executable(test_p1_null_backend test_p1_null_backend.cpp)
target_link_libraries(test_p1_null_backend PRIVATE spe_core)
add_test(NAME p1_null_backend COMMAND test_p1_null_backend)

add_executable(test_p1_max_block_boundary test_p1_max_block_boundary.cpp)
target_link_libraries(test_p1_max_block_boundary PRIVATE spe_core)
add_test(NAME p1_max_block_boundary COMMAND test_p1_max_block_boundary)

add_executable(test_p1_trace_ring test_p1_trace_ring.cpp)
target_link_libraries(test_p1_trace_ring PRIVATE spe_util)
add_test(NAME p1_trace_ring COMMAND test_p1_trace_ring)

# P2 tests
set(SPE_CONFIGS_DIR "${CMAKE_SOURCE_DIR}/../configs")

add_executable(test_p2_coords test_p2_coords.cpp)
target_link_libraries(test_p2_coords PRIVATE spe_core)
add_test(NAME p2_coords COMMAND test_p2_coords)

add_executable(test_p2_layout_loader test_p2_layout_loader.cpp)
target_link_libraries(test_p2_layout_loader PRIVATE spe_core)
target_compile_definitions(test_p2_layout_loader PRIVATE
    SPE_CONFIGS_DIR="${SPE_CONFIGS_DIR}")
add_test(NAME p2_layout_loader COMMAND test_p2_layout_loader "${SPE_CONFIGS_DIR}")

add_executable(test_p2_compat_checker test_p2_compat_checker.cpp)
target_link_libraries(test_p2_compat_checker PRIVATE spe_core)
target_compile_definitions(test_p2_compat_checker PRIVATE
    SPE_CONFIGS_DIR="${SPE_CONFIGS_DIR}")
add_test(NAME p2_compat_checker COMMAND test_p2_compat_checker "${SPE_CONFIGS_DIR}")

# RT-assert violation counting only meaningful when SPE_RT_ASSERTS is on.
if(SPATIAL_ENGINE_RT_ASSERTS)
    add_executable(test_p1_rt_no_alloc test_p1_rt_no_alloc.cpp)
    target_link_libraries(test_p1_rt_no_alloc PRIVATE spe_core)
    add_test(NAME p1_rt_no_alloc COMMAND test_p1_rt_no_alloc)
endif()

# P3 tests
add_executable(test_p3_vbap test_p3_vbap.cpp)
target_link_libraries(test_p3_vbap PRIVATE spe_core)
add_test(NAME p3_vbap COMMAND test_p3_vbap)

add_executable(test_p3_dbap test_p3_dbap.cpp)
target_link_libraries(test_p3_dbap PRIVATE spe_core)
add_test(NAME p3_dbap COMMAND test_p3_dbap)

add_executable(test_p3_wfs test_p3_wfs.cpp)
target_link_libraries(test_p3_wfs PRIVATE spe_core)
add_test(NAME p3_wfs COMMAND test_p3_wfs)

add_executable(test_p3_compat_rules test_p3_compat_rules.cpp)
target_link_libraries(test_p3_compat_rules PRIVATE spe_core)
add_test(NAME p3_compat_rules COMMAND test_p3_compat_rules)

add_executable(test_p3_distance_hf test_p3_distance_hf.cpp)
target_link_libraries(test_p3_distance_hf PRIVATE spe_core)
add_test(NAME p3_distance_hf COMMAND test_p3_distance_hf)

add_executable(test_p3_gainramp_click test_p3_gainramp_click.cpp)
target_link_libraries(test_p3_gainramp_click PRIVATE spe_core spe_util)
add_test(NAME p3_gainramp_click COMMAND test_p3_gainramp_click)

add_executable(test_p3_propdelay_sweep test_p3_propdelay_sweep.cpp)
target_link_libraries(test_p3_propdelay_sweep PRIVATE spe_core spe_util)
add_test(NAME p3_propdelay_sweep COMMAND test_p3_propdelay_sweep)

add_executable(test_p3_algoswap_crossfade test_p3_algoswap_crossfade.cpp)
target_link_libraries(test_p3_algoswap_crossfade PRIVATE spe_core spe_util)
add_test(NAME p3_algoswap_crossfade COMMAND test_p3_algoswap_crossfade)

# P4 tests
add_executable(test_p4_handshake test_p4_handshake.cpp)
target_link_libraries(test_p4_handshake PRIVATE spe_core)
add_test(NAME p4_handshake COMMAND test_p4_handshake)

add_executable(test_p4_command_decode test_p4_command_decode.cpp)
target_link_libraries(test_p4_command_decode PRIVATE spe_core)
add_test(NAME p4_command_decode COMMAND test_p4_command_decode)

add_executable(test_p4_state_model_seq test_p4_state_model_seq.cpp)
target_link_libraries(test_p4_state_model_seq PRIVATE spe_core)
add_test(NAME p4_state_model_seq COMMAND test_p4_state_model_seq)

add_executable(test_p4_reorder_alert test_p4_reorder_alert.cpp)
target_link_libraries(test_p4_reorder_alert PRIVATE spe_core)
add_test(NAME p4_reorder_alert COMMAND test_p4_reorder_alert)

add_executable(test_p4_heartbeat_miss test_p4_heartbeat_miss.cpp)
target_link_libraries(test_p4_heartbeat_miss PRIVATE spe_core)
add_test(NAME p4_heartbeat_miss COMMAND test_p4_heartbeat_miss)

add_executable(test_p4_vst3stub test_p4_vst3stub.cpp)
target_link_libraries(test_p4_vst3stub PRIVATE spe_core)
add_test(NAME p4_vst3stub COMMAND test_p4_vst3stub)

add_executable(test_p4_flood_valid test_p4_flood_valid.cpp)
target_link_libraries(test_p4_flood_valid PRIVATE spe_core)
add_test(NAME p4_flood_valid COMMAND test_p4_flood_valid)

add_executable(test_p4_flood_malformed test_p4_flood_malformed.cpp)
target_link_libraries(test_p4_flood_malformed PRIVATE spe_core)
add_test(NAME p4_flood_malformed COMMAND test_p4_flood_malformed)

# P5 tests
add_executable(test_p5_decoder_underflow test_p5_decoder_underflow.cpp)
target_link_libraries(test_p5_decoder_underflow PRIVATE spe_core)
add_test(NAME p5_decoder_underflow COMMAND test_p5_decoder_underflow)

# P6 tests
add_executable(test_p6_dante_loopback test_p6_dante_loopback.cpp)
target_link_libraries(test_p6_dante_loopback PRIVATE spe_core)
add_test(NAME p6_dante_loopback COMMAND test_p6_dante_loopback)

# P7 tests
add_executable(test_p7_fdn_reverb test_p7_fdn_reverb.cpp)
target_link_libraries(test_p7_fdn_reverb PRIVATE spe_core)
add_test(NAME p7_fdn_reverb COMMAND test_p7_fdn_reverb)

# ADM-OSC tests
add_executable(test_p_adm_osc test_p_adm_osc.cpp)
target_link_libraries(test_p_adm_osc PRIVATE spe_core)
add_test(NAME p_adm_osc COMMAND test_p_adm_osc)

# US-001: Scene snapshot tests
add_executable(test_p_scene test_p_scene.cpp)
target_link_libraries(test_p_scene PRIVATE spe_core)
add_test(NAME p_scene COMMAND test_p_scene)

# US-002: MAX_OBJECTS scale-up tests
add_executable(test_p_max_objects test_p_max_objects.cpp)
target_link_libraries(test_p_max_objects PRIVATE spe_core)
add_test(NAME p_max_objects COMMAND test_p_max_objects)

# Feature 2: VBAP 3D elevation numerical tests
add_executable(test_p_vbap3d test_p_vbap3d.cpp)
target_link_libraries(test_p_vbap3d PRIVATE spe_core)
add_test(NAME p_vbap3d COMMAND test_p_vbap3d)

