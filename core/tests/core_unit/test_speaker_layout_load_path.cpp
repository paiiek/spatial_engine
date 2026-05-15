// test_speaker_layout_load_path.cpp
// P2: load each production YAML + the irregular-reordered fixture and
//     assert is_ok(result) + expected channel count.

#include "geometry/LayoutLoader.h"
#include "geometry/SpeakerLayout.h"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond, msg)                                         \
    do {                                                         \
        if (!(cond)) {                                           \
            std::fprintf(stderr, "FAIL: %s\n", (msg));          \
            ++failures;                                          \
        }                                                        \
    } while (0)

int main(int argc, char** argv) {
    std::string configs_dir;
    std::string fixtures_dir;
    if (argc >= 2) configs_dir  = std::string(argv[1]) + "/";
    else           configs_dir  = std::string(SPE_CONFIGS_DIR) + "/";
    if (argc >= 3) fixtures_dir = std::string(argv[2]) + "/";
    else           fixtures_dir = std::string(SPE_FIXTURES_DIR) + "/";

    using namespace spe::geometry;

    struct Case { std::string path; size_t expected_ch; };
    Case cases[] = {
        { configs_dir + "lab_4ch.yaml",              4 },
        { configs_dir + "lab_8ch.yaml",              8 },
        { configs_dir + "lab_8ch_aligned.yaml",      8 },
        { configs_dir + "lab_8ch_irregular.yaml",    8 },
        { fixtures_dir + "lab_irregular_reordered.yaml", 8 },
    };

    for (auto& c : cases) {
        auto r = load_layout(c.path);
        CHECK(is_ok(r), (std::string("load_layout ok: ") + c.path).c_str());
        if (is_ok(r)) {
            auto& layout = std::get<SpeakerLayout>(r);
            CHECK(layout.speakers.size() == c.expected_ch,
                  (std::string("channel count: ") + c.path).c_str());
        }
    }

    if (failures == 0) {
        std::puts("PASS test_speaker_layout_load_path");
        return 0;
    }
    std::fprintf(stderr, "test_speaker_layout_load_path FAILED: %d assertion(s)\n", failures);
    return 1;
}
