// test_p_vbap3d.cpp
// VBAP 3D elevation numerical tests
//
// Test 1: el=0 (horizontal) gain pattern is valid (sum-of-squares == 1)
// Test 2: el extreme values (-PI/2, +PI/2) — no crash, gain sum-of-squares valid
// Test 3: el=0 vs el=30deg — either gains differ (3D layout) or "no crash" confirmed

#include "render/AlgorithmAnalyticReference.h"
#include "render/VBAPRenderer.h"
#include "geometry/SpeakerLayout.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <array>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

#define CHECK_NEAR(a, b, tol) \
    do { float _a = (a), _b = (b); \
         if (std::abs(_a - _b) > (tol)) { \
             std::printf("FAIL %s:%d  |%.8f - %.8f| = %.2e > %.2e\n", \
                 __FILE__, __LINE__, (double)_a, (double)_b, \
                 (double)std::abs(_a - _b), (double)(tol)); \
             ++failures; \
         } } while(0)

using namespace spe::render;
using namespace spe::geometry;

static constexpr float kPi = 3.14159265358979323846f;

// 4-speaker horizontal layout (identical to test_p3_vbap)
static SpeakerLayout make_4ch_horizontal() {
    SpeakerLayout l;
    l.name = "test_4ch_horiz";
    l.regularity = Regularity::CIRCULAR;
    float azs[] = {0.f, 90.f, 180.f, 270.f};
    for (int i = 0; i < 4; ++i) {
        float az = azs[i] * kPi / 180.f;
        Speaker s;
        s.channel = i + 1;
        s.x = std::sin(az);
        s.y = 0.f;
        s.z = std::cos(az);
        l.speakers.push_back(s);
    }
    return l;
}

// 8-speaker layout: 4 horizontal + 4 elevated (height layer at +45 deg elevation)
static SpeakerLayout make_8ch_3d() {
    SpeakerLayout l;
    l.name = "test_8ch_3d";
    l.regularity = Regularity::IRREGULAR;
    float azs[] = {0.f, 90.f, 180.f, 270.f};
    // Lower ring at el=0
    for (int i = 0; i < 4; ++i) {
        float az = azs[i] * kPi / 180.f;
        Speaker s;
        s.channel = i + 1;
        s.x = std::sin(az);
        s.y = 0.f;
        s.z = std::cos(az);
        l.speakers.push_back(s);
    }
    // Upper ring at el=45 deg
    float el = 45.f * kPi / 180.f;
    for (int i = 0; i < 4; ++i) {
        float az = azs[i] * kPi / 180.f;
        Speaker s;
        s.channel = i + 5;
        s.x = std::cos(el) * std::sin(az);
        s.y = std::sin(el);
        s.z = std::cos(el) * std::cos(az);
        l.speakers.push_back(s);
    }
    return l;
}

// Helper: sum of squares of a gain vector
static float sum_sq(const std::vector<float>& g) {
    float ss = 0.f;
    for (float v : g) ss += v * v;
    return ss;
}

// Helper: check all gains are finite and non-negative
static bool all_valid(const std::vector<float>& g) {
    for (float v : g) {
        if (!std::isfinite(v)) return false;
        if (v < -1e-6f)        return false;
    }
    return true;
}

// --- Test 1: el=0 horizontal layout — gain sum-of-squares == 1 ---
static void test1_horizontal_el0() {
    auto layout = make_4ch_horizontal();
    float az = 45.f * kPi / 180.f; // between two speakers
    auto gains = AlgorithmAnalyticReference::vbap_gain(layout, az, 0.f);

    CHECK(all_valid(gains));
    CHECK_NEAR(sum_sq(gains), 1.0f, 1e-5f);
    std::printf("[PASS] test1: el=0 horizontal gain sum-of-squares == 1\n");
}

// --- Test 2: extreme elevation values — no crash, gains valid ---
static void test2_extreme_elevation() {
    auto layout = make_4ch_horizontal();
    float az = 0.f;

    // el = -PI/2 (straight down)
    {
        auto gains = AlgorithmAnalyticReference::vbap_gain(layout, az, -kPi / 2.f);
        CHECK(all_valid(gains));
        // gain sum may be 0 if no speaker subtends this direction — just must not crash
        std::printf("[PASS] test2a: el=-PI/2 no crash, gains finite/non-negative\n");
    }

    // el = +PI/2 (straight up)
    {
        auto gains = AlgorithmAnalyticReference::vbap_gain(layout, az, +kPi / 2.f);
        CHECK(all_valid(gains));
        std::printf("[PASS] test2b: el=+PI/2 no crash, gains finite/non-negative\n");
    }

    // Verify via VBAPRenderer::processBlock as well (no crash path)
    {
        VBAPRenderer renderer;
        renderer.prepareToPlay(layout, 48000.0);

        constexpr int BLOCK = 32;
        constexpr int SPKS  = 4;
        static float silence[BLOCK] = {};
        static float out[BLOCK * SPKS] = {};

        ObjectState obj;
        obj.az_rad = 0.f;
        obj.el_rad = +kPi / 2.f;
        obj.dist_m = 1.f;
        obj.active = true;

        const float* dry[1] = { silence };
        std::array<ObjectState, 1> objs = { obj };

        renderer.processBlock(
            std::span<const ObjectState>(objs.data(), objs.size()),
            std::span<const float* const>(dry, 1),
            out, BLOCK);

        // Output must be all finite
        bool ok = true;
        for (int i = 0; i < BLOCK * SPKS; ++i)
            if (!std::isfinite(out[i])) { ok = false; break; }
        CHECK(ok);
        std::printf("[PASS] test2c: processBlock el=+PI/2 output is finite\n");
    }
}

// --- Test 3: el=0 vs el=30deg gain distribution ---
static void test3_elevation_effect() {
    // On a 3D layout, raising the elevation should change at least one speaker gain
    auto layout = make_8ch_3d();
    float az = 0.f;

    auto gains_el0  = AlgorithmAnalyticReference::vbap_gain(layout, az, 0.f);
    auto gains_el30 = AlgorithmAnalyticReference::vbap_gain(layout, az, 30.f * kPi / 180.f);

    CHECK(all_valid(gains_el0));
    CHECK(all_valid(gains_el30));

    bool any_diff = false;
    for (size_t i = 0; i < gains_el0.size(); ++i) {
        if (std::abs(gains_el0[i] - gains_el30[i]) > 1e-4f) {
            any_diff = true;
            break;
        }
    }

    CHECK(any_diff);
    std::printf("[PASS] test3: el=30 changes gain distribution vs el=0\n");
    // Gains must remain valid regardless of el_rad
    CHECK(all_valid(gains_el0));
    CHECK(all_valid(gains_el30));
}

// --- Test 4: dimensionality boundary (max|y| around 1e-3 threshold) ---
static void test4_dimensionality_boundary() {
    // (a) Sub-threshold (max|y| = 5e-4) → 2D path: gains identical for el=0 and el=30
    {
        SpeakerLayout l;
        l.name = "test_4ch_subthresh";
        l.regularity = Regularity::CIRCULAR;
        float azs[] = {0.f, 90.f, 180.f, 270.f};
        for (int i = 0; i < 4; ++i) {
            float az = azs[i] * kPi / 180.f;
            Speaker s;
            s.channel = i + 1;
            s.x = std::sin(az);
            s.y = 5e-4f * (i % 2 == 0 ? 1.f : -1.f); // |y| < 1e-3
            s.z = std::cos(az);
            l.speakers.push_back(s);
        }
        float az = 0.f;
        auto g0  = AlgorithmAnalyticReference::vbap_gain(l, az, 0.f);
        auto g30 = AlgorithmAnalyticReference::vbap_gain(l, az, 30.f * kPi / 180.f);

        CHECK(all_valid(g0));
        CHECK(all_valid(g30));

        // 2D path ignores el_rad → gains must match exactly
        bool same = true;
        for (size_t i = 0; i < g0.size(); ++i)
            if (std::abs(g0[i] - g30[i]) > 1e-5f) { same = false; break; }
        CHECK(same);
        std::printf("[PASS] test4a: max|y|=5e-4 routes to 2D path (gains el=0 == el=30)\n");
    }

    // (b) Above threshold (max|y| = 2e-3) → 3D path: gains differ
    {
        SpeakerLayout l;
        l.name = "test_8ch_3d_thresh";
        l.regularity = Regularity::IRREGULAR;
        float azs[] = {0.f, 90.f, 180.f, 270.f};
        for (int i = 0; i < 4; ++i) {
            float az = azs[i] * kPi / 180.f;
            Speaker s;
            s.channel = i + 1;
            s.x = std::sin(az);
            s.y = 0.f;
            s.z = std::cos(az);
            l.speakers.push_back(s);
        }
        // Upper layer with small (but >1e-3) elevation: y=2e-3
        for (int i = 0; i < 4; ++i) {
            float az = azs[i] * kPi / 180.f;
            Speaker s;
            s.channel = i + 5;
            s.x = std::sin(az);
            s.y = 2e-3f;
            s.z = std::cos(az);
            l.speakers.push_back(s);
        }
        float az = 0.f;
        auto g0  = AlgorithmAnalyticReference::vbap_gain(l, az, 0.f);
        auto g30 = AlgorithmAnalyticReference::vbap_gain(l, az, 30.f * kPi / 180.f);

        CHECK(all_valid(g0));
        CHECK(all_valid(g30));

        bool any_diff = false;
        for (size_t i = 0; i < g0.size(); ++i)
            if (std::abs(g0[i] - g30[i]) > 1e-4f) { any_diff = true; break; }
        CHECK(any_diff);
        std::printf("[PASS] test4b: max|y|=2e-3 routes to 3D path (gains differ for el=0 vs el=30)\n");
    }
}

static void test5_fallback_gain_pattern() {
    // Layout: 4ch horizontal + speaker[0].y=2e-3 → routes to 3D path (max|y|>1e-3).
    // The perturbed triplet has det≈2e-3 (non-zero), but el=+60° has sy≈0.866
    // while all speakers have y≤2e-3. Cramer's rule gives at least one negative
    // gain for EVERY triplet → non-negativity gate at AAR.cpp:193 rejects all
    // triplets → fallback path at AAR.cpp:236 fires.
    auto layout = make_4ch_horizontal();
    layout.speakers[0].y = 2e-3f; // routes to 3D path; still above-hull for el=60°
    const float az = 0.f;
    const float el = 60.f * kPi / 180.f;
    auto gains = AlgorithmAnalyticReference::vbap_gain(layout, az, el);

    CHECK(all_valid(gains));                    // finite + non-negative
    CHECK_NEAR(sum_sq(gains), 1.0f, 1e-5f);    // energy normalised
    int nonzero = 0;
    for (float v : gains) if (v > 1e-6f) ++nonzero;
    CHECK(nonzero >= 2 && nonzero <= 3); // ≥2 speakers: distinguishes nearest-3 from
                                          // the last-ditch single-speaker branch (AAR.cpp:290)
    std::printf("[PASS] test5: fallback gain pattern valid (>=2 nonzero, sum_sq=1)\n");
}

// v0.8 audit P2.2 (DSP-5) — degenerate-triplet fallback Σg²≈1 guard.
// The outside-hull fallback (AlgorithmAnalyticReference.cpp:259+) picks the
// 3 nearest speakers by angular distance, then attempts Cramer's rule. If
// the chosen 3 are degenerate (det(L) ≈ 0, e.g. nearly coplanar through the
// origin), it falls into the inverse-angular-distance branch at
// AAR.cpp:299-304, then clamps non-negative and energy-normalises at
// AAR.cpp:311-320. This guard test pins Σg² ≈ 1 (±1e-5) so a future
// regression that drops the energy normalisation fails loudly.
//
// Trigger recipe (verified by inspection of the dispatch + fallback paths):
//   - 4-speaker layout with all speakers in the y = 2e-3 plane (above the
//     2D/3D threshold of 1e-3, so routes to 3D path).
//   - Source at el = +60° → sy = sin(60°) ≈ 0.866; no triplet in-hull
//     (all speaker unit vectors have y ≈ 2e-3 ≪ 0.866), so every Cramer
//     attempt yields at least one negative gain → the non-negativity gate
//     at AAR.cpp:216 rejects ALL triplets → fallback fires.
//   - The 3 nearest speakers are all in the (nearly horizontal) y=2e-3
//     plane, so the resulting 3×3 column matrix has det ≈ 2e-3 × (cross-
//     product of two nearly-horizontal vectors) ≈ 0 → degenerate sub-
//     branch at AAR.cpp:299 fires.
static void test6_degenerate_triplet_fallback_energy_guard() {
    // 4 speakers, all in the y=2e-3 plane (≥1e-3 → 3D dispatch path).
    SpeakerLayout l;
    l.name = "test_4ch_coplanar";
    l.regularity = Regularity::IRREGULAR;
    const float y_off = 2e-3f;
    const float azs[] = {0.f, 90.f, 180.f, 270.f};
    for (int i = 0; i < 4; ++i) {
        float az = azs[i] * kPi / 180.f;
        Speaker s;
        s.channel = i + 1;
        s.x = std::sin(az);
        s.y = y_off;        // all speakers share the same y → near-coplanar
        s.z = std::cos(az);
        l.speakers.push_back(s);
    }

    const float az = 30.f * kPi / 180.f;
    const float el = 60.f * kPi / 180.f; // well above the y=2e-3 plane → no in-hull triplet
    auto gains = AlgorithmAnalyticReference::vbap_gain(l, az, el);

    CHECK(all_valid(gains));
    // Pin Σg² ≈ 1 within 1e-5 — this is the load-bearing assertion.
    // If a future change removes the normalisation at AAR.cpp:311-320 the
    // raw inverse-angular weights would give Σg² ≫ 1 (or ≪ 1) and this
    // would fail loudly.
    CHECK_NEAR(sum_sq(gains), 1.0f, 1e-5f);

    // Sanity: at least one gain must be non-trivial (not the all-zero
    // last-ditch branch at AAR.cpp:313-316).
    int nonzero = 0;
    for (float v : gains) if (v > 1e-6f) ++nonzero;
    CHECK(nonzero >= 1);
    std::printf("[PASS] test6: degenerate-triplet fallback Σg²≈1 guard (nonzero=%d, sum_sq=%.6f)\n",
                nonzero, static_cast<double>(sum_sq(gains)));
}

int main() {
    test1_horizontal_el0();
    test2_extreme_elevation();
    test3_elevation_effect();
    test4_dimensionality_boundary();
    test5_fallback_gain_pattern();
    test6_degenerate_triplet_fallback_energy_guard();

    if (failures == 0) {
        std::printf("[RESULT] PASS\n");
        return 0;
    }
    std::printf("[RESULT] FAIL (%d failures)\n", failures);
    return 1;
}
