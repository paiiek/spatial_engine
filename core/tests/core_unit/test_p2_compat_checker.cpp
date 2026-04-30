// P2: LayoutCompatibilityChecker — register 3 layouts x 3 algorithms = 9 pairs.
// At P2 every pair must return Compatible.

#include "geometry/LayoutLoader.h"
#include "geometry/SpeakerLayout.h"
#include "render/LayoutCompatibilityChecker.h"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            std::fprintf(stderr, "FAIL: %s\n", msg);           \
            ++failures;                                         \
        }                                                       \
    } while (0)

int main(int argc, char** argv) {
    std::string configs_dir;
    if (argc >= 2) {
        configs_dir = std::string(argv[1]) + "/";
    } else {
        configs_dir = std::string(SPE_CONFIGS_DIR) + "/";
    }

    using namespace spe::geometry;
    using namespace spe::render;

    // Load all 3 layouts.
    auto r4   = load_layout(configs_dir + "lab_4ch.yaml");
    auto r8   = load_layout(configs_dir + "lab_8ch.yaml");
    auto r8i  = load_layout(configs_dir + "lab_8ch_irregular.yaml");

    CHECK(is_ok(r4),  "lab_4ch loads for compat test");
    CHECK(is_ok(r8),  "lab_8ch loads for compat test");
    CHECK(is_ok(r8i), "lab_8ch_irregular loads for compat test");

    if (!is_ok(r4) || !is_ok(r8) || !is_ok(r8i)) {
        std::fprintf(stderr, "p2_compat_checker: cannot proceed, layout load failed\n");
        return 1;
    }

    const auto& l4  = std::get<SpeakerLayout>(r4);
    const auto& l8  = std::get<SpeakerLayout>(r8);
    const auto& l8i = std::get<SpeakerLayout>(r8i);

    LayoutCompatibilityChecker checker;

    // Register 9 pairs.
    const Algorithm algos[] = {Algorithm::VBAP, Algorithm::WFS, Algorithm::DBAP};
    const SpeakerLayout* layouts[] = {&l4, &l8, &l8i};
    const char* layout_names[] = {"lab_4ch", "lab_8ch", "lab_8ch_irregular"};

    for (int li = 0; li < 3; ++li) {
        for (int ai = 0; ai < 3; ++ai) {
            checker.register_rule(layout_names[li], algos[ai], CompatStatus::Compatible);
        }
    }

    CHECK(checker.rules().size() == 9, "9 rules registered");

    // P2: all 9 pairs must return Compatible.
    int pair_count = 0;
    for (int li = 0; li < 3; ++li) {
        for (int ai = 0; ai < 3; ++ai) {
            auto result = checker.validate(*layouts[li], algos[ai]);
            CHECK(result.status == CompatStatus::Compatible, "P2: all pairs Compatible");
            ++pair_count;
        }
    }

    CHECK(pair_count == 9, "validated all 9 pairs");

    if (failures == 0) {
        std::printf("p2_compat_checker OK: 9 pairs all Compatible\n");
        return 0;
    }
    std::fprintf(stderr, "p2_compat_checker FAILED: %d assertion(s)\n", failures);
    return 1;
}
