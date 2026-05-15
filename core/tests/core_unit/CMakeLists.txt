# core/tests/core_unit — CTest unit suite for the C++ audio core.

# Tests rely on assert() for correctness checks. Release builds inject
# -DNDEBUG which strips assertions and silently turns failures into PASS.
# Re-enable assertions for every test target in this directory.
add_compile_options(-UNDEBUG)

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
# CMAKE_SOURCE_DIR varies: 'spatial_engine/' (top-level build) vs 'core/'
# (legacy `cd core/build` flow). Probe both — repo configs/ is at repo root.
if(EXISTS "${CMAKE_SOURCE_DIR}/configs")
    set(SPE_CONFIGS_DIR "${CMAKE_SOURCE_DIR}/configs")
else()
    set(SPE_CONFIGS_DIR "${CMAKE_SOURCE_DIR}/../configs")
endif()

# v0.3.1 fixtures live under core/tests/fixtures.
if(EXISTS "${CMAKE_SOURCE_DIR}/core/tests/fixtures")
    set(SPE_FIXTURES_DIR "${CMAKE_SOURCE_DIR}/core/tests/fixtures")
else()
    set(SPE_FIXTURES_DIR "${CMAKE_SOURCE_DIR}/tests/fixtures")
endif()

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

# v0.3.1 — SpeakerLayout channel→index lookup + duplicate-channel rejection.
add_executable(test_speaker_layout_channel_lookup test_speaker_layout_channel_lookup.cpp)
target_link_libraries(test_speaker_layout_channel_lookup PRIVATE spe_core)
target_compile_definitions(test_speaker_layout_channel_lookup PRIVATE
    SPE_CONFIGS_DIR="${SPE_CONFIGS_DIR}"
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME speaker_layout_channel_lookup
    COMMAND test_speaker_layout_channel_lookup "${SPE_CONFIGS_DIR}" "${SPE_FIXTURES_DIR}")

add_executable(test_channel_map_duplicate_rejects test_channel_map_duplicate_rejects.cpp)
target_link_libraries(test_channel_map_duplicate_rejects PRIVATE spe_core)
add_test(NAME channel_map_duplicate_rejects COMMAND test_channel_map_duplicate_rejects)

# v0.3.1 P1b — OSC per-channel routing via SpeakerLayout::channelToIndex().
# Four merge-gate tests, one per affected handler. Each runs against the
# reordered fixture {8,1,2,7,3,6,4,5} and asserts state lands on the
# vector position whose YAML channel matches the wire channel.
add_executable(test_osc_output_gain_reordered_channels
    test_osc_output_gain_reordered_channels.cpp)
target_link_libraries(test_osc_output_gain_reordered_channels PRIVATE spe_core)
target_compile_definitions(test_osc_output_gain_reordered_channels PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME osc_output_gain_reordered_channels
    COMMAND test_osc_output_gain_reordered_channels "${SPE_FIXTURES_DIR}")

add_executable(test_osc_output_limit_reordered_channels
    test_osc_output_limit_reordered_channels.cpp)
target_link_libraries(test_osc_output_limit_reordered_channels PRIVATE spe_core)
target_compile_definitions(test_osc_output_limit_reordered_channels PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME osc_output_limit_reordered_channels
    COMMAND test_osc_output_limit_reordered_channels "${SPE_FIXTURES_DIR}")

add_executable(test_osc_noise_type_reordered_channels
    test_osc_noise_type_reordered_channels.cpp)
target_link_libraries(test_osc_noise_type_reordered_channels PRIVATE spe_core)
target_compile_definitions(test_osc_noise_type_reordered_channels PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME osc_noise_type_reordered_channels
    COMMAND test_osc_noise_type_reordered_channels "${SPE_FIXTURES_DIR}")

add_executable(test_osc_noise_gain_reordered_channels
    test_osc_noise_gain_reordered_channels.cpp)
target_link_libraries(test_osc_noise_gain_reordered_channels PRIVATE spe_core)
target_compile_definitions(test_osc_noise_gain_reordered_channels PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME osc_noise_gain_reordered_channels
    COMMAND test_osc_noise_gain_reordered_channels "${SPE_FIXTURES_DIR}")

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

# v0.4 P2 — OSC SysLoadLayout round-trip
add_executable(test_osc_sys_load_layout_roundtrip test_osc_sys_load_layout_roundtrip.cpp)
target_link_libraries(test_osc_sys_load_layout_roundtrip PRIVATE spe_core)
add_test(NAME osc_sys_load_layout_roundtrip COMMAND test_osc_sys_load_layout_roundtrip)

# v0.4 P2 — speaker layout load from path (5 YAML files)
add_executable(test_speaker_layout_load_path test_speaker_layout_load_path.cpp)
target_link_libraries(test_speaker_layout_load_path PRIVATE spe_core)
target_compile_definitions(test_speaker_layout_load_path PRIVATE
    SPE_CONFIGS_DIR="${SPE_CONFIGS_DIR}"
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME speaker_layout_load_path
    COMMAND test_speaker_layout_load_path "${SPE_CONFIGS_DIR}" "${SPE_FIXTURES_DIR}")

# v0.4 P3 — OSC SysBinauralSofa round-trip
add_executable(test_osc_sys_binaural_sofa_roundtrip test_osc_sys_binaural_sofa_roundtrip.cpp)
target_link_libraries(test_osc_sys_binaural_sofa_roundtrip PRIVATE spe_core)
add_test(NAME osc_sys_binaural_sofa_roundtrip COMMAND test_osc_sys_binaural_sofa_roundtrip)

# v0.4 P3 — OSC SysBinauralEnable round-trip
add_executable(test_osc_sys_binaural_enable_roundtrip test_osc_sys_binaural_enable_roundtrip.cpp)
target_link_libraries(test_osc_sys_binaural_enable_roundtrip PRIVATE spe_core)
add_test(NAME osc_sys_binaural_enable_roundtrip COMMAND test_osc_sys_binaural_enable_roundtrip)

# v0.4 P3 — binaural setBinauralEnabled RT no-alloc (only meaningful with SPE_RT_ASSERTS)
if(SPATIAL_ENGINE_RT_ASSERTS)
    add_executable(test_p_binaural_setDirection_no_alloc test_p_binaural_setDirection_no_alloc.cpp)
    target_link_libraries(test_p_binaural_setDirection_no_alloc PRIVATE spe_core Threads::Threads)
    add_test(NAME p_binaural_setDirection_no_alloc
        COMMAND test_p_binaural_setDirection_no_alloc)
endif()

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

# Phase C3: ADM-OSC v1.0 compatibility tests (new tags, round-trip, cross-prefix)
add_executable(test_p_adm_osc_v1_compat test_p_adm_osc_v1_compat.cpp)
target_link_libraries(test_p_adm_osc_v1_compat PRIVATE spe_core Threads::Threads)
add_test(NAME p_adm_osc_v1_compat COMMAND test_p_adm_osc_v1_compat)

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

# T3 (v1b): VBAPRenderer gain cache tests
add_executable(test_p_vbap_cache test_p_vbap_cache.cpp)
target_link_libraries(test_p_vbap_cache PRIVATE spe_core)
add_test(NAME p_vbap_cache COMMAND test_p_vbap_cache)

# F2: Ambisonics 1st-order encoder numerical tests
add_executable(test_p_ambi test_p_ambi.cpp)
target_link_libraries(test_p_ambi PRIVATE spe_core)
add_test(NAME p_ambi COMMAND test_p_ambi)

# v1c: BinauralMonitor pure-C++ HRTF pipeline tests
add_executable(test_p_binaural_stub test_p_binaural_stub.cpp)
target_link_libraries(test_p_binaural_stub PRIVATE spe_core)
add_test(NAME p_binaural_stub COMMAND test_p_binaural_stub)

# M3: IRConvReverb (OLA) tests
add_executable(test_p_ir_reverb test_p_ir_reverb.cpp)
target_link_libraries(test_p_ir_reverb PRIVATE spe_core)
add_test(NAME p_ir_reverb COMMAND test_p_ir_reverb)

# M2: AmbiDecoder mode-matching projection tests
add_executable(test_p_ambi_decoder test_p_ambi_decoder.cpp)
target_link_libraries(test_p_ambi_decoder PRIVATE spe_core)
add_test(NAME p_ambi_decoder COMMAND test_p_ambi_decoder)

# M1: per-object Source WIDTH fan-out tests
add_executable(test_p_source_width test_p_source_width.cpp)
target_link_libraries(test_p_source_width PRIVATE spe_core)
add_test(NAME p_source_width COMMAND test_p_source_width)

# M6: per-speaker time-alignment (delay_ms / gain_db)
add_executable(test_p_speaker_alignment test_p_speaker_alignment.cpp)
target_link_libraries(test_p_speaker_alignment PRIVATE spe_core)
add_test(NAME p_speaker_alignment COMMAND test_p_speaker_alignment)

# M9: per-channel ChannelLimiter + /output/{ch}/{gain,limit} OSC
add_executable(test_p_channel_limiter test_p_channel_limiter.cpp)
target_link_libraries(test_p_channel_limiter PRIVATE spe_core)
add_test(NAME p_channel_limiter COMMAND test_p_channel_limiter)

# B3: IRConvReverb WAV loading (loadIRFromWav)
add_executable(test_p_ir_loading test_p_ir_loading.cpp)
target_link_libraries(test_p_ir_loading PRIVATE spe_core)
add_test(NAME p_ir_loading COMMAND test_p_ir_loading)

# B4: DBAP width 정밀화 — 3 가상소스 power-sum + energy 보존
add_executable(test_p_dbap_width test_p_dbap_width.cpp)
target_link_libraries(test_p_dbap_width PRIVATE spe_core)
add_test(NAME p_dbap_width COMMAND test_p_dbap_width)

# M4: Scene crossfade time-based interpolation
add_executable(test_p_scene_crossfade test_p_scene_crossfade.cpp)
target_link_libraries(test_p_scene_crossfade PRIVATE spe_core)
add_test(NAME p_scene_crossfade COMMAND test_p_scene_crossfade)

# M7: SMPTE LTC biphase-mark decoder
add_executable(test_p_ltc_decoder test_p_ltc_decoder.cpp)
target_link_libraries(test_p_ltc_decoder PRIVATE spe_core)
add_test(NAME p_ltc_decoder COMMAND test_p_ltc_decoder)

# C1.a: NullBackend audio-input path acceptance
add_executable(test_p_audio_io_input test_p_audio_io_input.cpp)
target_link_libraries(test_p_audio_io_input PRIVATE spe_core)
add_test(NAME p_audio_io_input COMMAND test_p_audio_io_input)

# C1.b: SPSC ring + QueuedCmd POD extension acceptance
add_executable(test_p_spsc_ring test_p_spsc_ring.cpp)
target_link_libraries(test_p_spsc_ring PRIVATE spe_util Threads::Threads)
add_test(NAME p_spsc_ring COMMAND test_p_spsc_ring)

# C1.c: LtcChase audio→ring→control-thread decoder acceptance
add_executable(test_p_ltc_chase test_p_ltc_chase.cpp)
target_link_libraries(test_p_ltc_chase PRIVATE spe_core)
add_test(NAME p_ltc_chase COMMAND test_p_ltc_chase)

# C1.d: /sys/ltc_chase OSC roundtrip + engine integration acceptance
add_executable(test_p_sys_ltc_chase test_p_sys_ltc_chase.cpp)
target_link_libraries(test_p_sys_ltc_chase PRIVATE spe_core)
add_test(NAME p_sys_ltc_chase COMMAND test_p_sys_ltc_chase)

# M2-HOA extended: 4 new decoder algorithm tests
add_executable(test_p_ambi_decoder_max_re test_p_ambi_decoder_max_re.cpp)
target_link_libraries(test_p_ambi_decoder_max_re PRIVATE spe_core)
add_test(NAME p_ambi_decoder_max_re COMMAND test_p_ambi_decoder_max_re)

add_executable(test_p_ambi_decoder_allrad test_p_ambi_decoder_allrad.cpp)
target_link_libraries(test_p_ambi_decoder_allrad PRIVATE spe_core)
add_test(NAME p_ambi_decoder_allrad COMMAND test_p_ambi_decoder_allrad)

add_executable(test_p_ambi_decoder_epad test_p_ambi_decoder_epad.cpp)
target_link_libraries(test_p_ambi_decoder_epad PRIVATE spe_core)
add_test(NAME p_ambi_decoder_epad COMMAND test_p_ambi_decoder_epad)

add_executable(test_p_ambi_decoder_in_phase test_p_ambi_decoder_in_phase.cpp)
target_link_libraries(test_p_ambi_decoder_in_phase PRIVATE spe_core)
add_test(NAME p_ambi_decoder_in_phase COMMAND test_p_ambi_decoder_in_phase)


# ─────────────────────────────────────────────────────────────────────────
# v0.5 P0 — OlaConvolver::loadInto + prepareForReload tests
# ─────────────────────────────────────────────────────────────────────────
add_executable(test_ola_convolver_loadinto_rt_safe
    test_ola_convolver_loadinto_rt_safe.cpp)
target_link_libraries(test_ola_convolver_loadinto_rt_safe PRIVATE spe_core spe_util)
add_test(NAME ola_convolver_loadinto_rt_safe
    COMMAND test_ola_convolver_loadinto_rt_safe)

add_executable(test_ola_convolver_loadinto_capacity_violation_release
    test_ola_convolver_loadinto_capacity_violation_release.cpp)
target_link_libraries(test_ola_convolver_loadinto_capacity_violation_release PRIVATE spe_core)
add_test(NAME ola_convolver_loadinto_capacity_violation_release
    COMMAND test_ola_convolver_loadinto_capacity_violation_release)
