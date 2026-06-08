// core/src/geometry/LayoutLoader.h

#pragma once

#include "geometry/SpeakerLayout.h"

#include <string>
#include <variant>

namespace spe::geometry {

// Named error strings returned on parse failure.
// Tests assert these exact substrings are present in the error.
inline constexpr const char* kErrMissingSpeakers  = "missing 'speakers'";
inline constexpr const char* kErrNegativeChannel  = "negative channel";
inline constexpr const char* kErrBothXyzAndSphere = "both xyz and spherical";
inline constexpr const char* kErrMissingVersion   = "missing 'version'";
inline constexpr const char* kErrMissingName      = "missing 'name'";
inline constexpr const char* kErrBadChannelValue  = "channel must be >= 1";
inline constexpr const char* kErrDuplicateChannel = "duplicate channel";
inline constexpr const char* kErrChannelTooLarge  = "channel exceeds kMaxYamlChannel";

// WHY: std::expected is gcc-13+; use variant for broader compatibility.
using LayoutResult = std::variant<SpeakerLayout, std::string>;

// Load a speaker layout from a YAML file.
// Returns LayoutResult with SpeakerLayout on success, std::string error on failure.
LayoutResult load_layout(const std::string& yaml_path);

// Serialise a speaker layout to a YAML file readable by load_layout().
// Writes Cartesian x/y/z (the stored geometry) at full float precision so
// load_layout(save_layout(L)) round-trips the speakers exactly. Returns true on
// success, false on any I/O failure. Control-thread only (does file I/O).
bool save_layout(const SpeakerLayout& layout, const std::string& yaml_path);

// Convenience: returns true if result holds a SpeakerLayout.
inline bool is_ok(const LayoutResult& r) {
    return std::holds_alternative<SpeakerLayout>(r);
}

}  // namespace spe::geometry
