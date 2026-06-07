// v0.3.1 merge-gate test:
// /noise/N/type must address the speaker whose YAML channel: N matches,
// not the YAML position. Uses the reordered fixture {8,1,2,7,3,6,4,5}.

#include "core/SpatialEngine.h"
#include "geometry/LayoutLoader.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"

#include <cstdio>
#include <string>
#include <variant>
#include <vector>

static int failures = 0;

#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL: %s\n", msg);                      \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static void drive_one_block(spe::core::SpatialEngine& engine, int n_spk) {
    constexpr int kFrames = 64;
    std::vector<std::vector<float>> bufs(static_cast<size_t>(n_spk),
                                         std::vector<float>(kFrames, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(n_spk));
    for (int s = 0; s < n_spk; ++s)
        ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    spe::audio_io::AudioBlock block;
    block.output_channels      = ptrs.data();
    block.output_channel_count = n_spk;
    block.num_frames           = kFrames;
    block.sample_rate          = 48000.0;
    engine.audioBlock(block);
}

int main(int argc, char** argv) {
    std::string fixtures_dir;
    if (argc >= 2) fixtures_dir = std::string(argv[1]) + "/";
    else           fixtures_dir = std::string(SPE_FIXTURES_DIR) + "/";

    auto result = spe::geometry::load_layout(fixtures_dir + "lab_irregular_reordered.yaml");
    CHECK(spe::geometry::is_ok(result), "reordered fixture loads");
    if (!spe::geometry::is_ok(result)) return 1;
    const auto& layout = std::get<spe::geometry::SpeakerLayout>(result);

    spe::core::SpatialEngine engine(0);
    engine.setLayout(layout);
    engine.prepareToPlay(48000.0, 64);

    // Default noise pink flag is false on every channel.
    for (size_t i = 0; i < engine.noiseChansSize(); ++i) {
        CHECK(engine.noisePinkAt(i) == false, "baseline: noise pink false");
    }

    // Send /noise/5/type ,s pink → vector idx 7 (channel_to_idx_[5]).
    spe::ipc::Command cmd;
    cmd.tag = spe::ipc::CommandTag::NoiseType;
    spe::ipc::PayloadNoiseType p; p.channel = 5; p.mode = 1;  // pink
    cmd.payload = p;
    engine.dispatchCommand(cmd);

    drive_one_block(engine, 8);

    CHECK(engine.noisePinkAt(7) == true,
          "/noise/5/type=pink routes to vector idx 7");
    for (size_t i = 0; i < engine.noiseChansSize(); ++i) {
        if (i == 7) continue;
        if (engine.noisePinkAt(i)) {
            std::fprintf(stderr,
                "FAIL: noise pink leaked into vector idx %zu\n", i);
            ++failures;
        }
    }

    // Negative case: /noise/9/type (unmapped) → no state mutation.
    spe::ipc::Command cmd_bad;
    cmd_bad.tag = spe::ipc::CommandTag::NoiseType;
    spe::ipc::PayloadNoiseType p_bad; p_bad.channel = 9; p_bad.mode = 1;  // pink
    cmd_bad.payload = p_bad;
    engine.dispatchCommand(cmd_bad);
    drive_one_block(engine, 8);

    for (size_t i = 0; i < engine.noiseChansSize(); ++i) {
        if (i == 7) {
            CHECK(engine.noisePinkAt(7) == true,
                  "idx 7 still pink after unmapped command");
        } else {
            CHECK(engine.noisePinkAt(i) == false,
                  "unmapped /noise/9/type did not flip any flag");
        }
    }

    if (failures == 0) {
        std::printf("test_osc_noise_type_reordered_channels OK\n");
        return 0;
    }
    std::fprintf(stderr,
        "test_osc_noise_type_reordered_channels FAILED: %d assertion(s)\n",
        failures);
    return 1;
}
