// core/src/hrtf/HrtfCatalog.h
//
// B-M1 — HRTF catalog loader (control-thread only, never audio-thread).
// Parses assets/hrtf/catalog.json using a hand-rolled parser (no new dep;
// yaml-cpp is the only JSON/YAML dep and it does not parse JSON objects).
//
// INVARIANT enforced at load time:
//   auto_fetch == true  =>  license starts with "CC-BY"

#pragma once

#include <string>
#include <vector>

namespace spe::hrtf {

struct CatalogEntry {
    std::string name;
    std::string display_name;
    std::string speh_path;
    std::string sofa_source_url;
    std::string license;
    std::string attribution;
    bool        auto_fetch       = false;
    int         n_positions_hint = 0;
    int         ir_length_hint   = 0;
};

// Typed error returned when the JSON cannot be parsed or fails the invariant.
struct CatalogError {
    std::string message;
};

// Result type — either a vector of entries or an error.
struct CatalogResult {
    bool ok = false;
    std::vector<CatalogEntry> entries;
    CatalogError              error;
};

// Load and validate catalog.json from disk.
// Control-thread only — do NOT call from the audio thread.
CatalogResult loadCatalog(const std::string& json_path);

// Convenience wrapper: load once, cache in a static.
// Returns nullptr if loading failed.  Thread-hostile (control thread only).
class HrtfCatalog {
public:
    // Load from the given path.  Returns false and populates last_error() on
    // failure.
    bool load(const std::string& json_path);

    const std::vector<CatalogEntry>& entries() const { return entries_; }

    // Returns nullptr if not found.
    const CatalogEntry* find(const std::string& name) const;

    const std::string& lastError() const { return last_error_; }

private:
    std::vector<CatalogEntry> entries_;
    std::string               last_error_;
};

} // namespace spe::hrtf
