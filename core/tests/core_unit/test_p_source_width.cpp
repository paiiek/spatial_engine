// test_p_source_width.cpp
// M1 acceptance tests: per-object Source WIDTH (0..π rad) fan-out.
//
// Tests:
//   test_width_zero_baseline  — width=0 → VBAP point source (≤2 nonzero gains)
//   test_width_90_distributed — width=π/2 (90°) → ≥3 nonzero gains (spread)

#include "render/VBAPRenderer.h"
#include "geometry/LayoutLoader.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static spe::geometry::SpeakerLayout make_lab_8ch() {
    // 8-speaker circular layout in the horizontal plane (every 45°).
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

static std::vector<float> run_one_block(spe::render::VBAPRenderer& renderer,
                                        float az_rad, float el_rad,
                                        float width_rad,
                                        int num_speakers)
{
    constexpr int FRAMES = 64;

    // Mono impulse at sample 0
    std::vector<float> dry(FRAMES, 0.f);
    dry[0] = 1.f;
    const float* dry_ptr = dry.data();

    std::vector<float> out(static_cast<size_t>(FRAMES * num_speakers), 0.f);

    spe::render::ObjectState obj;
    obj.az_rad    = az_rad;
    obj.el_rad    = el_rad;
    obj.dist_m    = 1.f;
    obj.active    = true;
    obj.width_rad = width_rad;

    renderer.processBlock(
        std::span<const spe::render::ObjectState>(&obj, 1),
        std::span<const float* const>(&dry_ptr, 1),
        out.data(),
        FRAMES);

    // Extract gains at sample 0 (first frame, interleaved)
    std::vector<float> gains(static_cast<size_t>(num_speakers));
    for (int s = 0; s < num_speakers; ++s)
        gains[static_cast<size_t>(s)] = out[static_cast<size_t>(s)];  // frame 0

    return gains;
}

static int count_nonzero(const std::vector<float>& gains, float threshold = 1e-4f) {
    int cnt = 0;
    for (float g : gains)
        if (std::fabs(g) > threshold) ++cnt;
    return cnt;
}

int main() {
    auto layout = make_lab_8ch();
    const int S = static_cast<int>(layout.speakers.size());

    // --- Test 1: width=0 baseline ---
    {
        spe::render::VBAPRenderer r;
        r.prepareToPlay(layout, 48000.0);

        // Front (az=0): should hit speaker 0 or pair 0,7
        auto gains = run_one_block(r, 0.f, 0.f, 0.f, S);
        int nz = count_nonzero(gains);

        std::printf("[test_width_zero_baseline] nonzero gains = %d\n", nz);
        assert(nz <= 2 && "width=0 point source must activate ≤2 speakers");
        std::printf("  PASS\n");
    }

    // --- Test 2: width=π/2 (90°) distributed ---
    {
        spe::render::VBAPRenderer r;
        r.prepareToPlay(layout, 48000.0);

        constexpr float PI_OVER_2 = 3.14159265f / 2.f;
        auto gains = run_one_block(r, 0.f, 0.f, PI_OVER_2, S);
        int nz = count_nonzero(gains);

        std::printf("[test_width_90_distributed] nonzero gains = %d (need ≥3)\n", nz);
        for (int i = 0; i < S; ++i)
            std::printf("  spk[%d] = %.5f\n", i, gains[static_cast<size_t>(i)]);
        assert(nz >= 3 && "width=π/2 must spread to ≥3 speakers");
        std::printf("  PASS\n");
    }

    std::printf("All test_p_source_width tests passed.\n");
    return 0;
}
