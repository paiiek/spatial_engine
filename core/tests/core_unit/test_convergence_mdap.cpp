// test_convergence_mdap.cpp
// Dreamscape convergence — MDAP (Multiple-Direction Amplitude Panning) source spread.
//
// Verifies AlgorithmAnalyticReference::vbap_mdap_gain_into:
//   1. spread ≈ 0 is bit-identical to the point-source vbap_gain_into.
//   2. a real spread widens the gain distribution (more speakers active).
//   3. Σg² == 1 (energy preserved) at every spread.
//   4. spread is clamped to 40° (kMdapSpreadMaxDegrees).
//   5. L/R invariant survives the spread (right object -> right speakers).
//
// Rig: 8ch dome (lower ring el=0 + upper ring el=45 at az {-135,-45,45,135}).

#include "render/AlgorithmAnalyticReference.h"
#include "geometry/SpeakerLayout.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while (0)

#define CHECK_NEAR(a, b, tol) \
    do { float _a = (a), _b = (b); \
         if (std::abs(_a - _b) > (tol)) { \
             std::printf("FAIL %s:%d  |%.6f - %.6f| > %.2e\n", \
                 __FILE__, __LINE__, (double) _a, (double) _b, (double) (tol)); \
             ++failures; \
         } } while (0)

using namespace spe::render;
using namespace spe::geometry;

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kAzs[4] = { -135.f, -45.f, 45.f, 135.f };

static SpeakerLayout make_8ch_dome() {
    SpeakerLayout l;
    l.name = "test_8ch_dome";
    l.regularity = Regularity::IRREGULAR;
    auto add = [&](int ch, float az_deg, float el_deg) {
        const float az = az_deg * kPi / 180.f, el = el_deg * kPi / 180.f;
        Speaker s;
        s.channel = ch;
        s.x = std::cos(el) * std::sin(az);
        s.y = std::sin(el);
        s.z = std::cos(el) * std::cos(az);
        l.speakers.push_back(s);
    };
    for (int i = 0; i < 4; ++i) add(i + 1, kAzs[i], 0.f);
    for (int i = 0; i < 4; ++i) add(i + 5, kAzs[i], 45.f);
    return l;
}

static std::vector<float> mdap(const SpeakerLayout& l, float az_deg, float el_deg, float spread_deg) {
    std::vector<float> g(l.speakers.size(), 0.f);
    AlgorithmAnalyticReference::vbap_mdap_gain_into(
        l, az_deg * kPi / 180.f, el_deg * kPi / 180.f, spread_deg,
        g.data(), static_cast<int>(g.size()));
    return g;
}

static float sum_sq(const std::vector<float>& g) {
    float ss = 0.f; for (float v : g) ss += v * v; return ss;
}
static int nonzero(const std::vector<float>& g, float thr = 1e-4f) {
    int n = 0; for (float v : g) if (v > thr) ++n; return n;
}

// ---- 1: spread≈0 == point source ----
static void test_zero_spread_equals_point() {
    auto l = make_8ch_dome();
    const float az = 20.f * kPi / 180.f, el = 20.f * kPi / 180.f;
    auto point = AlgorithmAnalyticReference::vbap_gain(l, az, el);
    auto m0 = mdap(l, 20.f, 20.f, 0.0f);
    for (size_t i = 0; i < point.size(); ++i)
        CHECK_NEAR(point[i], m0[i], 1e-6f);
    std::printf("[PASS] spread=0 == point-source VBAP\n");
}

// ---- 2 + 3: spread widens distribution, Σg²=1 ----
static void test_spread_widens_and_energy() {
    auto l = make_8ch_dome();
    // Off-grid direction so the point source already spans a triplet.
    auto point = mdap(l, 20.f, 20.f, 0.0f);
    auto wide  = mdap(l, 20.f, 20.f, 30.0f);
    CHECK_NEAR(sum_sq(point), 1.0f, 1e-4f);
    CHECK_NEAR(sum_sq(wide),  1.0f, 1e-4f);
    CHECK(nonzero(wide) >= nonzero(point));
    CHECK(nonzero(wide) > nonzero(point) || nonzero(point) >= 4); // genuinely wider unless already broad
    std::printf("[PASS] spread widens distribution (point n=%d -> wide n=%d), Σg²=1\n",
                nonzero(point), nonzero(wide));
}

// ---- 4: 40° clamp ----
static void test_spread_clamp_40deg() {
    auto l = make_8ch_dome();
    auto at40 = mdap(l, 20.f, 20.f, 40.0f);
    auto at60 = mdap(l, 20.f, 20.f, 60.0f);   // clamped to 40
    for (size_t i = 0; i < at40.size(); ++i)
        CHECK_NEAR(at40[i], at60[i], 1e-6f);
    std::printf("[PASS] spread clamped to 40° (60° == 40°)\n");
}

// ---- 5: L/R invariant under spread ----
static void test_lr_invariant_under_spread() {
    auto l = make_8ch_dome();
    auto g = mdap(l, 45.f, 30.f, 30.0f);   // right-ish, elevated, spread
    float right = 0.f, left = 0.f;
    for (int i = 0; i < 8; ++i) {
        const float e = g[(size_t) i] * g[(size_t) i];
        if (kAzs[i % 4] > 0.f) right += e; else left += e;
    }
    CHECK(right > left);
    CHECK_NEAR(sum_sq(g), 1.0f, 1e-4f);
    std::printf("[PASS] L/R invariant under spread (right=%.3f > left=%.3f)\n",
                (double) right, (double) left);
}

// ---- 6: 2D layout azimuth-arc spread genuinely widens (dense ring) ----
static void test_2d_layout_arc_spread() {
    // 24-speaker horizontal ring (15° spacing) — finer than the 40°-clamped
    // spread's ±20° arc, so the spread must recruit speakers beyond the point
    // pair. (A sparse ring with >40° spacing cannot widen — see smoke_mdap.py.)
    SpeakerLayout l;
    l.name = "test_ring24";
    l.regularity = Regularity::CIRCULAR;
    for (int i = 0; i < 24; ++i) {
        const float az = (-180.f + 360.f * static_cast<float>(i) / 24.f) * kPi / 180.f;
        Speaker s; s.channel = i + 1;
        s.x = std::sin(az); s.y = 0.f; s.z = std::cos(az);
        l.speakers.push_back(s);
    }
    auto point = mdap(l, 22.f, 0.f, 0.0f);    // between az=15 and az=30 -> pair
    auto wide  = mdap(l, 22.f, 0.f, 40.0f);
    CHECK_NEAR(sum_sq(point), 1.0f, 1e-4f);
    CHECK_NEAR(sum_sq(wide),  1.0f, 1e-4f);
    CHECK(nonzero(point) == 2);
    CHECK(nonzero(wide) > nonzero(point));    // strict widening via the arc
    std::printf("[PASS] 2D arc spread widens (point n=%d -> wide n=%d, Σg²=1)\n",
                nonzero(point), nonzero(wide));
}

int main() {
    test_zero_spread_equals_point();
    test_spread_widens_and_energy();
    test_spread_clamp_40deg();
    test_lr_invariant_under_spread();
    test_2d_layout_arc_spread();

    if (failures == 0) { std::printf("[RESULT] PASS\n"); return 0; }
    std::printf("[RESULT] FAIL (%d failures)\n", failures);
    return 1;
}
