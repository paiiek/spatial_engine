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
endif()
