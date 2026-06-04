// test_convergence_room_early.cpp
// Dreamscape Convergence ⑥c — Shoebox early-reflection compute core
// (iae::RoomEarly): first-order image geometry, per-reflection delay/gain, and
// the width-spread direction. Verifies the geometry/timing invariants in
// isolation (the per-object ring-buffer rendering + live wiring are ⑥d).

#include "render/ported/RoomEarly.h"

#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

static bool approx(float a, float b, float eps = 1.0e-4f) { return std::abs(a - b) < eps; }
static bool is_unit(const iae::Vec3& v) { return approx(iae::length(v), 1.f, 1.0e-3f); }

int main() {
    const double SR = 48000.0;
    // Use a generous ring for the geometry/ordering checks so true reflection
    // delays are not clamped (a 6 m room's paths exceed the live 512-tap ring —
    // that saturation is exercised separately below).
    const int    ringLen = 4096;

    // --- firstOrderImage: exact mirror geometry (RoomEngine.cpp:20-39) --------
    {
        const iae::Vec3 p { 1.f, 0.5f, 2.f };
        const iae::Vec3 h { 6.f, 5.f, 3.f };
        const auto i0 = iae::firstOrderImage(p, h, 0); // +x wall: {2hx-px, py, pz}
        CHECK(approx(i0.x, 2.f*6.f - 1.f) && approx(i0.y, 0.5f) && approx(i0.z, 2.f));
        const auto i1 = iae::firstOrderImage(p, h, 1); // -x wall: {-2hx-px, py, pz}
        CHECK(approx(i1.x, -2.f*6.f - 1.f) && approx(i1.y, 0.5f) && approx(i1.z, 2.f));
        const auto i4 = iae::firstOrderImage(p, h, 4); // +z wall: {px, py, 2hz-pz}
        CHECK(approx(i4.x, 1.f) && approx(i4.y, 0.5f) && approx(i4.z, 2.f*3.f - 2.f));
    }

    // --- computeFirstOrderReflections: 6 valid taps, delays/gains sane ---------
    {
        iae::RoomEarlyParams pr;                 // default room {6,5,3}, bal 0.45
        const iae::Vec3 obj { 1.f, 0.f, 2.f };   // a touch right & in front
        iae::EarlyReflection r[iae::kNumFirstOrderImages];
        const int nValid = iae::computeFirstOrderReflections(obj, pr, SR, ringLen, r);
        CHECK(nValid == 6);                       // none degenerate for this room/source
        for (int k = 0; k < 6; ++k) {
            CHECK(r[k].valid);
            CHECK(r[k].delaySamples >= 1 && r[k].delaySamples <= ringLen - 2);
            CHECK(r[k].gain > 0.f && std::isfinite(r[k].gain));
            CHECK(is_unit(r[k].dir));
        }
        // The far -x wall (image at -2hx-px) is farther than the near +x wall, so
        // its extra delay is strictly larger. (tap0 = +x, tap1 = -x)
        CHECK(r[1].delaySamples > r[0].delaySamples);
        // Greater extra path -> more attenuation: the longer-delay tap is quieter.
        CHECK(r[1].gain < r[0].gain);
        std::printf("[reflections] +x: d=%d g=%.3f   -x: d=%d g=%.3f\n",
                    r[0].delaySamples, r[0].gain, r[1].delaySamples, r[1].gain);
    }

    // --- delay grows with room size (longer paths) ----------------------------
    {
        const iae::Vec3 obj { 0.5f, 0.f, 1.f };
        iae::EarlyReflection rs[6], rb[6];
        iae::RoomEarlyParams small; small.halfExtents = { 3.f, 3.f, 2.f };
        iae::RoomEarlyParams big;   big.halfExtents   = { 12.f, 10.f, 6.f };
        iae::computeFirstOrderReflections(obj, small, SR, ringLen, rs);
        iae::computeFirstOrderReflections(obj, big,   SR, ringLen, rb);
        // Bigger room -> longer reflection paths -> larger delays on the -x wall.
        CHECK(rb[1].delaySamples > rs[1].delaySamples);
    }

    // --- ring saturation: a large room clamps delays to ringLen-2 (faithful) ---
    {
        iae::RoomEarlyParams pr;                 // {6,5,3}: paths ~10 m > 512 taps
        const iae::Vec3 obj { 1.f, 0.f, 2.f };
        iae::EarlyReflection r[6];
        iae::computeFirstOrderReflections(obj, pr, SR, /*ringLen*/ 512, r);
        CHECK(r[0].delaySamples == 510 && r[1].delaySamples == 510); // clamped
    }

    // --- earlySpreadDirection: width=0 -> identity; width>0 -> distinct units --
    {
        const iae::Vec3 u = iae::normalized(iae::Vec3{ 0.3f, 0.1f, 1.f });
        const auto z = iae::earlySpreadDirection(u, 0.f, 1, iae::kEarlySpreadSamples);
        CHECK(approx(z.x, u.x) && approx(z.y, u.y) && approx(z.z, u.z)); // width 0 -> u

        const auto s0 = iae::earlySpreadDirection(u, 60.f, 0, iae::kEarlySpreadSamples);
        const auto s1 = iae::earlySpreadDirection(u, 60.f, 1, iae::kEarlySpreadSamples);
        const auto s2 = iae::earlySpreadDirection(u, 60.f, 2, iae::kEarlySpreadSamples);
        CHECK(is_unit(s0) && is_unit(s1) && is_unit(s2));
        // centre sample (wi=1) is the unaltered axis; the outer two are spread off it
        CHECK(approx(s1.x, u.x, 1.0e-3f) && approx(s1.y, u.y, 1.0e-3f));
        const float d0 = iae::dot(s0, u), d2 = iae::dot(s2, u);
        CHECK(d0 < 0.9999f && d2 < 0.9999f);          // genuinely rotated off-axis
        CHECK(approx(d0, d2, 1.0e-3f));                // symmetric about the axis
    }

    if (failures == 0) std::printf("test_convergence_room_early: ALL PASS\n");
    return failures;
}
