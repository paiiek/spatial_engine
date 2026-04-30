// P2: LayoutLoader — load 3 valid YAMLs; assert 3 named errors on malformed input.

#include "geometry/LayoutLoader.h"
#include "geometry/SpeakerLayout.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

static int failures = 0;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            std::fprintf(stderr, "FAIL: %s\n", msg);           \
            ++failures;                                         \
        }                                                       \
    } while (0)

// Write a temp YAML file and return its path.
static std::string write_temp(const std::string& name, const std::string& content) {
    namespace fs = std::filesystem;
    auto path = (fs::temp_directory_path() / name).string();
    std::ofstream f(path);
    f << content;
    return path;
}

int main(int argc, char** argv) {
    // Configs dir passed as argv[1] from CTest, or derived from source tree.
    std::string configs_dir;
    if (argc >= 2) {
        configs_dir = std::string(argv[1]) + "/";
    } else {
        // WHY: fallback for manual runs from build dir; CTest passes the dir.
        configs_dir = std::string(SPE_CONFIGS_DIR) + "/";
    }

    using namespace spe::geometry;

    // --- Valid layouts ---

    {
        auto result = load_layout(configs_dir + "lab_4ch.yaml");
        CHECK(is_ok(result), "lab_4ch.yaml loads OK");
        if (is_ok(result)) {
            const auto& layout = std::get<SpeakerLayout>(result);
            CHECK(layout.speakers.size() == 4,    "lab_4ch has 4 speakers");
            CHECK(layout.name == "lab_4ch",        "lab_4ch name");
            CHECK(layout.regularity == Regularity::CIRCULAR, "lab_4ch regularity CIRCULAR");
        }
    }

    {
        auto result = load_layout(configs_dir + "lab_8ch.yaml");
        CHECK(is_ok(result), "lab_8ch.yaml loads OK");
        if (is_ok(result)) {
            const auto& layout = std::get<SpeakerLayout>(result);
            CHECK(layout.speakers.size() == 8,    "lab_8ch has 8 speakers");
            CHECK(layout.name == "lab_8ch",        "lab_8ch name");
        }
    }

    {
        auto result = load_layout(configs_dir + "lab_8ch_irregular.yaml");
        CHECK(is_ok(result), "lab_8ch_irregular.yaml loads OK");
        if (is_ok(result)) {
            const auto& layout = std::get<SpeakerLayout>(result);
            CHECK(layout.speakers.size() == 8,    "lab_8ch_irregular has 8 speakers");
            CHECK(layout.regularity == Regularity::IRREGULAR, "lab_8ch_irregular regularity");
        }
    }

    // --- Malformed: missing speakers ---
    {
        auto path = write_temp("bad_no_speakers.yaml",
            "version: \"1.0\"\nname: \"bad\"\n");
        auto result = load_layout(path);
        CHECK(!is_ok(result), "missing speakers -> error");
        if (!is_ok(result)) {
            const auto& err = std::get<std::string>(result);
            CHECK(err.find(kErrMissingSpeakers) != std::string::npos,
                  "missing speakers error string");
        }
    }

    // --- Malformed: negative channel ---
    {
        auto path = write_temp("bad_neg_channel.yaml",
            "version: \"1.0\"\nname: \"bad\"\nspeakers:\n"
            "  - id: 1\n    channel: -1\n    az_deg: 0\n    el_deg: 0\n    dist_m: 1.0\n");
        auto result = load_layout(path);
        CHECK(!is_ok(result), "negative channel -> error");
        if (!is_ok(result)) {
            const auto& err = std::get<std::string>(result);
            CHECK(err.find(kErrNegativeChannel) != std::string::npos,
                  "negative channel error string");
        }
    }

    // --- Malformed: both xyz and spherical ---
    {
        auto path = write_temp("bad_both_pos.yaml",
            "version: \"1.0\"\nname: \"bad\"\nspeakers:\n"
            "  - id: 1\n    channel: 1\n"
            "    az_deg: 0\n    el_deg: 0\n    dist_m: 1.0\n"
            "    x: 0\n    y: 0\n    z: 1\n");
        auto result = load_layout(path);
        CHECK(!is_ok(result), "both xyz and spherical -> error");
        if (!is_ok(result)) {
            const auto& err = std::get<std::string>(result);
            CHECK(err.find(kErrBothXyzAndSphere) != std::string::npos,
                  "both xyz+spherical error string");
        }
    }

    if (failures == 0) {
        std::printf("p2_layout_loader OK\n");
        return 0;
    }
    std::fprintf(stderr, "p2_layout_loader FAILED: %d assertion(s)\n", failures);
    return 1;
}
