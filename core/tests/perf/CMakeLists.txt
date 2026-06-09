# core/tests/perf — Performance / soak tests.
# Soak test runs on Release only (skip Debug to keep CI fast).
# Per ADR 0006 / A6: soak uses real UDP path, NOT in-process injectPacket.

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR SPATIAL_ENGINE_RUN_SOAK)
    add_executable(soak_adm_osc_flood soak_adm_osc_flood.cpp)
    target_link_libraries(soak_adm_osc_flood PRIVATE spe_core Threads::Threads)
    add_test(NAME soak_adm_osc_flood
             COMMAND soak_adm_osc_flood
             CONFIGURATIONS Release)
    set_tests_properties(soak_adm_osc_flood PROPERTIES TIMEOUT 120)

    # v0.9 Lane E (E-M3) D1(b) — fix-1a single-producer race stress. Floods
    # /adm/obj/* over real UDP concurrently with active cue crossfades posting
    # object updates; both funnel through the ONE UDP-thread cmd_fifo_ producer.
    # Intended to run under ThreadSanitizer (build with -fsanitize=thread; run
    # under `setarch -R` on kernels with the ASLR/TSan mmap conflict).
    add_executable(soak_cue_cmdfifo_race soak_cue_cmdfifo_race.cpp)
    target_link_libraries(soak_cue_cmdfifo_race PRIVATE spe_core Threads::Threads)
    add_test(NAME soak_cue_cmdfifo_race
             COMMAND soak_cue_cmdfifo_race
             CONFIGURATIONS Release)
    set_tests_properties(soak_cue_cmdfifo_race PROPERTIES TIMEOUT 60)

    # v0.9 Lane F5 (F5-M3b) — Option C lazy-WFS-alloc TSan gate. An "audio" thread
    # spins WFSRenderer::processBlock while a "control" thread calls
    # ensureAllocated() mid-flight (first WFS activation). Asserts zero data races
    # on ready_/delays_/ramps_ (under -fsanitize=thread) and correct non-silent
    # audio after the allocate-then-publish flip.
    add_executable(soak_wfs_algoswap_race soak_wfs_algoswap_race.cpp)
    target_link_libraries(soak_wfs_algoswap_race PRIVATE spe_core Threads::Threads)
    add_test(NAME soak_wfs_algoswap_race
             COMMAND soak_wfs_algoswap_race
             CONFIGURATIONS Release)
    set_tests_properties(soak_wfs_algoswap_race PROPERTIES TIMEOUT 120)

    # v0.9 Lane F4 (F4b / AC9) — scene-save concurrency gate. One "audio" thread
    # drives obj_cache_ writes through the production drain + three-buffer
    # publish; a "control" thread calls snapshotObjects() mid-flight (>=150
    # rounds). Asserts ZERO data races (under -fsanitize=thread) AND a
    # correlated-field-pair no-tearing invariant in every snapshot.
    add_executable(soak_scene_save_race soak_scene_save_race.cpp)
    target_link_libraries(soak_scene_save_race PRIVATE spe_core Threads::Threads)
    add_test(NAME soak_scene_save_race
             COMMAND soak_scene_save_race
             CONFIGURATIONS Release)
    set_tests_properties(soak_scene_save_race PROPERTIES TIMEOUT 120)

    # Phase 4.3 Inc 4 — live speaker-layout swap quiescence handshake. An "audio"
    # thread spins audioBlock() (draining noise/output commands that read
    # layout_/noise_chans_/spk_limiters_ UNGATED, plus rendering an active object)
    # while a "control" thread applies a new layout (8ch<->12ch) >=200 times via
    # applyLayoutLive. Asserts ZERO data races (under -fsanitize=thread) on the
    # reallocated layout state AND every output sample finite AND non-silent after
    # settle (the rebuild published correctly).
    add_executable(soak_layout_swap_race soak_layout_swap_race.cpp)
    target_link_libraries(soak_layout_swap_race PRIVATE spe_core Threads::Threads)
    add_test(NAME soak_layout_swap_race
             COMMAND soak_layout_swap_race
             CONFIGURATIONS Release)
    set_tests_properties(soak_layout_swap_race PROPERTIES TIMEOUT 120)

    # v0.9 Lane C (C-M3 + C-M4) — RT-budget + memory-footprint measurement.
    # Drives audioBlock() at the heaviest config (8ch + VBAP + B2 binaural
    # Direct per-object) across object counts {8,16,32,64,96,128}; counts above
    # the compiled cap (spe::MAX_OBJECTS) are skipped. Gates peak <= 50% RT
    # budget AND xruns == 0 at the cap, and max-RSS < 100 MB. Builds in BOTH the
    # 64 and 128 configs (the sweep self-limits to the compiled cap). Needs the
    # synthetic SOFA fixture to prime the per-object OlaConvolvers.
    if(EXISTS "${CMAKE_SOURCE_DIR}/core/tests/fixtures")
        set(SPE_PERF_FIXTURES_DIR "${CMAKE_SOURCE_DIR}/core/tests/fixtures")
    else()
        set(SPE_PERF_FIXTURES_DIR "${CMAKE_SOURCE_DIR}/tests/fixtures")
    endif()
    add_executable(perf_obj_block_time perf_obj_block_time.cpp)
    target_link_libraries(perf_obj_block_time PRIVATE spe_core Threads::Threads)
    target_compile_definitions(perf_obj_block_time PRIVATE
        SPE_FIXTURES_DIR="${SPE_PERF_FIXTURES_DIR}")
    add_test(NAME perf_obj_block_time
             COMMAND perf_obj_block_time
             CONFIGURATIONS Release)
    set_tests_properties(perf_obj_block_time PROPERTIES TIMEOUT 300)

    # v1.0 Phase 1.4 — RT-budget SPEAKER sweep. Fixes the O(spk) measurement gap
    # (perf_obj_block_time sweeps objects at fixed 8 speakers). Sweeps speaker
    # counts {8..128} at 32 active objects, render-only and render+room; feeds
    # docs/RT_BUDGET_SPEAKERS.md. Gates render+room <100% budget up to 64 spk.
    add_executable(perf_speaker_sweep perf_speaker_sweep.cpp)
    target_link_libraries(perf_speaker_sweep PRIVATE spe_core Threads::Threads)
    add_test(NAME perf_speaker_sweep
             COMMAND perf_speaker_sweep
             CONFIGURATIONS Release)
    set_tests_properties(perf_speaker_sweep PROPERTIES TIMEOUT 300)
endif()
