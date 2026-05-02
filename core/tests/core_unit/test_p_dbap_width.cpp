// test_p_dbap_width.cpp
// B4 acceptance tests: DBAP width精密化 — 3 virtual-source power-sum model.
//
// Tests:
//   test_dbap_width_zero_baseline        — width=0 → single-source (regression)
//   test_dbap_width_pi_half_distributed  — width=π/2 → ≥3 nonzero, energy≈1
//   test_dbap_width_smooth_transition    — increasing width → monotone spread

#include "render/DBAPRenderer.h"
#include "geometry/LayoutLoader.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static spe::geometry::SpeakerLayout make_ring_8ch()
{
    spe::geometry::SpeakerLayout layout;
    constexpr int N = 8;
    constexpr float r = 2.0f;
    for (int i = 0; i < N; ++i) {
        float az = static_cast<float>(i) * (2.0f * 3.14159265f / N);
        spe::geometry::Speaker spk;
        spk.x = r * std::sin(az);
        spk.y = 0.f;
        spk.z = r * std::cos(az);
        layout.speakers.push_back(spk);
    }
    return layout;
}

static std::vector<float> run_one_block(spe::render::DBAPRenderer& renderer,
                                        float az_rad, float el_rad,
                                        float dist_m, float width_rad,
                                        int num_speakers)
{
    constexpr int FRAMES = 64;
    std::vector<float> dry(FRAMES, 0.f);
    dry[0] = 1.f;
    const float* dry_ptr = dry.data();

    std::vector<float> out(static_cast<size_t>(FRAMES * num_speakers), 0.f);

    spe::render::ObjectState obj;
    obj.az_rad    = az_rad;
    obj.el_rad    = el_rad;
    obj.dist_m    = dist_m;
    obj.active    = true;
    obj.width_rad = width_rad;

    renderer.processBlock(
        std::span<const spe::render::ObjectState>(&obj, 1),
        std::span<const float* const>(&dry_ptr, 1),
        out.data(),
        FRAMES);

    // Extract gains at sample 0 (interleaved: frame 0, all speakers)
    std::vector<float> gains(static_cast<size_t>(num_speakers));
    for (int s = 0; s < num_speakers; ++s)
        gains[static_cast<size_t>(s)] = out[static_cast<size_t>(s)];
    return gains;
}

static int count_nonzero(const std::vector<float>& gains, float threshold = 1e-4f)
{
    int cnt = 0;
    for (float g : gains)
        if (std::fabs(g) > threshold) ++cnt;
    return cnt;
}

static float sum_sq(const std::vector<float>& gains)
{
    float s = 0.f;
    for (float g : gains) s += g * g;
    return s;
}

int main()
{
    constexpr float PI      = 3.14159265f;
    constexpr float PI_HALF = PI * 0.5f;

    auto layout = make_ring_8ch();
    const int S = static_cast<int>(layout.speakers.size());

    // --- Test 1: width=0 baseline ---
    {
        spe::render::DBAPRenderer r;
        r.prepareToPlay(layout, 48000.0);

        // Reference: single-source DBAP at az=0
        auto gains_w0 = run_one_block(r, 0.f, 0.f, 1.f, 0.f, S);

        // Dominant speaker should be front (az≈0 → speaker 0)
        int max_idx = static_cast<int>(
            std::max_element(gains_w0.begin(), gains_w0.end()) - gains_w0.begin());

        std::printf("[test_dbap_width_zero_baseline] max speaker idx=%d gain=%.5f\n",
                    max_idx, gains_w0[static_cast<size_t>(max_idx)]);

        // Speaker 0 is at az=0 → should have largest gain
        assert(max_idx == 0 && "width=0 max gain must be at speaker 0 (az=0)");

        // Energy should be normalised (sum g^2 ≈ 1)
        float ss = sum_sq(gains_w0);
        std::printf("  sum_sq = %.6f\n", ss);
        assert(std::fabs(ss - 1.f) < 0.05f && "DBAP energy must be ≈1");

        std::printf("  PASS\n");
    }

    // --- Test 2: width=π/2 distributed ---
    {
        spe::render::DBAPRenderer r;
        r.prepareToPlay(layout, 48000.0);

        auto gains = run_one_block(r, 0.f, 0.f, 1.f, PI_HALF, S);
        int nz = count_nonzero(gains);
        float ss = sum_sq(gains);

        std::printf("[test_dbap_width_pi_half_distributed] nonzero=%d sum_sq=%.6f\n",
                    nz, ss);
        for (int i = 0; i < S; ++i)
            std::printf("  spk[%d] = %.5f\n", i, gains[static_cast<size_t>(i)]);

        assert(nz >= 3 && "width=π/2 must spread to ≥3 speakers");
        assert(std::fabs(ss - 1.f) < 0.05f && "energy must be preserved (sum_sq ≈ 1)");
        std::printf("  PASS (energy error = %.3f%%)\n",
                    std::fabs(ss - 1.f) * 100.f);
    }

    // --- Test 3: smooth/monotone spread ---
    {
        spe::render::DBAPRenderer r;
        r.prepareToPlay(layout, 48000.0);

        // Width sweep: 0, π/8, π/4, π/2
        const float widths[] = { 0.f, PI / 8.f, PI / 4.f, PI_HALF };
        constexpr int NW = 4;

        // Track gain of the speaker *adjacent* to the dominant (spk 1 for az=0)
        float prev_adj = -1.f;
        bool monotone  = true;

        std::printf("[test_dbap_width_smooth_transition]\n");
        for (int w = 0; w < NW; ++w) {
            r.prepareToPlay(layout, 48000.0);  // reset ramps
            auto gains = run_one_block(r, 0.f, 0.f, 1.f, widths[w], S);

            // Adjacent speaker = spk 1 (az = 45°)
            float adj = gains[1];
            std::printf("  width=%.4f adj_gain=%.5f\n", widths[w], adj);

            if (prev_adj >= 0.f && adj < prev_adj - 1e-5f)
                monotone = false;
            prev_adj = adj;
        }

        assert(monotone && "adjacent speaker gain must monotonically increase with width");
        std::printf("  PASS\n");
    }

    std::printf("All test_p_dbap_width tests passed.\n");
    return 0;
}
