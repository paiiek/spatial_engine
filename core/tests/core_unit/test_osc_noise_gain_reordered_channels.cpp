// v0.3.1 merge-gate test:
// /noise/N/gain must address the speaker whose YAML channel: N matches,
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

    // Default noise gain_lin is 0 on every channel.
    for (size_t i = 0; i < engine.noiseChansSize(); ++i) {
        CHECK_NEAR(engine.noiseGainLinAt(i), 0.f, 1e-6f, "baseline noise gain == 0");
    }

    // Send /noise/5/gain ,f -6.0 → vector idx 7.
    spe::ipc::Command cmd;
    cmd.tag = spe::ipc::CommandTag::NoiseGain;
    spe::ipc::PayloadNoiseGain p; p.channel = 5; p.gain_db = -6.f;
    cmd.payload = p;
    engine.dispatchCommand(cmd);

    drive_one_block(engine, 8);

    const float expected = std::pow(10.f, -6.f / 20.f);
    CHECK_NEAR(engine.noiseGainLinAt(7), expected, 1e-4f,
               "/noise/5/gain -6 dB routes to vector idx 7");

    for (size_t i = 0; i < engine.noiseChansSize(); ++i) {
        if (i == 7) continue;
        CHECK_NEAR(engine.noiseGainLinAt(i), 0.f, 1e-6f,
                   "other vector idx noise gain still 0");
    }

    // Negative case: /noise/9/gain (unmapped) → idx 7 unchanged, others stay 0.
    spe::ipc::Command cmd_bad;
    cmd_bad.tag = spe::ipc::CommandTag::NoiseGain;
    spe::ipc::PayloadNoiseGain p_bad; p_bad.channel = 9; p_bad.gain_db = -3.f;
    cmd_bad.payload = p_bad;
    engine.dispatchCommand(cmd_bad);
    drive_one_block(engine, 8);

    CHECK_NEAR(engine.noiseGainLinAt(7), expected, 1e-4f,
               "idx 7 still expected gain after unmapped command");
    const float bad_target = std::pow(10.f, -3.f / 20.f);
    for (size_t i = 0; i < engine.noiseChansSize(); ++i) {
        if (i == 7) continue;
        const float v = engine.noiseGainLinAt(i);
        if (std::abs(v - bad_target) < 1e-4f) {
            std::fprintf(stderr,
                "FAIL: unmapped /noise/9/gain bled into vector idx %zu\n", i);
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("test_osc_noise_gain_reordered_channels OK\n");
        return 0;
    }
    std::fprintf(stderr,
        "test_osc_noise_gain_reordered_channels FAILED: %d assertion(s)\n",
        failures);
    return 1;
}
