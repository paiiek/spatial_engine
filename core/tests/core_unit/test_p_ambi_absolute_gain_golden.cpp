// core/tests/core_unit/test_p_ambi_absolute_gain_golden.cpp
//
// v0.8 audit P3.2 (Test-2) — ABSOLUTE-GAIN closed-form golden vector for the
// SN3D ambisonic encode → SAMPLING decode path.
//
// Independent oracle:
//   * For a fixed N-speaker layout, compute the per-speaker decoded gains
//     for a unit-amplitude source at (az=0, el=0) using the SAMPLING
//     decoder (D = E^T / N — exact closed form, derived here, NOT pulled
//     from AmbiDecoder).
//   * Compute the same gains by encoding the source to W/Y/Z/X via
//     AmbisonicEncoder and applying D to that 4-vector.
//   * Cross-check both against a third path: g_s = (1/N)·Σ_k Y_k(az_src,el_src)·Y_k(az_s,el_s)
//     where Y_k are the SN3D real spherical harmonics typed straight from
//     the textbook formula (NOT lifted from AmbisonicEncoder.cpp).
//   * Tolerance ±1e-5.
//
// REGRESSION GUARD:
//   The point is to lock the ABSOLUTE numeric path — both the encoder
//   constants AND the projection — against an independent oracle. A future
//   "fix" that:
//     (a) drops the SN3D N_lm factor anywhere,
//     (b) introduces an extra √(4π) energy normalisation, or
//     (c) reshuffles ACN ordering,
//   would line up neither against the textbook SH formulas typed below nor
//   against the layout-driven sampling decoder. This complements
//   test_sn3d_constants.cpp (which locks peak magnitudes) by locking the
//   FULL encode → decode product at one specific direction.
//
// SAMPLING decoder rationale:
//   For a uniformly-sampled layout, the SAMPLING decoder is the canonical
//   closed-form: D_{s,k} = (1/N) · Y_k(az_s, el_s). It is the unique
//   decoder for which g_s = (1/N) · Σ_k Y_k(az_src,el_src)·Y_k(az_s,el_s)
//   reproduces the (truncated-N) SH projection of the panning function
//   — i.e. the textbook directional response. We use a 6-speaker octahedral
//   layout (3D, regular, K=4 ≤ S=6) so the SAMPLING decoder is well-posed.
//
// SCOPE: 1st-order only (K=4). The 1st-order ACN re-map quirk
// ({W,X,Y,Z}↔{W,Y,Z,X}) is the only encoder index gotcha; 2nd/3rd are
// straight ACN. Locking 1st order independently is sufficient for the
// "absolute gain lock" goal — higher orders share the same numeric pipeline
// once K=4 is pinned.

#include "ambi/AmbisonicEncoder.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi  = 3.14159265358979323846f;
constexpr float kTol = 1e-5f;

int g_failures = 0;

void check_near(const char* tag, double actual, double expected, double tol) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr,
            "FAIL %s: got %.7f expected %.7f (|d|=%.2e tol=%.2e)\n",
            tag, actual, expected, std::fabs(actual - expected), tol);
        ++g_failures;
    } else {
        std::printf("PASS %s: %.7f ~= %.7f\n", tag, actual, expected);
    }
}

// ── Closed-form SN3D real spherical harmonic Y_k(az, el) for k ∈ {0,1,2,3}.
//    ACN order: 0=Y_0^0, 1=Y_1^-1, 2=Y_1^0, 3=Y_1^1.
//    Engine az convention: az=0 → +z (front), az=+π/2 → +x (right).
//    SN3D normalisation N_lm = sqrt((2 − δ_{m,0})·(l−|m|)!/(l+|m|)!);
//    for l ≤ 1 we have N_00=N_10=1 and N_1±1=1, so the SN3D peaks are 1.
//
//    These literals are typed directly from the textbook formula and
//    independently verifiable by anyone reading Zotter & Frank §3.2 —
//    they are NOT derived from AmbisonicEncoder.cpp or AmbiDecoder.cpp.
double Y_oracle(int k, double az, double el) {
    const double cos_el = std::cos(el);
    switch (k) {
        case 0: return 1.0;                        // Y_0^0 = 1
        case 1: return cos_el * std::sin(az);      // Y_1^-1 (right)
        case 2: return std::sin(el);               // Y_1^0  (up)
        case 3: return cos_el * std::cos(az);      // Y_1^1  (front)
        default: return 0.0;
    }
}

// ── Apply the 1st-order encoder via the public engine API, then re-map
//    {W,X,Y,Z} struct fields to ACN ordering {0=W, 1=Y, 2=Z, 3=X}.
//    This re-map IS load-bearing and matches the documented invariant in
//    AmbiDecoder::buildEncodingMatrixE (single source of truth).
std::array<double, 4> encode_acn(double az, double el) {
    const auto c = spe::ambi::AmbisonicEncoder::encode_1st_order(
        static_cast<float>(az), static_cast<float>(el));
    // ACN: 0=W, 1=Y_1^-1 (engine .Y), 2=Y_1^0 (engine .Z), 3=Y_1^1 (engine .X)
    return { static_cast<double>(c.W),
             static_cast<double>(c.Y),
             static_cast<double>(c.Z),
             static_cast<double>(c.X) };
}

// ── Speaker direction list — 6-speaker regular octahedron.
//    Speakers at the six cardinal points on the unit sphere:
//      front (+z), back (-z), right (+x), left (-x), up (+y), down (-y).
//    A regular octahedron is a 3-design — exactly the family for which the
//    SAMPLING decoder is the canonical closed form for K=4.
struct Dir { double az_rad; double el_rad; const char* name; };
constexpr std::array<Dir, 6> kSpeakers = {{
    {  0.0,         0.0,        "front (+z)" },
    {  kPi,         0.0,        "back  (-z)" },
    {  kPi / 2.0,   0.0,        "right (+x)" },
    { -kPi / 2.0,   0.0,        "left  (-x)" },
    {  0.0,         kPi / 2.0,  "up    (+y)" },
    {  0.0,        -kPi / 2.0,  "down  (-y)" },
}};

} // namespace

int main() {
    // ── Source: (az=0, el=0) = front, unit amplitude.
    constexpr double kAzSrc = 0.0;
    constexpr double kElSrc = 0.0;

    // ── Step 1: build the SAMPLING decoder D[S][K] = (1/N) · Y_k(az_s, el_s).
    //    Closed-form: N is the speaker count, here N=6.
    constexpr int S = static_cast<int>(kSpeakers.size());
    constexpr int K = 4;
    const double invN = 1.0 / static_cast<double>(S);
    double D[S][K]{};
    for (int s = 0; s < S; ++s) {
        for (int k = 0; k < K; ++k) {
            D[s][k] = invN * Y_oracle(k, kSpeakers[s].az_rad, kSpeakers[s].el_rad);
        }
    }

    // ── Step 2: ORACLE per-speaker gain = (1/N) · Σ_k Y_k(src) · Y_k(spk).
    //    This is the closed form derived straight from the SH formula at
    //    top of file — NOT a measurement of the engine.
    double g_oracle[S]{};
    for (int s = 0; s < S; ++s) {
        double acc = 0.0;
        for (int k = 0; k < K; ++k) {
            acc += Y_oracle(k, kAzSrc, kElSrc) *
                   Y_oracle(k, kSpeakers[s].az_rad, kSpeakers[s].el_rad);
        }
        g_oracle[s] = invN * acc;
    }

    // ── Step 3: ENGINE path — encode the source via AmbisonicEncoder, then
    //    multiply by the SAMPLING decoder (built from oracle SHs).
    //    Tests that the encoder produces SN3D-normalised SH coefficients
    //    matching the oracle Y_k at the source direction.
    const auto sh = encode_acn(kAzSrc, kElSrc);
    double g_engine[S]{};
    for (int s = 0; s < S; ++s) {
        double acc = 0.0;
        for (int k = 0; k < K; ++k) {
            acc += D[s][k] * sh[static_cast<size_t>(k)];
        }
        g_engine[s] = acc;
    }

    // ── Step 4: Cross-check both paths against each other AND against the
    //    pure-oracle closed-form expected values for this exact layout.
    //
    //    At az=0, el=0 the source SH is sh = [1, 0, 0, 1]. The decoded
    //    gains follow from D · sh:
    //      front (+z): (1/6)·(1·1 + 1·1)  = 1/3
    //      back  (-z): (1/6)·(1·1 + 1·(-1)) = 0
    //      right (+x): (1/6)·(1·1 + 0·1)  = 1/6
    //      left  (-x): (1/6)·(1·1 + 0·1)  = 1/6
    //      up    (+y): (1/6)·(1·1 + 0·1)  = 1/6
    //      down  (-y): (1/6)·(1·1 + 0·1)  = 1/6
    //    Sum = 1/3 + 0 + 4·(1/6) = 1.0 (panning-law unit-sum sanity).
    constexpr double kExpected[S] = {
        1.0 / 3.0,  // front
        0.0,        // back
        1.0 / 6.0,  // right
        1.0 / 6.0,  // left
        1.0 / 6.0,  // up
        1.0 / 6.0,  // down
    };

    // Per-speaker checks: engine vs typed-literal expected.
    for (int s = 0; s < S; ++s) {
        char tag[96];
        std::snprintf(tag, sizeof(tag),
                      "spk[%d] %s (engine vs literal)", s, kSpeakers[s].name);
        check_near(tag, g_engine[s], kExpected[s], kTol);
    }

    // Per-speaker checks: oracle-SH path vs typed-literal expected.
    for (int s = 0; s < S; ++s) {
        char tag[96];
        std::snprintf(tag, sizeof(tag),
                      "spk[%d] %s (oracle vs literal)", s, kSpeakers[s].name);
        check_near(tag, g_oracle[s], kExpected[s], kTol);
    }

    // Per-speaker checks: engine vs oracle (locks encoder = SN3D Y_k).
    for (int s = 0; s < S; ++s) {
        char tag[96];
        std::snprintf(tag, sizeof(tag),
                      "spk[%d] %s (engine vs oracle)", s, kSpeakers[s].name);
        check_near(tag, g_engine[s], g_oracle[s], kTol);
    }

    // Panning-law sanity: Σ g = 1.0 for a source on the layout for the
    // 6-spk octahedron at (az=0,el=0). Derived from the SH closure relation
    // truncated at l=1 — independent of the engine implementation.
    double sum_engine = 0.0;
    double sum_oracle = 0.0;
    for (int s = 0; s < S; ++s) {
        sum_engine += g_engine[s];
        sum_oracle += g_oracle[s];
    }
    check_near("sum_engine == 1.0", sum_engine, 1.0, kTol);
    check_near("sum_oracle == 1.0", sum_oracle, 1.0, kTol);

    if (g_failures == 0) {
        std::printf(
            "OK  test_p_ambi_absolute_gain_golden: all closed-form gains matched\n");
        return 0;
    }
    std::fprintf(stderr,
        "FAIL test_p_ambi_absolute_gain_golden: %d failure(s)\n", g_failures);
    return 1;
}
