// v0.3.1: LayoutLoader must reject layouts that declare the same YAML
// channel on two speakers. Silent shadowing under the channel→index lookup
// would mis-route OSC automation.

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

int main() {
    using namespace spe::geometry;

    // Two speakers declaring `channel: 3` → error.
    {
        auto path = write_temp("v031_duplicate_channel.yaml",
            "version: \"1.0\"\nname: \"dup\"\nspeakers:\n"
            "  - id: 1\n    channel: 3\n    az_deg:  0\n    el_deg: 0\n    dist_m: 1.0\n"
            "  - id: 2\n    channel: 5\n    az_deg: 90\n    el_deg: 0\n    dist_m: 1.0\n"
            "  - id: 3\n    channel: 3\n    az_deg: 180\n    el_deg: 0\n    dist_m: 1.0\n");
        auto result = load_layout(path);
        CHECK(!is_ok(result), "duplicate channel -> error");
        if (!is_ok(result)) {
            const auto& err = std::get<std::string>(result);
            CHECK(err.find(kErrDuplicateChannel) != std::string::npos,
                  "duplicate channel error string contains 'duplicate channel'");
        }
    }

    // Channel exceeding kMaxYamlChannel → error.
    {
        auto path = write_temp("v031_channel_too_large.yaml",
            "version: \"1.0\"\nname: \"big\"\nspeakers:\n"
            "  - id: 1\n    channel: 9999\n    az_deg: 0\n    el_deg: 0\n    dist_m: 1.0\n");
        auto result = load_layout(path);
        CHECK(!is_ok(result), "channel too large -> error");
        if (!is_ok(result)) {
            const auto& err = std::get<std::string>(result);
            CHECK(err.find(kErrChannelTooLarge) != std::string::npos,
                  "channel-too-large error string");
        }
    }

    if (failures == 0) {
        std::printf("test_channel_map_duplicate_rejects OK\n");
        return 0;
    }
    std::fprintf(stderr, "test_channel_map_duplicate_rejects FAILED: %d assertion(s)\n", failures);
    return 1;
}
