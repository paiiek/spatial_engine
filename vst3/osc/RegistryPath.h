// vst3/osc/RegistryPath.h
// Header-only XDG path resolution for the plugin instance registry.
// Returns $XDG_CONFIG_HOME/spatial_engine/instances.json when XDG_CONFIG_HOME
// is set AND non-empty; otherwise ~/.config/spatial_engine/instances.json.
// XDG spec: set-but-empty $XDG_CONFIG_HOME falls back to ~/.config (v03-Q10).
// JUCE-free: no JUCE includes.
#pragma once

#include <string>
#include <cstdlib>

namespace spe::vst3::osc {

// Returns the absolute path to the registry file.
// No heap allocation on the hot path beyond the single std::string construction.
inline std::string registryPath()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    // XDG spec §3.1: if $XDG_CONFIG_HOME is not set OR is empty, use ~/.config
    std::string base;
    if (xdg && xdg[0] != '\0') {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        if (!home || home[0] == '\0') home = "/tmp";
        base = std::string(home) + "/.config";
    }
    return base + "/spatial_engine/instances.json";
}

} // namespace spe::vst3::osc
