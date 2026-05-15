// v0.3.1: SpeakerLayout::channelToIndex unit tests.
// Verifies the YAML channel → vector index translation table built by
// LayoutLoader for sequential, reordered, and out-of-range queries.

#include "geometry/LayoutLoader.h"
#include "geometry/SpeakerLayout.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int failures = 0;

#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL: %s\n", msg);                      \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static std::string write_temp(const std::string& name, const std::string& content) {
    namespace fs = std::filesystem;
    auto path = (fs::temp_directory_path() / name).string();
    std::ofstream f(path);
    f << content;
    return path;
}

int main(int argc, char** argv) {
    std::string configs_dir;
    std::string fixtures_dir;
    if (argc >= 2) {
        configs_dir = std::string(argv[1]) + "/";
    } else {
        configs_dir = std::string(SPE_CONFIGS_DIR) + "/";
    }
    if (argc >= 3) {
        fixtures_dir = std::string(argv[2]) + "/";
    } else {
        fixtures_dir = std::string(SPE_FIXTURES_DIR) + "/";
    }

    using namespace spe::geometry;

    // --- Sequential layout (lab_8ch.yaml) ---
    {
        auto result = load_layout(configs_dir + "lab_8ch.yaml");
        CHECK(is_ok(result), "lab_8ch.yaml loads OK");
        if (is_ok(result)) {
            const auto& layout = std::get<SpeakerLayout>(result);
            for (int ch = 1; ch <= 8; ++ch) {
                int idx = layout.channelToIndex(ch);
                if (idx != ch - 1) {
                    std::fprintf(stderr,
                        "FAIL: sequential channelToIndex(%d) = %d, expected %d\n",
                        ch, idx, ch - 1);
                    ++failures;
                }
            }
            // Out-of-range queries
            CHECK(layout.channelToIndex(0)   == -1, "sequential: channelToIndex(0) == -1");
            CHECK(layout.channelToIndex(9)   == -1, "sequential: channelToIndex(9) == -1");
            CHECK(layout.channelToIndex(99)  == -1, "sequential: channelToIndex(99) == -1");
            CHECK(layout.channelToIndex(-1)  == -1, "sequential: channelToIndex(-1) == -1");
            CHECK(layout.channelToIndex(SpeakerLayout::kMaxYamlChannel + 1) == -1,
                  "sequential: channelToIndex(kMax+1) == -1");
        }
    }

    // --- Reordered layout (lab_irregular_reordered.yaml) ---
    // YAML channels in declaration order: {8, 1, 2, 7, 3, 6, 4, 5}
    // → channel_to_idx_: ch1→1, ch2→2, ch3→4, ch4→6, ch5→7, ch6→5, ch7→3, ch8→0
    {
        auto result = load_layout(fixtures_dir + "lab_irregular_reordered.yaml");
        CHECK(is_ok(result), "lab_irregular_reordered.yaml loads OK");
        if (is_ok(result)) {
            const auto& layout = std::get<SpeakerLayout>(result);
            CHECK(layout.speakers.size() == 8, "reordered: 8 speakers");
            CHECK(layout.channelToIndex(8) == 0, "reordered: channelToIndex(8) == 0");
            CHECK(layout.channelToIndex(1) == 1, "reordered: channelToIndex(1) == 1");
            CHECK(layout.channelToIndex(2) == 2, "reordered: channelToIndex(2) == 2");
            CHECK(layout.channelToIndex(7) == 3, "reordered: channelToIndex(7) == 3");
            CHECK(layout.channelToIndex(3) == 4, "reordered: channelToIndex(3) == 4");
            CHECK(layout.channelToIndex(6) == 5, "reordered: channelToIndex(6) == 5");
            CHECK(layout.channelToIndex(4) == 6, "reordered: channelToIndex(4) == 6");
            CHECK(layout.channelToIndex(5) == 7, "reordered: channelToIndex(5) == 7");
            CHECK(layout.channelToIndex(0)  == -1, "reordered: channelToIndex(0) == -1");
            CHECK(layout.channelToIndex(9)  == -1, "reordered: channelToIndex(9) == -1");
            CHECK(layout.channelToIndex(99) == -1, "reordered: channelToIndex(99) == -1");
        }
    }

    // --- Sparse synthesised layout (channels 1, 3, 5 only) ---
    {
        auto path = write_temp("v031_sparse_layout.yaml",
            "version: \"1.0\"\nname: \"sparse\"\nspeakers:\n"
            "  - id: 1\n    channel: 1\n    az_deg:   0\n    el_deg: 0\n    dist_m: 1.0\n"
            "  - id: 2\n    channel: 3\n    az_deg:  90\n    el_deg: 0\n    dist_m: 1.0\n"
            "  - id: 3\n    channel: 5\n    az_deg: 180\n    el_deg: 0\n    dist_m: 1.0\n");
        auto result = load_layout(path);
        CHECK(is_ok(result), "sparse layout loads OK");
        if (is_ok(result)) {
            const auto& layout = std::get<SpeakerLayout>(result);
            CHECK(layout.channelToIndex(1) == 0, "sparse: channelToIndex(1) == 0");
            CHECK(layout.channelToIndex(3) == 1, "sparse: channelToIndex(3) == 1");
            CHECK(layout.channelToIndex(5) == 2, "sparse: channelToIndex(5) == 2");
            // Gaps stay unmapped.
            CHECK(layout.channelToIndex(2) == -1, "sparse: channelToIndex(2) == -1 (gap)");
            CHECK(layout.channelToIndex(4) == -1, "sparse: channelToIndex(4) == -1 (gap)");
        }
    }

    if (failures == 0) {
        std::printf("test_speaker_layout_channel_lookup OK\n");
        return 0;
    }
    std::fprintf(stderr, "test_speaker_layout_channel_lookup FAILED: %d assertion(s)\n", failures);
    return 1;
}
