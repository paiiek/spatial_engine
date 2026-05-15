// v0.3.1 merge-gate test:
// /output/N/gain must address the speaker whose YAML channel: N matches,
// not the YAML position. Uses the reordered fixture {8,1,2,7,3,6,4,5}.

#include "core/SpatialEngine.h"
#include "geometry/LayoutLoader.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"

#include <array>
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

// Drive engine with one minimal audio block so the command FIFO is drained.
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

    // Confirm fixture geometry before exercising handlers.
    CHECK(layout.channelToIndex(5) == 7, "fixture: ch 5 -> vector idx 7");

    spe::core::SpatialEngine engine(0);
    engine.setLayout(layout);
    engine.prepareToPlay(48000.0, 64);

    // Capture baseline gain at vector index 7 BEFORE sending the command.
    // The reordered fixture declares gain_db: -8.0 on channel: 5 → 10^(-8/20).
    const float baseline_idx7 = engine.spkGainLinAt(7);
    CHECK_NEAR(baseline_idx7, std::pow(10.f, -8.f / 20.f), 1e-4f,
               "baseline gain at idx 7 reflects fixture gain_db: -8");

    // Snapshot pre-command gains for every vector index so we can verify
    // that ONLY vector idx 7 (= channel_to_idx_[5]) changes. The fixture
    // declares unique gain_db values per slot, but some are integer dB
    // values like -6, so we can't use -6 dB as the test mutation target
    // (it would collide with vector idx 5's baseline gain_db: -6.0).
    // Use -12.3 dB which does not collide with any fixture value.
    std::vector<float> before(engine.spkGainLinSize());
    for (size_t i = 0; i < before.size(); ++i) before[i] = engine.spkGainLinAt(i);

    spe::ipc::Command cmd;
    cmd.tag = spe::ipc::CommandTag::OutputGain;
    spe::ipc::PayloadOutputGain p; p.channel = 5; p.gain_db = -12.3f;
    cmd.payload = p;
    engine.dispatchCommand(cmd);

    drive_one_block(engine, 8);

    // Vector index 7 (= channel_to_idx_[5]) should now hold 10^(-12.3/20).
    const float expected = std::pow(10.f, -12.3f / 20.f);
    CHECK_NEAR(engine.spkGainLinAt(7), expected, 1e-4f,
               "/output/5/gain -12.3 dB routes to vector idx 7");

    // No other vector position should have changed.
    for (size_t i = 0; i < engine.spkGainLinSize(); ++i) {
        if (i == 7) continue;
        const float v = engine.spkGainLinAt(i);
        if (std::abs(v - before[i]) > 1e-6f) {
            std::fprintf(stderr,
                "FAIL: vector idx %zu unexpectedly mutated %.6f -> %.6f\n",
                i, (double)before[i], (double)v);
            ++failures;
        }
    }

    // Negative case: unmapped wire channel 9 → no state mutation anywhere.
    std::vector<float> snapshot(engine.spkGainLinSize());
    for (size_t i = 0; i < snapshot.size(); ++i)
        snapshot[i] = engine.spkGainLinAt(i);

    spe::ipc::Command cmd_bad;
    cmd_bad.tag = spe::ipc::CommandTag::OutputGain;
    spe::ipc::PayloadOutputGain p_bad; p_bad.channel = 9; p_bad.gain_db = -30.f;
    cmd_bad.payload = p_bad;
    engine.dispatchCommand(cmd_bad);
    drive_one_block(engine, 8);

    for (size_t i = 0; i < snapshot.size(); ++i) {
        const float v = engine.spkGainLinAt(i);
        if (std::abs(v - snapshot[i]) > 1e-6f) {
            std::fprintf(stderr,
                "FAIL: unmapped /output/9/gain mutated idx %zu (%.6f -> %.6f)\n",
                i, (double)snapshot[i], (double)v);
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("test_osc_output_gain_reordered_channels OK\n");
        return 0;
    }
    std::fprintf(stderr,
        "test_osc_output_gain_reordered_channels FAILED: %d assertion(s)\n",
        failures);
    return 1;
}
