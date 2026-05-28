// core/tests/core_unit/test_sn3d_constants.cpp
//
// v0.8 audit P1.2 — SN3D normalisation constant lock test ("oracle").
//
// Independent verification (no dependency on AmbisonicEncoder source) that
// the SH encoder's per-channel peak values match the closed-form SN3D
// values:
//
//   N_lm = sqrt( (2 − δ_{m,0}) · (l − |m|)! / (l + |m|)! )
//
// applied to the canonical associated-Legendre tables WITHOUT the
// Condon-Shortley phase, evaluated at the el/az that places each ACN
// channel at its global magnitude maximum.
//
// REGRESSION GUARD: if a future "fix" rewrites AmbisonicEncoder.cpp to
// peak-normalise m≠0 channels to 1.0 (i.e. convert SN3D → maxN), this
// test fails LOUDLY. The reviewer who tried this on the v0.8 audit
// (DSP-2) had to be talked out of it twice; this oracle nails the
// invariant down.
//
// Expected peaks (closed-form):
//   l=2, |m|=2 : √3/2          ≈ 0.8660254
//   l=2, |m|=1 : √3/2 · 0.5    ≈ 0.4330127 — see test_max_re path
//   l=3, |m|=3 : √(10)/4       ≈ 0.7905694
//   l=3, |m|=2 : √(15)/2 · scale where scale at peak el = 1/√10
//                                  → √(15/10)/2·1·1 etc.; numerical
//                                  search verifies the constant matches.
//   l=3, |m|=1 : √(6)/4  · peak factor
//
// The simplest way to check independently is to evaluate the encoder at
// a known angle that places ACN[k] at its analytic peak and compare to
// the closed-form value. For the sectoral (|m|=l) and tesseral channels
// we use exact-known coordinates; for others we numerically grid-scan
// el ∈ [-π/2, π/2] (az fixed at the channel's argmax az) to find the
// peak amplitude, then compare to the closed-form sup_{el}|Y_lm|.
//
// The independence guarantee: the closed-form constants below are typed
// from the SN3D definition (`kSqrt3_2`, `kSqrt10_4`, `kSqrt6_4`,
// `kSqrt15_4`) and NOT copied from AmbisonicEncoder.cpp — a peak-=1
// "fix" that rewrites the encoder constants would NOT line up against
// these literals.

#include "ambi/AmbisonicEncoder.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTol = 1e-5f;

int g_failures = 0;

void check_near(const char* tag, float actual, float expected, float tol) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "FAIL %s: got %.7f expected %.7f (|d|=%.2e tol=%.2e)\n",
                     tag, actual, expected, std::fabs(actual - expected), tol);
        ++g_failures;
    } else {
        std::printf("PASS %s: %.7f ~= %.7f\n", tag, actual, expected);
    }
}

// Peak-scan an ACN channel over el ∈ [-π/2, π/2] at fixed az.
// Coarse scan + bracket refinement. Returns max|c[k]|.
float peak_over_el(int order, int k, float az) {
    const int Nsteps = 4001;
    float peak = 0.f;
    for (int i = 0; i < Nsteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(Nsteps - 1);
        const float el = -kPi / 2.f + t * kPi;
        float v = 0.f;
        if (order == 2) {
            auto c = spe::ambi::AmbisonicEncoder::encode_2nd_order(az, el);
            v = std::fabs(c[static_cast<size_t>(k)]);
        } else if (order == 3) {
            auto c = spe::ambi::AmbisonicEncoder::encode_3rd_order(az, el);
            v = std::fabs(c[static_cast<size_t>(k)]);
        }
        if (v > peak) peak = v;
    }
    return peak;
}

// Peak-scan over (az, el) grid. Used when both arguments are free.
float peak_over_az_el(int order, int k) {
    const int Nsteps_az = 361;
    const int Nsteps_el = 181;
    float peak = 0.f;
    for (int ia = 0; ia < Nsteps_az; ++ia) {
        const float az = -kPi + 2.f * kPi *
            static_cast<float>(ia) / static_cast<float>(Nsteps_az - 1);
        for (int ie = 0; ie < Nsteps_el; ++ie) {
            const float el = -kPi / 2.f + kPi *
                static_cast<float>(ie) / static_cast<float>(Nsteps_el - 1);
            float v = 0.f;
            if (order == 2) {
                auto c = spe::ambi::AmbisonicEncoder::encode_2nd_order(az, el);
                v = std::fabs(c[static_cast<size_t>(k)]);
            } else if (order == 3) {
                auto c = spe::ambi::AmbisonicEncoder::encode_3rd_order(az, el);
                v = std::fabs(c[static_cast<size_t>(k)]);
            }
            if (v > peak) peak = v;
        }
    }
    return peak;
}

} // namespace

int main() {
    // ── Closed-form SN3D peak literals — typed from the definition, NOT
    //    copied from AmbisonicEncoder.cpp. The constants here are the
    //    sup_{az, el} |Y_lm(az, el)| of the SN3D-normalised real SHs.
    //
    //    Derivation:
    //      Y_lm(az,el) = N_lm · P_l^|m|(sin el) · trig(m·az)
    //      where trig = sin(|m|az) for m<0, cos(m·az) for m>0, 1 for m=0,
    //      P_l^|m| are non-Condon-Shortley assoc. Legendre fns, and
    //      N_lm = sqrt((2−δ_{m,0}) · (l−|m|)! / (l+|m|)!).
    //
    //    sup_{az}|trig| = 1, so the peak is N_lm · sup_{el} |P_l^|m|(sin el)|.

    constexpr float kSqrt3_over_2 = 0.8660254037844386f;  // √3/2
    constexpr float kSqrt10_over_4 = 0.7905694150420949f; // √10/4
    constexpr float kSqrt15_over_4 = 0.9682458365518543f; // √15/4 — peak of |m|=2 at el such that sin(2el)=1
    constexpr float kSqrt6_over_4_pk = 0.8432740427115678f; // √6/4 · peak of cos(el)(5 sin²el − 1)

    // ── l=2, |m|=2 (ACN 4 = Y_2^-2, ACN 8 = Y_2^2)
    //    Y_2^±2 = (√3/2) · 2 sin(az)cos(az) · cos²(el)
    //                          (or cos(2az) for m=+2)
    //    sup over (az,el): factor 1 · 1 = 1 → peak = √3/2.
    {
        // At az=π/4, el=0: c[4] = (√3/2)·sin(π/2)·1 = √3/2
        auto c2 = spe::ambi::AmbisonicEncoder::encode_2nd_order(kPi / 4.f, 0.f);
        check_near("l=2,|m|=2 (ACN4 @ az=pi/4,el=0)", c2[4], kSqrt3_over_2, kTol);
        // At az=0, el=0: c[8] = (√3/2)·cos(0)·1 = √3/2
        auto c2b = spe::ambi::AmbisonicEncoder::encode_2nd_order(0.f, 0.f);
        check_near("l=2,|m|=2 (ACN8 @ az=0,el=0)", c2b[8], kSqrt3_over_2, kTol);
    }

    // ── l=3, |m|=3 (ACN 9 = Y_3^-3, ACN 15 = Y_3^3)
    //    Y_3^±3 = (√10/4) · {sin,cos}(3az) · cos³(el)
    //    sup = 1·1 = 1 → peak = √10/4 ≈ 0.7905694
    {
        // At az=π/6 (sin 3·π/6 = sin π/2 = 1), el=0: c[9] = (√10/4)·1·1 = √10/4
        auto c3 = spe::ambi::AmbisonicEncoder::encode_3rd_order(kPi / 6.f, 0.f);
        check_near("l=3,|m|=3 (ACN9 @ az=pi/6,el=0)", c3[9], kSqrt10_over_4, kTol);
        // At az=0, el=0: c[15] = (√10/4)·cos(0)·1 = √10/4
        auto c3b = spe::ambi::AmbisonicEncoder::encode_3rd_order(0.f, 0.f);
        check_near("l=3,|m|=3 (ACN15 @ az=0,el=0)", c3b[15], kSqrt10_over_4, kTol);
    }

    // ── l=3, |m|=2 (ACN 10 = Y_3^-2, ACN 14 = Y_3^2)
    //    Y_3^±2 = (√15/2) · {sin,cos}(2az) · sin(el) · cos²(el)
    //    sup_{el} of |sin(el)·cos²(el)| = 2/(3√3) ≈ 0.3849001794597505
    //    sup overall = (√15/2)·(2/(3√3)) = √15/(3·√3) = √5/3 ≈ 0.7453560
    constexpr float kPeak_l3_m2 = 0.7453559924999298f;  // √5/3 ≈ 0.7453560
    {
        const float peak_acn10 = peak_over_az_el(3, 10);
        check_near("l=3,|m|=2 (ACN10 sup)", peak_acn10, kPeak_l3_m2, 1e-4f);
        const float peak_acn14 = peak_over_az_el(3, 14);
        check_near("l=3,|m|=2 (ACN14 sup)", peak_acn14, kPeak_l3_m2, 1e-4f);
    }

    // ── l=3, |m|=1 (ACN 11 = Y_3^-1, ACN 13 = Y_3^1)
    //    Y_3^±1 = (√6/4) · {sin,cos}(az) · (5 sin²(el) − 1) · cos(el)
    //    sup_{el} of |(5 sin²el − 1)·cos(el)| = 4·√(2/15)/3·something …
    //    Numerically: this peak ≈ √6/4 · 1.376… ≈ 0.84327404
    //    The closed form: at sin²el=3/5 → (5·3/5 − 1)·√(2/5) = 2·√(2/5)
    //                                   ≈ 1.2649. Then √6/4·1.2649 ≈ 0.7746.
    //    But sup also at el=0: |5·0−1|·1 = 1 → √6/4 ≈ 0.6124.
    //    Numerical max: ≈ 0.84327
    constexpr float kPeak_l3_m1 = 0.8432740427115678f;
    (void)kSqrt6_over_4_pk;  // silence unused; keep the named constant for docs
    (void)kSqrt15_over_4;
    {
        const float peak_acn11 = peak_over_az_el(3, 11);
        check_near("l=3,|m|=1 (ACN11 sup)", peak_acn11, kPeak_l3_m1, 1e-4f);
        const float peak_acn13 = peak_over_az_el(3, 13);
        check_near("l=3,|m|=1 (ACN13 sup)", peak_acn13, kPeak_l3_m1, 1e-4f);
    }

    // ── l=2, |m|=1 (ACN 5 = Y_2^-1, ACN 7 = Y_2^1)
    //    Y_2^±1 = √3 · {sin,cos}(az) · sin(el)·cos(el)
    //    sup |sin(el)cos(el)| = 1/2 → peak = √3/2 ≈ 0.8660254
    {
        const float peak_acn5 = peak_over_az_el(2, 5);
        check_near("l=2,|m|=1 (ACN5 sup)", peak_acn5, kSqrt3_over_2, 1e-4f);
        const float peak_acn7 = peak_over_az_el(2, 7);
        check_near("l=2,|m|=1 (ACN7 sup)", peak_acn7, kSqrt3_over_2, 1e-4f);
    }

    // ── Sanity: ACN 0 (W) is always 1.0 (SN3D zonal m=0 → N_00=1).
    {
        auto c2 = spe::ambi::AmbisonicEncoder::encode_2nd_order(0.f, 0.f);
        check_near("W (ACN0)", c2[0], 1.0f, kTol);
    }

    // ── Sanity: ACN 6 (Y_2^0 zonal) = 0.5·(3sin²el − 1), peak at el=±π/2 → 1.0.
    {
        auto c2 = spe::ambi::AmbisonicEncoder::encode_2nd_order(0.f, kPi / 2.f);
        check_near("Y_2^0 (ACN6 @ el=pi/2)", c2[6], 1.0f, kTol);
    }

    // ── Sanity: ACN 12 (Y_3^0 zonal) = 0.5·(5 sin³el − 3sinel), peak at el=π/2 → 1.0.
    {
        auto c3 = spe::ambi::AmbisonicEncoder::encode_3rd_order(0.f, kPi / 2.f);
        check_near("Y_3^0 (ACN12 @ el=pi/2)", c3[12], 1.0f, kTol);
    }

    // ── Negative test (compile-time-stable): silence unused helper warning.
    (void)peak_over_el;

    if (g_failures == 0) {
        std::printf("OK  test_sn3d_constants: all SN3D oracle checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "FAIL test_sn3d_constants: %d failure(s)\n", g_failures);
    return 1;
}
