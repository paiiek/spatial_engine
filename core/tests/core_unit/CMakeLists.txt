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

# Dreamscape Convergence Phase 0 — canonical render-frame adapter
# (mmhoa<->ported Y<->Z swap) + L/R invariant lock for the ported panning ports.
add_executable(test_convergence_coords test_convergence_coords.cpp)
target_link_libraries(test_convergence_coords PRIVATE spe_core)
add_test(NAME convergence_coords COMMAND test_convergence_coords)

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

add_executable(test_p_delayline_clamp test_p_delayline_clamp.cpp)
target_link_libraries(test_p_delayline_clamp PRIVATE spe_core)
add_test(NAME p_delayline_clamp COMMAND test_p_delayline_clamp)

add_executable(test_p3_algoswap_crossfade test_p3_algoswap_crossfade.cpp)
target_link_libraries(test_p3_algoswap_crossfade PRIVATE spe_core spe_util)
add_test(NAME p3_algoswap_crossfade COMMAND test_p3_algoswap_crossfade)

# P4 tests
add_executable(test_p4_handshake test_p4_handshake.cpp)
target_link_libraries(test_p4_handshake PRIVATE spe_core)
add_test(NAME p4_handshake COMMAND test_p4_handshake)

# v0.5.1 Q1 — OSC outbound reply channel end-to-end smoke test.
# Binds a loopback client, sends a handshake into OSCBackend (which
# captures the sender via recvfrom()), drives sendReply() from the
# control thread, asserts the packet arrives back within 200 ms.
# Runs 10 consecutive rounds on ephemeral ports (WM-2 legacy-client path).
add_executable(test_osc_outbound_reply_smoke test_osc_outbound_reply_smoke.cpp)
target_link_libraries(test_osc_outbound_reply_smoke PRIVATE spe_core Threads::Threads)
add_test(NAME osc_outbound_reply_smoke COMMAND test_osc_outbound_reply_smoke)

# ADR 0019 PR4 — shm telemetry emitter unit test. Drives
# spe::bin::ShmTelemetryEmitter against a RingHeader-backed fixture + a
# no-drain OSCBackend, asserting the /sys/warning + /sys/state wire bytes
# (edge emit, underrun 1/s latch, clock-domain stale, state on-change,
# retry-on-no-peer). Control-thread-only; no RT-path code.
add_executable(test_shm_telemetry_emitter test_shm_telemetry_emitter.cpp)
target_link_libraries(test_shm_telemetry_emitter PRIVATE spe_core Threads::Threads)
add_test(NAME shm_telemetry_emitter COMMAND test_shm_telemetry_emitter)

# v0.5.1 Q1 — binaural probe → /sys/binaural_warning emission test.
# Injects a synthetic low-throughput result, drives the warning emission
# path, asserts the client receives the ,sf "ambivs_disabled_cpu" packet.
add_executable(test_binaural_probe_warning_emission
    test_binaural_probe_warning_emission.cpp)
target_link_libraries(test_binaural_probe_warning_emission PRIVATE
    spe_core Threads::Threads)
target_compile_definitions(test_binaural_probe_warning_emission PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME binaural_probe_warning_emission
    COMMAND test_binaural_probe_warning_emission)

# v0.9 Lane A (A-M1) — /sys/metrics 1 Hz emit, end-to-end. Spins up a real
# SpatialEngine on an ephemeral UDP port, drives audioBlock()s (warming the
# CpuMeter), forces an oversized block (engine_overrun_count>0), then runs the
# same ,s key=value emit the core binary's 1 Hz tick does and asserts a client
# receives /sys/metrics with the expected field names + value ranges.
add_executable(test_p_sys_metrics_extended test_p_sys_metrics_extended.cpp)
target_link_libraries(test_p_sys_metrics_extended PRIVATE spe_core Threads::Threads)
add_test(NAME p_sys_metrics_extended COMMAND test_p_sys_metrics_extended)

# v0.9 Lane A (A-M1, AC2b) — CpuMeter record-latency microbench. Measures the
# median cost of recordBlockStart()+recordBlockEnd() over N>=50 samples and
# asserts it stays under a small µs budget (scalar O(1) estimator).
add_executable(bench_cpumeter_record_latency bench_cpumeter_record_latency.cpp)
target_link_libraries(bench_cpumeter_record_latency PRIVATE spe_util)
add_test(NAME cpumeter_record_latency COMMAND bench_cpumeter_record_latency)

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

# v0.8 audit P3 — independent-oracle test additions (P3.2, P3.3, P3.4).
# All verify closed-form expected values derived from textbook formulas,
# NOT from the code under test. P3.7 extends test_p4_flood_malformed in-place.
# P3.3 (FDN T60) landed in P2.3 alongside the DSP-6 read-pointer fix; the
# oracle would fail by ~100× against the pre-fix code.

# P3.2 — SN3D absolute-gain golden vector (encoder × SAMPLING decoder oracle).
add_executable(test_p_ambi_absolute_gain_golden
    test_p_ambi_absolute_gain_golden.cpp)
target_link_libraries(test_p_ambi_absolute_gain_golden PRIVATE spe_core)
add_test(NAME p_ambi_absolute_gain_golden
    COMMAND test_p_ambi_absolute_gain_golden)

# P3.4 — HrtfLookup NN oracle: midpoint direction matches closed-form NN endpoint.
add_executable(test_p_hrtf_lookup_interp test_p_hrtf_lookup_interp.cpp)
target_link_libraries(test_p_hrtf_lookup_interp PRIVATE spe_core)
add_test(NAME p_hrtf_lookup_interp COMMAND test_p_hrtf_lookup_interp)

# P3.3 — FDN T60 accuracy oracle (closed-form T60 = -3·D_mean/(sr·log10(g))).
# Sequenced with the DSP-6 fix in src/reverb/FdnReverb.cpp (see commit log).
add_executable(test_p7_fdn_t60_accuracy test_p7_fdn_t60_accuracy.cpp)
target_link_libraries(test_p7_fdn_t60_accuracy PRIVATE spe_core)
add_test(NAME p7_fdn_t60_accuracy COMMAND test_p7_fdn_t60_accuracy)

# ADM-OSC tests
add_executable(test_p_adm_osc test_p_adm_osc.cpp)
target_link_libraries(test_p_adm_osc PRIVATE spe_core)
add_test(NAME p_adm_osc COMMAND test_p_adm_osc)

# M5.1: EchoPlane unit tests
add_executable(test_echo_plane test_echo_plane.cpp)
target_link_libraries(test_echo_plane PRIVATE spe_core)
add_test(NAME echo_plane COMMAND test_echo_plane)

# Phase C3: ADM-OSC v1.0 compatibility tests (new tags, round-trip, cross-prefix)
add_executable(test_p_adm_osc_v1_compat test_p_adm_osc_v1_compat.cpp)
target_link_libraries(test_p_adm_osc_v1_compat PRIVATE spe_core Threads::Threads)
add_test(NAME p_adm_osc_v1_compat COMMAND test_p_adm_osc_v1_compat)

# US-001: Scene snapshot tests
add_executable(test_p_scene test_p_scene.cpp)
target_link_libraries(test_p_scene PRIVATE spe_core)
add_test(NAME p_scene COMMAND test_p_scene)

# v0.9 Lane E (E-M1): scene library management ops + D3 rescan recovery.
add_executable(test_p_scene_library_ops test_p_scene_library_ops.cpp)
target_link_libraries(test_p_scene_library_ops PRIVATE spe_core)
add_test(NAME p_scene_library_ops COMMAND test_p_scene_library_ops)

# v0.9 Lane E (E-M2): CueList JSON round-trip + boundary value tests.
add_executable(test_p_cuelist_serialize test_p_cuelist_serialize.cpp)
target_link_libraries(test_p_cuelist_serialize PRIVATE spe_core)
add_test(NAME p_cuelist_serialize COMMAND test_p_cuelist_serialize)

# v0.9 Lane E (E-M3): CueEngine go/advance + dwell latch + gain units +
# SceneLoad-failure hold.
add_executable(test_p_cue_go_advance test_p_cue_go_advance.cpp)
target_link_libraries(test_p_cue_go_advance PRIVATE spe_core)
add_test(NAME p_cue_go_advance COMMAND test_p_cue_go_advance)

# v0.9 Lane E (E-M3): wire-path dispatch — /cue/go datagram queued on the UDP
# path, applied on a simulated control loop, outbound updates forwarded via sink.
add_executable(test_p_cue_wire_dispatch test_p_cue_wire_dispatch.cpp)
target_link_libraries(test_p_cue_wire_dispatch PRIVATE spe_core)
add_test(NAME p_cue_wire_dispatch COMMAND test_p_cue_wire_dispatch)

# v0.9 Lane E (E-M6): full library→cuelist→auto-advance end-to-end integration.
# Covers multi-scene multi-cue flow, manual jump mid-sequence, next/prev nav,
# and SceneController library ops (duplicate/rename/remove) feeding into a cue.
add_executable(test_p_scene_cue_e2e test_p_scene_cue_e2e.cpp)
target_link_libraries(test_p_scene_cue_e2e PRIVATE spe_core)
add_test(NAME p_scene_cue_e2e COMMAND test_p_scene_cue_e2e)

# v0.9 Lane F4 (F4b) — save-side object-state capture round-trip: drive objects
# through the production command path, snapshotObjects → /scene/save → reload →
# assert non-zero width/reverb; mute persistence; touched heuristic; empty-objects
# regression baseline.
add_executable(test_p_scene_obj_state_e2e test_p_scene_obj_state_e2e.cpp)
target_link_libraries(test_p_scene_obj_state_e2e PRIVATE spe_core)
add_test(NAME p_scene_obj_state_e2e COMMAND test_p_scene_obj_state_e2e)

# US-002: MAX_OBJECTS scale-up tests
add_executable(test_p_max_objects test_p_max_objects.cpp)
target_link_libraries(test_p_max_objects PRIVATE spe_core)
add_test(NAME p_max_objects COMMAND test_p_max_objects)

# v0.9 Lane C (C-M2 / D2): boundary object (MAX_OBJECTS-1) renders non-silent.
# Cap-relative — proves obj 63 at the 64-build and obj 127 at the 128-build.
add_executable(test_p_object_cap_render test_p_object_cap_render.cpp)
target_link_libraries(test_p_object_cap_render PRIVATE spe_core)
target_compile_definitions(test_p_object_cap_render PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME p_object_cap_render
    COMMAND test_p_object_cap_render "${SPE_FIXTURES_DIR}")

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

# v0.8 audit P1.2 — SN3D closed-form peak oracle. INDEPENDENT verification
# (constants typed from the SN3D definition, NOT copied from
# AmbisonicEncoder.cpp). Pins the m≠0 peak values so a future "fix" that
# rewrites the encoder to maxN (peak=1) fails LOUDLY.
add_executable(test_sn3d_constants test_sn3d_constants.cpp)
target_link_libraries(test_sn3d_constants PRIVATE spe_core)
add_test(NAME sn3d_constants COMMAND test_sn3d_constants)

# v0.8 audit P1.1 — functional test: runtime /sys/ambi_decoder_type switch
# actually changes the live decode matrix (M2HOA-Q14). Drives the control-
# tick forwarder path through AmbisonicRenderer.
add_executable(test_ambi_decoder_type_runtime_apply
    test_ambi_decoder_type_runtime_apply.cpp)
target_link_libraries(test_ambi_decoder_type_runtime_apply PRIVATE spe_core)
add_test(NAME ambi_decoder_type_runtime_apply
    COMMAND test_ambi_decoder_type_runtime_apply)

# v0.8 audit P1.1 — concurrency stress test: RAPID back-to-back decoder-
# type rebuilds against a concurrent decode() loop. Validates the lock-
# free double-buffer (no UAF / torn read) and exercises the BINDING
# timing-slack invariant (switches faster than 1 Hz → would FAIL if the
# slack were removed → no vacuous pass).
add_executable(test_ambi_decoder_type_swap_concurrent
    test_ambi_decoder_type_swap_concurrent.cpp)
target_link_libraries(test_ambi_decoder_type_swap_concurrent
    PRIVATE spe_core Threads::Threads)
add_test(NAME ambi_decoder_type_swap_concurrent
    COMMAND test_ambi_decoder_type_swap_concurrent)
# 200k decode + 50k rebuild iterations; ~52s on an unloaded host.
# 120s timeout prevents flakes under parallel ctest -j load.
set_tests_properties(ambi_decoder_type_swap_concurrent PROPERTIES TIMEOUT 120)

# v0.8 audit P1.3 (DSP-3) — VBAP cold-miss RT-sentinel. Only meaningful
# under SPATIAL_ENGINE_RT_ASSERTS=ON (build_rton). Forces a never-seen
# direction through the audio-thread cold-miss path inside an explicit
# SPE_RT_NO_ALLOC_SCOPE and asserts rt_alloc_violations()==0. Mirrors
# p1_rt_no_alloc / shared_ring_backend_rt_sentinel gating.
if(SPATIAL_ENGINE_RT_ASSERTS)
    add_executable(test_vbap_cold_miss_rt_safe test_vbap_cold_miss_rt_safe.cpp)
    target_link_libraries(test_vbap_cold_miss_rt_safe PRIVATE spe_core)
    add_test(NAME vbap_cold_miss_rt_safe COMMAND test_vbap_cold_miss_rt_safe)
endif()

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

# ─────────────────────────────────────────────────────────────────────────
# v0.5 P2 — KdTree3D HRTF lookup tests
# ─────────────────────────────────────────────────────────────────────────
add_executable(test_kdtree3d_matches_brute_force
    test_kdtree3d_matches_brute_force.cpp)
target_link_libraries(test_kdtree3d_matches_brute_force PRIVATE spe_core)
add_test(NAME kdtree3d_matches_brute_force
    COMMAND test_kdtree3d_matches_brute_force)

add_executable(test_kdtree3d_query_rt_safe
    test_kdtree3d_query_rt_safe.cpp)
target_link_libraries(test_kdtree3d_query_rt_safe PRIVATE spe_core spe_util)
add_test(NAME kdtree3d_query_rt_safe
    COMMAND test_kdtree3d_query_rt_safe)

add_executable(test_kdtree3d_8_canonical_directions
    test_kdtree3d_8_canonical_directions.cpp)
target_link_libraries(test_kdtree3d_8_canonical_directions PRIVATE spe_core)
add_test(NAME kdtree3d_8_canonical_directions
    COMMAND test_kdtree3d_8_canonical_directions)

# ─────────────────────────────────────────────────────────────────────────
# v0.5 P1 — BinauralMonitor double-buffer + crossfade tests
# Use the committed synthetic_min.speh fixture for the .speh path.
# ─────────────────────────────────────────────────────────────────────────
add_executable(test_p_binaural_alloc_free_rt
    test_p_binaural_alloc_free_rt.cpp)
target_link_libraries(test_p_binaural_alloc_free_rt PRIVATE spe_core spe_util Threads::Threads)
target_compile_definitions(test_p_binaural_alloc_free_rt PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME p_binaural_alloc_free_rt
    COMMAND test_p_binaural_alloc_free_rt)

add_executable(test_p_binaural_crossfade_smooth
    test_p_binaural_crossfade_smooth.cpp)
target_link_libraries(test_p_binaural_crossfade_smooth PRIVATE spe_core)
target_compile_definitions(test_p_binaural_crossfade_smooth PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME p_binaural_crossfade_smooth
    COMMAND test_p_binaural_crossfade_smooth)

add_executable(test_p_binaural_chained_crossfade
    test_p_binaural_chained_crossfade.cpp)
target_link_libraries(test_p_binaural_chained_crossfade PRIVATE spe_core)
target_compile_definitions(test_p_binaural_chained_crossfade PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME p_binaural_chained_crossfade
    COMMAND test_p_binaural_chained_crossfade)

# ─────────────────────────────────────────────────────────────────────────
# v0.5 P3 — Multi-object B1 per-object HRTF sum tests
# ─────────────────────────────────────────────────────────────────────────
add_executable(test_binaural_b1_one_object
    test_binaural_b1_one_object.cpp)
target_link_libraries(test_binaural_b1_one_object PRIVATE spe_core)
target_compile_definitions(test_binaural_b1_one_object PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME binaural_b1_one_object
    COMMAND test_binaural_b1_one_object)

add_executable(test_binaural_b1_two_objects_linear
    test_binaural_b1_two_objects_linear.cpp)
target_link_libraries(test_binaural_b1_two_objects_linear PRIVATE spe_core)
target_compile_definitions(test_binaural_b1_two_objects_linear PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME binaural_b1_two_objects_linear
    COMMAND test_binaural_b1_two_objects_linear)

add_executable(test_binaural_b1_64_objects_rt_safe
    test_binaural_b1_64_objects_rt_safe.cpp)
target_link_libraries(test_binaural_b1_64_objects_rt_safe PRIVATE spe_core spe_util)
target_compile_definitions(test_binaural_b1_64_objects_rt_safe PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME binaural_b1_64_objects_rt_safe
    COMMAND test_binaural_b1_64_objects_rt_safe)

# ─────────────────────────────────────────────────────────────────────────
# v0.5 P4 — B2 AmbiVS path (24-pt t-design VS) tests (A4/A6).
# ─────────────────────────────────────────────────────────────────────────
add_executable(test_b2_ownership_lifecycle
    test_b2_ownership_lifecycle.cpp)
target_link_libraries(test_b2_ownership_lifecycle PRIVATE spe_core)
target_compile_definitions(test_b2_ownership_lifecycle PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b2_ownership_lifecycle
    COMMAND test_b2_ownership_lifecycle)

add_executable(test_b2_layout_change_does_not_reinit_vs
    test_b2_layout_change_does_not_reinit_vs.cpp)
target_link_libraries(test_b2_layout_change_does_not_reinit_vs PRIVATE spe_core)
target_compile_definitions(test_b2_layout_change_does_not_reinit_vs PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b2_layout_change_does_not_reinit_vs
    COMMAND test_b2_layout_change_does_not_reinit_vs)

add_executable(test_b2_throughput_probe_fallback
    test_b2_throughput_probe_fallback.cpp)
target_link_libraries(test_b2_throughput_probe_fallback PRIVATE spe_core)
target_compile_definitions(test_b2_throughput_probe_fallback PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b2_throughput_probe_fallback
    COMMAND test_b2_throughput_probe_fallback)

add_executable(test_b2_ambivs_equivalent_to_b1_at_order3
    test_b2_ambivs_equivalent_to_b1_at_order3.cpp)
target_link_libraries(test_b2_ambivs_equivalent_to_b1_at_order3 PRIVATE spe_core)
target_compile_definitions(test_b2_ambivs_equivalent_to_b1_at_order3 PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b2_ambivs_equivalent_to_b1_at_order3
    COMMAND test_b2_ambivs_equivalent_to_b1_at_order3)

# v0.6 #5 — runtime sticky-underrun auto-demote (B2 wall-clock detector).
add_executable(test_b2_runtime_underrun_auto_demote
    test_b2_runtime_underrun_auto_demote.cpp)
target_link_libraries(test_b2_runtime_underrun_auto_demote PRIVATE spe_core)
target_compile_definitions(test_b2_runtime_underrun_auto_demote PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b2_runtime_underrun_auto_demote
    COMMAND test_b2_runtime_underrun_auto_demote)

# v0.7 D-S1 — user-controlled reset hatch (60 s cooldown, 8-atomic AM-1 reset).
add_executable(test_b2_runtime_underrun_user_reset
    test_b2_runtime_underrun_user_reset.cpp)
target_link_libraries(test_b2_runtime_underrun_user_reset PRIVATE spe_core)
target_compile_definitions(test_b2_runtime_underrun_user_reset PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b2_runtime_underrun_user_reset
    COMMAND test_b2_runtime_underrun_user_reset)

# v0.7 D-S1 — concurrent-safety: audio-thread demote vs IO-thread reset
# (Scenario A) + initialize() vs OSC reset (Scenario B, Critic §D.8).
add_executable(test_b2_runtime_underrun_user_reset_concurrent
    test_b2_runtime_underrun_user_reset_concurrent.cpp)
target_link_libraries(test_b2_runtime_underrun_user_reset_concurrent PRIVATE spe_core)
target_compile_definitions(test_b2_runtime_underrun_user_reset_concurrent PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b2_runtime_underrun_user_reset_concurrent
    COMMAND test_b2_runtime_underrun_user_reset_concurrent)

# v0.6 P1-3 — engine-level integration of #5 forwarders + P1-4 kill-switch.
# Bridges audioBlock() through the SpatialEngine API rather than direct
# BinauralMonitor poking so a stub/no-op forwarder cannot slip past CI.
add_executable(test_b2_runtime_underrun_engine_integration
    test_b2_runtime_underrun_engine_integration.cpp)
target_link_libraries(test_b2_runtime_underrun_engine_integration PRIVATE spe_core)
target_compile_definitions(test_b2_runtime_underrun_engine_integration PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b2_runtime_underrun_engine_integration
    COMMAND test_b2_runtime_underrun_engine_integration)

# ─────────────────────────────────────────────────────────────────────────
# v0.5.1 Q2 (A3) — B1↔B2 mode-transition crossfade tests.
# ─────────────────────────────────────────────────────────────────────────
add_executable(test_b1_b2_mode_transition_smooth
    test_b1_b2_mode_transition_smooth.cpp)
target_link_libraries(test_b1_b2_mode_transition_smooth PRIVATE spe_core)
target_compile_definitions(test_b1_b2_mode_transition_smooth PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b1_b2_mode_transition_smooth
    COMMAND test_b1_b2_mode_transition_smooth)

add_executable(test_b1_b2_mode_transition_probe_clamped
    test_b1_b2_mode_transition_probe_clamped.cpp)
target_link_libraries(test_b1_b2_mode_transition_probe_clamped PRIVATE spe_core)
target_compile_definitions(test_b1_b2_mode_transition_probe_clamped PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b1_b2_mode_transition_probe_clamped
    COMMAND test_b1_b2_mode_transition_probe_clamped)

# ─────────────────────────────────────────────────────────────────────────
# v0.5.1 hotfix — post-validation must-fix regression tests.
# ─────────────────────────────────────────────────────────────────────────

# Fix 1 (code-reviewer MAJOR): xfade arm-skip on disable→re-enable. Ensures
# prev_effective_mode_ stays in lock-step while binaural is disabled so
# the first re-enabled block does NOT arm a phantom ramp.
add_executable(test_b1_b2_mode_transition_disable_reenable
    test_b1_b2_mode_transition_disable_reenable.cpp)
target_link_libraries(test_b1_b2_mode_transition_disable_reenable
    PRIVATE spe_core)
target_compile_definitions(test_b1_b2_mode_transition_disable_reenable PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME b1_b2_mode_transition_disable_reenable
    COMMAND test_b1_b2_mode_transition_disable_reenable)

# Fix 2 + Fix 3 (security H1 + M1): overridePeerPort validation +
# capture-peer-only-on-valid-decode.
add_executable(test_osc_security_peer_validation
    test_osc_security_peer_validation.cpp)
target_link_libraries(test_osc_security_peer_validation
    PRIVATE spe_core Threads::Threads)
add_test(NAME osc_security_peer_validation
    COMMAND test_osc_security_peer_validation)

# Fix 4 (code-MINOR): multi-producer SPSC ring safety. Stress 3 producers x
# 1000 iters and verify sent+drops accounting + payload integrity.
add_executable(test_osc_outbound_multi_producer
    test_osc_outbound_multi_producer.cpp)
target_link_libraries(test_osc_outbound_multi_producer
    PRIVATE spe_core Threads::Threads)
add_test(NAME osc_outbound_multi_producer
    COMMAND test_osc_outbound_multi_producer)

# ADR 0018 PR1 — OSC type-tag parser: ',d' and ',h' coverage (P1-P4).
add_executable(test_osc_type_tag_parser test_osc_type_tag_parser.cpp)
target_link_libraries(test_osc_type_tag_parser PRIVATE spe_core)
add_test(NAME osc_type_tag_parser COMMAND test_osc_type_tag_parser)

# ADR 0018 PR2 — Phase B sync handlers: D-2 transport/play advisory timetag,
# D-3 transport/pause alias, D-5 external-player heartbeat staleness (§3b 1-10).
add_executable(test_phase_b_sync_handlers test_phase_b_sync_handlers.cpp)
target_link_libraries(test_phase_b_sync_handlers PRIVATE spe_core Threads::Threads)
add_test(NAME phase_b_sync_handlers COMMAND test_phase_b_sync_handlers)

# v0.5.1 final polish (Sec H2): default OSC bind is loopback (127.0.0.1).
# Asserts: (a) no setBindAddr() call leaves the listener on 127.0.0.1,
# (b) explicit "0.0.0.0" still binds the wildcard, (c) invalid bind strings
# fall back to loopback with a stderr warning.
add_executable(test_osc_bind_loopback_default
    test_osc_bind_loopback_default.cpp)
target_link_libraries(test_osc_bind_loopback_default
    PRIVATE spe_core Threads::Threads)
add_test(NAME osc_bind_loopback_default
    COMMAND test_osc_bind_loopback_default)

# ─────────────────────────────────────────────────────────────────────────
# ADR 0019 PR1 — POSIX shared-memory ring: header layout + region tests.
# ─────────────────────────────────────────────────────────────────────────

# Header-layout tests: no system deps, compile-time static_assert + runtime.
add_executable(test_shm_ring_header test_shm_ring_header.cpp)
target_link_libraries(test_shm_ring_header PRIVATE spe_core)
add_test(NAME shm_ring_header COMMAND test_shm_ring_header)

# Region tests: require /dev/shm (Linux) or POSIX shm (macOS).
add_executable(test_shared_memory_region test_shared_memory_region.cpp)
target_link_libraries(test_shared_memory_region PRIVATE spe_core)
add_test(NAME shared_memory_region COMMAND test_shared_memory_region)

# ─────────────────────────────────────────────────────────────────────────
# ADR 0019 PR2 — input-only SharedRingBackend: cases 1–8.
# ─────────────────────────────────────────────────────────────────────────

# Always-on target: cases 1–7 + case 8 structural invariants (ii/iii/iv).
# Requires /dev/shm (Linux) or POSIX shm (macOS); SKIPs elsewhere.
add_executable(test_shared_ring_backend test_shared_ring_backend.cpp)
target_link_libraries(test_shared_ring_backend PRIVATE spe_core)
add_test(NAME shared_ring_backend COMMAND test_shared_ring_backend)

# RT-sentinel target: case 8 invariant (i) rt_alloc_violations()==0 is only
# meaningful when the alloc-recording override TU (RtAssertNoAlloc.cpp) is
# compiled, i.e. SPE_RT_ASSERTS=ON. Mirror p1_rt_no_alloc (line 106 above).
if(SPATIAL_ENGINE_RT_ASSERTS)
    add_executable(test_shared_ring_backend_rt_sentinel test_shared_ring_backend.cpp)
    target_link_libraries(test_shared_ring_backend_rt_sentinel PRIVATE spe_core)
    target_compile_definitions(test_shared_ring_backend_rt_sentinel PRIVATE
        SRB_RT_SENTINEL=1)
    add_test(NAME shared_ring_backend_rt_sentinel
        COMMAND test_shared_ring_backend_rt_sentinel)
endif()

# B-M1 — HRTF catalog loader gate test
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/hrtf/catalog.json")
    set(SPE_CATALOG_PATH "${CMAKE_SOURCE_DIR}/assets/hrtf/catalog.json")
else()
    set(SPE_CATALOG_PATH "${CMAKE_SOURCE_DIR}/../assets/hrtf/catalog.json")
endif()
add_executable(test_p_hrtf_catalog test_p_hrtf_catalog.cpp)
target_link_libraries(test_p_hrtf_catalog PRIVATE spe_core)
target_compile_definitions(test_p_hrtf_catalog PRIVATE
    SPE_CATALOG_PATH="${SPE_CATALOG_PATH}")
add_test(NAME p_hrtf_catalog COMMAND test_p_hrtf_catalog)

# B-M3 — OSC /sys/binaural_sofa_select encode/decode round-trip + catalog resolution
add_executable(test_p_osc_binaural_sofa_select test_p_osc_binaural_sofa_select.cpp)
target_link_libraries(test_p_osc_binaural_sofa_select PRIVATE spe_core)
target_compile_definitions(test_p_osc_binaural_sofa_select PRIVATE
    SPE_CATALOG_PATH="${SPE_CATALOG_PATH}")
add_test(NAME osc_binaural_sofa_select COMMAND test_p_osc_binaural_sofa_select)

# B-M2 — runtime SOFA hot-swap functional test: loadPendingSofa +
# applyPendingSofaChange swaps the active table/tree (detectable via fixtureB's
# distinct ITD + ir_length); a failed (oversized ir_length) load leaves the
# active slot untouched and returns the failure code.
add_executable(test_p_hrtf_sofa_swap_apply test_p_hrtf_sofa_swap_apply.cpp)
target_link_libraries(test_p_hrtf_sofa_swap_apply PRIVATE spe_core)
target_compile_definitions(test_p_hrtf_sofa_swap_apply PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}")
add_test(NAME p_hrtf_sofa_swap_apply COMMAND test_p_hrtf_sofa_swap_apply)

# B-M6 — Per-dataset ITD oracle regression guard.
# Loads every catalog .speh present on disk (skip-with-message if absent) plus
# the always-present synthetic_itd_pm90.speh CI fixture.  Validates az=±90 ITD
# against the Woodworth closed-form using each dataset's OWN sample_rate.
# The test FAILS (not skips) if zero datasets were exercised.
add_executable(test_p_binaural_dataset_itd_oracle
    test_p_binaural_dataset_itd_oracle.cpp)
target_link_libraries(test_p_binaural_dataset_itd_oracle PRIVATE spe_core)
target_compile_definitions(test_p_binaural_dataset_itd_oracle PRIVATE
    SPE_FIXTURES_DIR="${SPE_FIXTURES_DIR}"
    SPE_CATALOG_PATH="${SPE_CATALOG_PATH}")
add_test(NAME binaural_dataset_itd_oracle
    COMMAND test_p_binaural_dataset_itd_oracle)
