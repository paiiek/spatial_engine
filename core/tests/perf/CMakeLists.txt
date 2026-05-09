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
endif()
