// core/tests/core_unit/test_ambi_decoder_type_runtime_apply.cpp
//
// v0.8 audit P1.1 — FUNCTIONAL test: runtime /sys/ambi_decoder_type switch
// actually changes the live decode matrix.
//
// Pre-P1.1 bug: SpatialEngine::applyPendingDecoderTypeChange() ran ONLY
// in prepareToPlay(); any OSC-driven decoder-type change after start was a
// silent no-op until device restart. M2HOA-Q14.
//
// This test:
//   1. Builds an AmbisonicRenderer at PINV (default).
//   2. Captures the decode output for a known SH input → reference1.
//   3. Calls setDecoderType(MAX_RE) followed by applyPendingDecoderTypeChange()
//      (mirroring the new ~1 Hz control tick).
//   4. Re-decodes the same SH input → reference2.
//   5. Asserts reference1 ≠ reference2 (i.e. the matrix actually changed),
//      AND no NaNs/garbage appeared.
//
// Combined with the concurrency stress test, this proves both that the
// apply path is reachable from the control thread AND that the swap
// is observed by the audio path.

#include "render/AmbisonicRenderer.h"
#include "render/RenderingAlgorithm.h"
#include "ambi/AmbisonicEncoder.h"
#include "geometry/SpeakerLayout.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int   kBlockSamples = 64;

spe::geometry::SpeakerLayout make_circular_8ch() {
    spe::geometry::SpeakerLayout layout;
    for (int i = 0; i < 8; ++i) {
        const float az = static_cast<float>(i) * 45.f * (kPi / 180.f);
        spe::geometry::Speaker spk;
        spk.channel = i;
        spk.x = std::sin(az);
        spk.y = 0.f;
        spk.z = std::cos(az);
        layout.speakers.push_back(spk);
    }
    return layout;
}

// Render one block at the given decoder type and capture the output.
std::vector<float> render_one_block(spe::render::AmbisonicRenderer& renderer,
                                    int N_spk) {
    // One active object at az=π/4, el=0, distance 1 m.
    std::vector<spe::render::ObjectState> objs(1);
    objs[0].az_rad = kPi / 4.f;
    objs[0].el_rad = 0.f;
    objs[0].dist_m = 1.f;
    objs[0].active = true;
    objs[0].width_rad = 0.f;

    // Dry mono input — constant 1.0 for deterministic output.
    std::vector<float> dry(kBlockSamples, 1.f);
    const float* dry_ptr = dry.data();

    std::vector<float> out(static_cast<size_t>(N_spk) * kBlockSamples, 0.f);
    renderer.processBlock(
        std::span<const spe::render::ObjectState>(objs.data(), 1),
        std::span<const float* const>(&dry_ptr, 1),
        out.data(), kBlockSamples);
    return out;
}

float l2_distance(const std::vector<float>& a, const std::vector<float>& b) {
    float s = 0.f;
    for (size_t i = 0; i < a.size(); ++i) {
        const float d = a[i] - b[i];
        s += d * d;
    }
    return std::sqrt(s);
}

bool has_garbage(const std::vector<float>& v) {
    for (float x : v) {
        if (std::isnan(x) || std::isinf(x) || std::fabs(x) > 1e6f) return true;
    }
    return false;
}

} // namespace

int main() {
    auto layout = make_circular_8ch();
    spe::render::AmbisonicRenderer renderer;
    renderer.prepareToPlay(layout, 48000.0);
    renderer.setOrder(2);

    // ── (1) Decode with the default (PINV) type.
    auto out_pinv = render_one_block(renderer, 8);
    assert(!has_garbage(out_pinv) && "PINV output contains NaN/inf");

    // ── (2) Switch to MAX_RE via the new control-tick forwarder path.
    //       setDecoderType is the OSC-FIFO-drain effect; applyPending… is
    //       the ~1 Hz control tick.
    renderer.setDecoderType(1);  // 1 = MAX_RE
    renderer.applyPendingDecoderTypeChange();

    auto out_max_re = render_one_block(renderer, 8);
    assert(!has_garbage(out_max_re) && "MAX_RE output contains NaN/inf");

    // ── (3) Assert the matrices differ. MAX_RE pre-multiplies SH-side
    //       weights, so the output MUST be numerically distinct from PINV
    //       for the same SH input. L2 distance > 1e-3 (well above float
    //       noise) is the lock-in.
    const float diff = l2_distance(out_pinv, out_max_re);
    if (!(diff > 1e-3f)) {
        std::fprintf(stderr,
            "FAIL test_ambi_decoder_type_runtime_apply: "
            "PINV→MAX_RE diff=%.7g (expected >1e-3) — the runtime apply "
            "did NOT take effect.\n", diff);
        return 1;
    }

    // ── (4) And: re-applying with no pending change is a no-op.
    auto out_again = render_one_block(renderer, 8);
    const float diff2 = l2_distance(out_max_re, out_again);
    if (!(diff2 < 1e-6f)) {
        std::fprintf(stderr,
            "FAIL test_ambi_decoder_type_runtime_apply: "
            "subsequent block diff=%.7g (expected steady-state)\n", diff2);
        return 1;
    }

    // ── (5) Switch to a SECOND type (ALLRAD) and assert it ALSO swaps.
    renderer.setDecoderType(2);  // 2 = ALLRAD
    renderer.applyPendingDecoderTypeChange();
    auto out_allrad = render_one_block(renderer, 8);
    const float diff3 = l2_distance(out_max_re, out_allrad);
    if (!(diff3 > 1e-3f)) {
        std::fprintf(stderr,
            "FAIL test_ambi_decoder_type_runtime_apply: "
            "MAX_RE→ALLRAD diff=%.7g (expected >1e-3)\n", diff3);
        return 1;
    }

    std::printf(
        "OK  test_ambi_decoder_type_runtime_apply: "
        "PINV→MAX_RE diff=%.4g, MAX_RE→ALLRAD diff=%.4g\n",
        diff, diff3);
    return 0;
}
