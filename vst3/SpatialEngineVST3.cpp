// vst3/SpatialEngineVST3.cpp
//
// Phase B placeholder for the VST3 plugin entry symbols. Builds a working
// shared library so the build system can be exercised end-to-end without
// the Steinberg vst3sdk available. Phase C will replace these stubs with a
// real IPluginFactory implementation backed by a SpatialEngine instance.
//
// The exported symbols intentionally return inert values:
//   - GetPluginFactory() → nullptr (host sees "no classes", skips cleanly)
//   - ModuleEntry/Exit   → true (POSIX bundle init succeeds without DSO unload errors)
//
// Hosts that probe the bundle will not list any plugin classes; this is the
// expected behaviour for a build-only scaffold.

#include <cstdint>

#if defined(_WIN32)
  #define SPATIAL_ENGINE_VST3_EXPORT __declspec(dllexport)
#else
  #define SPATIAL_ENGINE_VST3_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

// Minimum VST3 entry symbol expected by SDK loaders.
SPATIAL_ENGINE_VST3_EXPORT void* GetPluginFactory();
SPATIAL_ENGINE_VST3_EXPORT void* GetPluginFactory() { return nullptr; }

// Linux/macOS bundle init/exit. Returning true keeps the host happy
// during the load-and-probe phase even when no factory is published.
SPATIAL_ENGINE_VST3_EXPORT bool ModuleEntry(void* /*sharedLibraryHandle*/);
SPATIAL_ENGINE_VST3_EXPORT bool ModuleEntry(void* /*sharedLibraryHandle*/) { return true; }

SPATIAL_ENGINE_VST3_EXPORT bool ModuleExit();
SPATIAL_ENGINE_VST3_EXPORT bool ModuleExit() { return true; }

} // extern "C"
