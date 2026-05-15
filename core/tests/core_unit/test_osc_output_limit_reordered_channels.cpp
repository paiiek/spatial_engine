// v0.3.1 merge-gate test:
// /output/N/limit must address the speaker whose YAML channel: N matches,
// not the YAML position. Uses the reordered fixture {8,1,2,7,3,6,4,5}.

#include "core/SpatialEngine.h"
#include "geometry/LayoutLoader.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"

#include <cmath>
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

#define CHECK_NEAR(a, b, tol, msg)                                        \
    do {                                                                  \
        float _a = (a), _b = (b);                                         \
        if (std::abs(_a - _b) > (tol)) {                                  \
            std::fprintf(stderr,                                          \
                "FAIL: %s |%.6f - %.6f| = %.2e > %.2e\n",                 \
                msg, (double)_a, (double)_b,                              \
                (double)std::abs(_a-_b), (double)(tol));                  \
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

    // ChannelLimiter default threshold is 1.0. Send /output/5/limit ,f -3.0
    // and verify the threshold at vector index 7 changes; others stay at 1.0.
    spe::ipc::Command cmd;
    cmd.tag = spe::ipc::CommandTag::OutputLimit;
    spe::ipc::PayloadOutputLimit p; p.channel = 5; p.threshold_db = -3.f;
    cmd.payload = p;
    engine.dispatchCommand(cmd);

    drive_one_block(engine, 8);

    const float expected = std::pow(10.f, -3.f / 20.f);
    CHECK_NEAR(engine.spkLimiterThresholdAt(7), expected, 1e-4f,
               "/output/5/limit -3 dB routes to vector idx 7");

    for (size_t i = 0; i < engine.spkLimitersSize(); ++i) {
        if (i == 7) continue;
        const float t = engine.spkLimiterThresholdAt(i);
        // Default ChannelLimiter threshold is 1.0; reject 10^(-3/20)≈0.707.
        if (std::abs(t - expected) < 1e-4f) {
            std::fprintf(stderr,
                "FAIL: limiter threshold leaked into vector idx %zu (=%.6f)\n",
                i, (double)t);
            ++failures;
        }
    }

    // Negative case: /output/9/limit (unmapped) must not perturb idx 7.
    const float idx7_before = engine.spkLimiterThresholdAt(7);
    spe::ipc::Command cmd_bad;
    cmd_bad.tag = spe::ipc::CommandTag::OutputLimit;
    spe::ipc::PayloadOutputLimit p_bad; p_bad.channel = 9; p_bad.threshold_db = -40.f;
    cmd_bad.payload = p_bad;
    engine.dispatchCommand(cmd_bad);
    drive_one_block(engine, 8);

    CHECK_NEAR(engine.spkLimiterThresholdAt(7), idx7_before, 1e-6f,
               "unmapped /output/9/limit did not modify any limiter");

    if (failures == 0) {
        std::printf("test_osc_output_limit_reordered_channels OK\n");
        return 0;
    }
    std::fprintf(stderr,
        "test_osc_output_limit_reordered_channels FAILED: %d assertion(s)\n",
        failures);
    return 1;
}
