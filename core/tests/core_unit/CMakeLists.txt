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
set(SPE_CONFIGS_DIR "${CMAKE_SOURCE_DIR}/configs")

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

