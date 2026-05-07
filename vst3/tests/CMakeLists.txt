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

# ---- ctest dependency chain (AM-R3-9) --------------------------------------
# Future step tests will add: set_tests_properties(... PROPERTIES DEPENDS vst3_host_fixture)
