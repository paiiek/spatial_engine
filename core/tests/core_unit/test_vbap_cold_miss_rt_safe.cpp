// core/tests/core_unit/test_vbap_cold_miss_rt_safe.cpp
//
// v0.8 audit P1.3 (DSP-3) — RT-sentinel for VBAP cold-cache-miss path.
//
// Pre-P1.3 bug: VBAPRenderer cold-miss called AlgorithmAnalyticReference::
// vbap_gain() which (a) returned std::vector<float> by value and (b)
// internally allocated std::vector temporaries (azs/idx/ux/uy/uz/ang/
// cands) — all inside the audio thread's SPE_RT_NO_ALLOC_SCOPE().
//
// This test (only meaningful under SPATIAL_ENGINE_RT_ASSERTS=ON, i.e.
// build_rton) forces a NEVER-SEEN direction through the renderer's cold-
// miss path and asserts rt_alloc_violations() == 0.
//
// Driven inside an explicit SPE_RT_NO_ALLOC_SCOPE so the per-thread guard
// is engaged for the processBlock() call (mirrors how SpatialEngine::
// audioBlock() does it on the live audio thread).

#include "render/VBAPRenderer.h"
#include "render/RenderingAlgorithm.h"
#include "geometry/SpeakerLayout.h"
#include "util/RtAssertNoAlloc.h"

#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

spe::geometry::SpeakerLayout make_8ch_dome() {
    // 8 speakers: 4 horizontal + 4 upper. 3D so vbap_gain_3d() path runs.
    spe::geometry::SpeakerLayout layout;
    int ch = 0;
    for (int i = 0; i < 4; ++i) {
        const float az = static_cast<float>(i) * 90.f * (kPi / 180.f);
        spe::geometry::Speaker spk;
        spk.channel = ch++;
        spk.x = std::sin(az); spk.y = 0.f; spk.z = std::cos(az);
        layout.speakers.push_back(spk);
    }
    const float el_up = 30.f * kPi / 180.f;
    for (int i = 0; i < 4; ++i) {
        const float az = (static_cast<float>(i) * 90.f + 45.f) * (kPi / 180.f);
        spe::geometry::Speaker spk;
        spk.channel = ch++;
        spk.x = std::cos(el_up) * std::sin(az);
        spk.y = std::sin(el_up);
        spk.z = std::cos(el_up) * std::cos(az);
        layout.speakers.push_back(spk);
    }
    return layout;
}

} // namespace

int main() {
    spe::util::rt_alloc_violations_reset();

    auto layout = make_8ch_dome();
    spe::render::VBAPRenderer renderer;
    renderer.prepareToPlay(layout, 48000.0);

    // ONE active object at a never-seen direction (no warm-up calls).
    constexpr int kBlockSamples = 64;
    std::vector<spe::render::ObjectState> objs(1);
    objs[0].az_rad    = 0.317f;   // not a grid-aligned bin → forces cold miss
    objs[0].el_rad    = 0.143f;
    objs[0].dist_m    = 1.f;
    objs[0].active    = true;
    objs[0].width_rad = 0.f;

    std::vector<float> dry(kBlockSamples, 0.25f);
    const float* dry_ptr = dry.data();

    std::vector<float> out(static_cast<size_t>(8) * kBlockSamples, 0.f);

    // Engage the RT-no-alloc scope on this thread, exactly mirroring how
    // SpatialEngine::audioBlock() does it. Any heap touch inside increments
    // rt_alloc_violations() (and SIGABRTs the test if asserts are wired).
    {
        SPE_RT_NO_ALLOC_SCOPE();
        renderer.processBlock(
            std::span<const spe::render::ObjectState>(objs.data(), 1),
            std::span<const float* const>(&dry_ptr, 1),
            out.data(), kBlockSamples);
    }

    const auto v = spe::util::rt_alloc_violations();
    if (v != 0) {
        std::fprintf(stderr,
            "FAIL test_vbap_cold_miss_rt_safe: rt_alloc_violations=%llu "
            "(expected 0) — VBAP cold-miss path STILL allocates on the "
            "audio thread\n",
            static_cast<unsigned long long>(v));
        return 1;
    }
    std::printf("OK  test_vbap_cold_miss_rt_safe: rt_alloc_violations=0 "
                "after cold-miss VBAP at az=%.3f, el=%.3f\n",
                objs[0].az_rad, objs[0].el_rad);
    return 0;
}
