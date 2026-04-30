cmake_minimum_required(VERSION 3.20)
project(spatial_engine
    VERSION 0.1.0
    DESCRIPTION "Real-time object-based immersive audio rendering engine"
    LANGUAGES CXX)

# Workspace-level settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Baseline diagnostic flags
# (RTTI stays ON: JUCE 7.x relies on dynamic_cast in several modules.)
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# Warnings-as-errors only on dedicated profile (CI gate)
option(SPATIAL_ENGINE_WERROR "Treat warnings as errors" OFF)
if(SPATIAL_ENGINE_WERROR)
    add_compile_options(-Werror)
endif()

# Test-build-only RT-asserts and click-detector
option(SPATIAL_ENGINE_RT_ASSERTS "Enable RT_ASSERT_NO_ALLOC and ClickDetectorFFT in test build" OFF)

enable_testing()
add_subdirectory(core)
