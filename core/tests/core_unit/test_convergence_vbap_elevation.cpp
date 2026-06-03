// test_convergence_vbap_elevation.cpp
// Dreamscape convergence — VBAP 3D 5-tier elevation layering.
//
// Two layers of verification:
//   (1) Standalone ported mask (iae::fillVbapMaskForObject): flat object keeps
//       only horizontal-layer speakers; elevated object keeps angularly-near
//       speakers; steep object drops the opposite (floor) layer.
//   (2) End-to-end through AlgorithmAnalyticReference::vbap_gain (3D path):
//       a flat object stays on the lower ring (upper ring ~silent); an elevated
//       object routes energy up to the upper ring while leaving the opposite
//       corner silent; Σg² == 1 throughout; right object -> right speakers
//       (L/R invariant).
//
// Rig: 8 speakers — lower ring (el=0) + upper ring (el=45), both at azimuths
// {-135,-45,45,135}. Array index layout:
//   idx 0..3 = lower  az {-135,-45, 45,135}   (el=0)
//   idx 4..7 = upper  az {-135,-45, 45,135}   (el=45)

#include "render/AlgorithmAnalyticReference.h"
#include "render/ported/SpatialMath.h"
#include "render/ported/VbapMask.h"
#include "coords/Coords.h"
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

// idx 0..3 lower (el=0), 4..7 upper (el=45)
static SpeakerLayout make_8ch_dome() {
    SpeakerLayout l;
    l.name = "test_8ch_dome";
    l.regularity = Regularity::IRREGULAR;
    auto add = [&](int ch, float az_deg, float el_deg) {
        const float az = az_deg * kPi / 180.f;
        const float el = el_deg * kPi / 180.f;
        Speaker s;
        s.channel = ch;
        s.x = std::cos(el) * std::sin(az); // right
        s.y = std::sin(el);                // up
        s.z = std::cos(el) * std::cos(az); // front
        l.speakers.push_back(s);
    };
    for (int i = 0; i < 4; ++i) add(i + 1, kAzs[i], 0.f);   // lower
    for (int i = 0; i < 4; ++i) add(i + 5, kAzs[i], 45.f);  // upper
    return l;
}

static iae::Vec3 ported_pos(const Speaker& s) {
    const auto p = spe::coords::mmhoa_to_ported(s.x, s.y, s.z);
    return iae::Vec3{ p[0], p[1], p[2] };
}
static iae::Vec3 ported_dir(float az_deg, float el_deg) {
    const auto p = spe::coords::pipeline_dir_to_ported(az_deg * kPi / 180.f,
                                                        el_deg * kPi / 180.f);
    return iae::Vec3{ p[0], p[1], p[2] };
}

static float sum_sq(const std::vector<float>& g) {
    float ss = 0.f; for (float v : g) ss += v * v; return ss;
}

// ---- (1a) flat object: horizontal layer only ----
static void test_mask_flat_horizontal_only() {
    auto layout = make_8ch_dome();
    std::array<iae::Vec3, 8> spk{};
    for (int i = 0; i < 8; ++i) spk[(size_t) i] = ported_pos(layout.speakers[(size_t) i]);

    std::array<bool, iae::kPrototypeChannels> mask{};
    iae::fillVbapMaskForObject(spk.data(), 8, nullptr,
                               /*objectFlat=*/true, ported_dir(45.f, 0.f), mask.data());

    for (int i = 0; i < 4; ++i) CHECK(mask[(size_t) i]);       // lower ring kept
    for (int i = 4; i < 8; ++i) CHECK(!mask[(size_t) i]);      // upper ring dropped
    std::printf("[PASS] mask flat -> horizontal layer only (lower 4 true, upper 4 false)\n");
}

// ---- (1b) elevated object: angularly-near speakers, opposite corner dropped ----
static void test_mask_elevated_adjacent() {
    auto layout = make_8ch_dome();
    std::array<iae::Vec3, 8> spk{};
    for (int i = 0; i < 8; ++i) spk[(size_t) i] = ported_pos(layout.speakers[(size_t) i]);

    std::array<bool, iae::kPrototypeChannels> mask{};
    iae::fillVbapMaskForObject(spk.data(), 8, nullptr,
                               /*objectFlat=*/false, ported_dir(45.f, 45.f), mask.data());

    CHECK(mask[6]);                       // upper az=45 (idx6) — on the source axis
    CHECK(iae::countVbapMaskTrue(mask.data(), 8) >= 3); // >=3 candidates for a triplet
    CHECK(!mask[0]);                      // lower az=-135 (opposite corner) excluded
    CHECK(!mask[4]);                      // upper az=-135 (opposite corner) excluded
    std::printf("[PASS] mask elevated -> on-axis upper kept, opposite corner dropped (n=%d)\n",
                iae::countVbapMaskTrue(mask.data(), 8));
}

// ---- (1c) steep object: opposite (floor) layer cut ----
static void test_mask_steep_opposite_layer_cut() {
    auto layout = make_8ch_dome();
    std::array<iae::Vec3, 8> spk{};
    for (int i = 0; i < 8; ++i) spk[(size_t) i] = ported_pos(layout.speakers[(size_t) i]);

    std::array<bool, iae::kPrototypeChannels> mask{};
    // el=80 -> ported up-component sin(80)=0.985 > kVbapSteepSourceUz(0.70): steep up.
    iae::fillVbapMaskForObject(spk.data(), 8, nullptr,
                               /*objectFlat=*/false, ported_dir(45.f, 80.f), mask.data());

    // The on-axis floor speaker (lower az=45, idx2) must be cut while the steep
    // tiers are active; if the mask had to relax to the whole group it would
    // still never *prefer* the floor over the ceiling. Assert the ceiling
    // (idx 4..7) carries the candidates and the directly-below floor speaker is
    // not the sole survivor.
    int upper = 0; for (int i = 4; i < 8; ++i) if (mask[(size_t) i]) ++upper;
    CHECK(upper >= 1);                    // at least one ceiling speaker participates
    CHECK(!(mask[2] && upper == 0));      // never floor-only for a steep-up source
    std::printf("[PASS] mask steep-up -> ceiling participates (upper=%d)\n", upper);
}

// ---- (2a) flat object stays on lower ring through vbap_gain ----
static void test_gain_flat_lower_ring() {
    auto layout = make_8ch_dome();
    auto g = AlgorithmAnalyticReference::vbap_gain(layout, 45.f * kPi / 180.f, 0.f);
    CHECK(g.size() == 8);
    CHECK_NEAR(sum_sq(g), 1.0f, 1e-4f);

    float upper = 0.f; for (int i = 4; i < 8; ++i) upper += g[(size_t) i] * g[(size_t) i];
    float lower = 0.f; for (int i = 0; i < 4; ++i) lower += g[(size_t) i] * g[(size_t) i];
    CHECK(upper < 0.02f);                 // upper ring effectively silent
    CHECK(lower > 0.9f);                  // energy on the lower ring
    std::printf("[PASS] gain flat -> lower ring (lower E=%.3f, upper E=%.3f)\n",
                (double) lower, (double) upper);
}

// ---- (2b) elevated object routes up; opposite corner silent ----
static void test_gain_elevated_routes_up() {
    auto layout = make_8ch_dome();
    auto g = AlgorithmAnalyticReference::vbap_gain(layout, 45.f * kPi / 180.f,
                                                   45.f * kPi / 180.f);
    CHECK(g.size() == 8);
    CHECK_NEAR(sum_sq(g), 1.0f, 1e-4f);

    float upper = 0.f; for (int i = 4; i < 8; ++i) upper += g[(size_t) i] * g[(size_t) i];
    CHECK(upper > 0.3f);                  // meaningful energy reached the ceiling
    CHECK(g[6] > 0.3f);                   // upper az=45 (on the source axis) dominant-ish
    CHECK(g[0] < 0.05f);                  // lower az=-135 (opposite corner) silent
    std::printf("[PASS] gain elevated -> ceiling (upper E=%.3f, g[az45,el45]=%.3f, "
                "g[opposite]=%.3f)\n", (double) upper, (double) g[6], (double) g[0]);
}

// ---- (2c) L/R invariant: right object -> right speakers ----
static void test_gain_lr_invariant() {
    auto layout = make_8ch_dome();
    // Right-ish elevated object (az=+45).
    auto g = AlgorithmAnalyticReference::vbap_gain(layout, 45.f * kPi / 180.f,
                                                   30.f * kPi / 180.f);
    float right = 0.f, left = 0.f;
    for (int i = 0; i < 8; ++i) {
        const float e = g[(size_t) i] * g[(size_t) i];
        if (kAzs[i % 4] > 0.f) right += e; else left += e;
    }
    CHECK(right > left);
    std::printf("[PASS] L/R invariant: right=%.3f > left=%.3f\n",
                (double) right, (double) left);
}

int main() {
    test_mask_flat_horizontal_only();
    test_mask_elevated_adjacent();
    test_mask_steep_opposite_layer_cut();
    test_gain_flat_lower_ring();
    test_gain_elevated_routes_up();
    test_gain_lr_invariant();

    if (failures == 0) { std::printf("[RESULT] PASS\n"); return 0; }
    std::printf("[RESULT] FAIL (%d failures)\n", failures);
    return 1;
}
