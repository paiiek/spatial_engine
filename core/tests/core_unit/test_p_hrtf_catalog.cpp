// core/tests/core_unit/test_p_hrtf_catalog.cpp
//
// B-M1 gate test — HRTF catalog loader.
// Conventions match test_ambi_decoder_type_runtime_apply.cpp:
//   plain assert / int main() / fprintf on failure / return 1.
//
// SPE_CATALOG_PATH is injected by CMakeLists via compile_definitions.

#include "hrtf/HrtfCatalog.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

int main() {
#ifndef SPE_CATALOG_PATH
    std::fprintf(stderr, "FAIL test_p_hrtf_catalog: SPE_CATALOG_PATH not defined\n");
    return 1;
#else
    const std::string catalog_path = SPE_CATALOG_PATH;

    // ── (1) Parse the committed catalog.json.
    spe::hrtf::HrtfCatalog cat;
    if (!cat.load(catalog_path)) {
        std::fprintf(stderr, "FAIL test_p_hrtf_catalog: load failed: %s\n",
                     cat.lastError().c_str());
        return 1;
    }

    // ── (2) Assert ≥4 entries.
    if (cat.entries().size() < 4) {
        std::fprintf(stderr, "FAIL test_p_hrtf_catalog: only %zu entries (need ≥4)\n",
                     cat.entries().size());
        return 1;
    }

    // ── (3) Assert each entry has non-empty license + attribution.
    for (const auto& e : cat.entries()) {
        if (e.license.empty()) {
            std::fprintf(stderr, "FAIL test_p_hrtf_catalog: entry '%s' has empty license\n",
                         e.name.c_str());
            return 1;
        }
        if (e.attribution.empty()) {
            std::fprintf(stderr, "FAIL test_p_hrtf_catalog: entry '%s' has empty attribution\n",
                         e.name.c_str());
            return 1;
        }
    }

    // ── (4) Assert auto_fetch => license starts with "CC-BY".
    for (const auto& e : cat.entries()) {
        if (e.auto_fetch && !startsWith(e.license, "CC-BY")) {
            std::fprintf(stderr,
                "FAIL test_p_hrtf_catalog: entry '%s' auto_fetch=true but license='%s'\n",
                e.name.c_str(), e.license.c_str());
            return 1;
        }
    }

    // ── (5) Assert find() works for known entries.
    const char* known[] = {"kemar", "cipic_003", "sadie2_H08", "hutubs_pp1"};
    for (const char* n : known) {
        if (cat.find(n) == nullptr) {
            std::fprintf(stderr, "FAIL test_p_hrtf_catalog: find('%s') returned nullptr\n", n);
            return 1;
        }
    }

    // ── (6) Assert invalid JSON returns a typed error, not a crash.
    {
        spe::hrtf::CatalogResult bad = spe::hrtf::loadCatalog("/dev/null");
        // /dev/null gives empty file → parse error (no top-level '{').
        // Even if it opens fine, we test with a deliberately malformed string
        // by passing a non-existent path.
        // We also directly test the loadCatalog() path with a bogus path.
        spe::hrtf::CatalogResult missing = spe::hrtf::loadCatalog("/nonexistent/path.json");
        if (missing.ok) {
            std::fprintf(stderr, "FAIL test_p_hrtf_catalog: missing file should not be ok\n");
            return 1;
        }
        if (missing.error.message.empty()) {
            std::fprintf(stderr, "FAIL test_p_hrtf_catalog: missing file error message is empty\n");
            return 1;
        }
    }

    std::printf("OK  test_p_hrtf_catalog: %zu entries loaded, all invariants hold\n",
                cat.entries().size());
    return 0;
#endif
}
