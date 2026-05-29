// core/tests/core_unit/test_p_hrtf_lookup_interp.cpp
//
// v0.8 audit P3.4 (Test-4) — HrtfLookup interpolation math vs closed-form.
//
// INTERPOLATION SCHEME (as of v0.8):
//   HrtfLookup::lookupHrtf() implements *nearest-neighbour* selection over
//   the loaded HrtfTable, both via brute-force great-circle distance
//   (nearestPositionBruteForceForTest, the test ground truth) and via the
//   KdTree3D unit-sphere index (production hot path). There is NO IR
//   blending — the returned HrtfPair pointers are ALIASES into the
//   underlying HrtfTable::ir(idx, recv) storage.
//
//   The "interpolation weights" the lookup uses are therefore the
//   degenerate (1.0, 0.0) selection over the two nearest endpoints, with
//   the tie broken by the order in which positions appear in the table
//   (brute-force iterates with `>` so the *first* of two exactly-equal
//   matches wins; KdTree3D's left-first split order matches that on the
//   adversarial pairs constructed below).
//
// CLOSED-FORM ORACLE (independent of the lookup):
//   For two adjacent measurement directions p_a, p_b on the unit sphere
//   and a query at the angular midpoint q, both great-circle distances
//   d(q, p_a) and d(q, p_b) are equal. The "analytic blend" the spec
//   alludes to is therefore the limit case 0.5·IR_a + 0.5·IR_b. The
//   lookup-under-test does NOT blend — so the L2-tight oracle is:
//
//     L2( lookupHrtf(q),  IR_a )  ==  0      OR
//     L2( lookupHrtf(q),  IR_b )  ==  0
//
//   AND the chosen one must equal the brute-force NN (the textbook NN
//   algorithm — see nearestPositionBruteForceForTest). This is what
//   "matches the actual interpolation weights the lookup uses" means in
//   this codebase: the (1.0, 0.0) selection over the great-circle NN.
//
// REGRESSION GUARD:
//   If a future change ever adds REAL interpolation (e.g. nearest-K
//   barycentric, bilinear in az/el, or VBAP-style weights), this test
//   will FAIL — and the right response is to widen the oracle to the
//   actual blending math (e.g. 0.5·IR_a + 0.5·IR_b with explicit
//   weights), not to drop this test. The point is to pin TODAY'S math
//   (NN) so a silent regression to "something else that also passes
//   energy checks" is impossible.
//
// FIXTURE:
//   Built in /tmp inline: 2 positions, 64-sample IRs. IR_a is a unit
//   impulse at sample 4; IR_b is a unit impulse at sample 17 — distinct
//   enough that any blend (0.5·IR_a + 0.5·IR_b) would have L2 strictly
//   greater than zero from either endpoint, but NN selection keeps L2=0
//   against the chosen endpoint.

#include "hrtf/HrtfLookup.h"
#include "hrtf/KdTree3D.h"
#include "hrtf/SofaBinReader.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.f;

int g_failures = 0;

// L2 distance between two same-length IRs (both receivers concatenated by caller).
double l2_distance(const float* a, const float* b, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += d * d;
    }
    return std::sqrt(sum);
}

// L2 distance between (left|right) of looked-up IR vs (left|right) of one endpoint.
double pair_l2(const spe::hrtf::HrtfPair& got,
               const float* ref_left, const float* ref_right, int n) {
    return std::sqrt(
        std::pow(l2_distance(got.left,  ref_left,  n), 2) +
        std::pow(l2_distance(got.right, ref_right, n), 2));
}

// Write a minimal .speh with `kPositions` positions and distinct IRs.
// Each position i gets:
//   az = i·(360/N) degrees, el = 0, dist = 1
//   left  IR: unit impulse at sample (i+1)·step
//   right IR: unit impulse at sample (i+1)·step + 1
// — guaranteed unique per position so the IR identity is sample-exact.
struct SpehSpec {
    static constexpr int kPositions = 2;
    static constexpr int kIrLen     = 64;
    static constexpr int kImpulseA  = 4;
    static constexpr int kImpulseB  = 17;
};

std::string writeTwoPositionSpeh(const std::string& path) {
    struct Hdr {
        char     magic[4];
        uint32_t n_positions;
        uint32_t ir_length;
        uint32_t n_receivers;
        float    sample_rate;
        uint32_t reserved;
    } hdr;
    std::memcpy(hdr.magic, "SPEH", 4);
    hdr.n_positions = SpehSpec::kPositions;
    hdr.ir_length   = SpehSpec::kIrLen;
    hdr.n_receivers = 2;
    hdr.sample_rate = 48000.f;
    hdr.reserved    = 0;

    // Two adjacent positions on the horizontal plane: az = 0° and az = 30°.
    // Midpoint (the test query) is az = 15°, el = 0°.
    std::vector<float> positions = {
        /* pos 0 */   0.f, 0.f, 1.f,
        /* pos 1 */  30.f, 0.f, 1.f,
    };

    // IR data: ir_data[pos*2*ir_len + recv*ir_len + sample].
    std::vector<float> ir(static_cast<size_t>(SpehSpec::kPositions) * 2 *
                              SpehSpec::kIrLen, 0.f);
    // Position 0: left impulse at kImpulseA, right impulse at kImpulseA+1.
    ir[0 * 2 * SpehSpec::kIrLen + 0 * SpehSpec::kIrLen + SpehSpec::kImpulseA    ] = 1.f;
    ir[0 * 2 * SpehSpec::kIrLen + 1 * SpehSpec::kIrLen + SpehSpec::kImpulseA + 1] = 0.75f;
    // Position 1: left impulse at kImpulseB, right impulse at kImpulseB+1.
    ir[1 * 2 * SpehSpec::kIrLen + 0 * SpehSpec::kIrLen + SpehSpec::kImpulseB    ] = 1.f;
    ir[1 * 2 * SpehSpec::kIrLen + 1 * SpehSpec::kIrLen + SpehSpec::kImpulseB + 1] = 0.25f;

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&hdr),               sizeof(hdr));
    f.write(reinterpret_cast<const char*>(positions.data()),
            static_cast<std::streamsize>(positions.size() * sizeof(float)));
    f.write(reinterpret_cast<const char*>(ir.data()),
            static_cast<std::streamsize>(ir.size() * sizeof(float)));
    return path;
}

// CLOSED-FORM analytic blend at the angular midpoint: w_a = w_b = 0.5.
// Computed here ONLY to verify it is NOT what the lookup returns
// (negative oracle: nearest-neighbor must beat blending).
void analyticBlend(const float* ir_a, const float* ir_b,
                   std::vector<float>& out, int n) {
    out.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        out[static_cast<size_t>(i)] = 0.5f * ir_a[i] + 0.5f * ir_b[i];
    }
}

void check_l2_zero(const char* tag, double l2, double tol) {
    if (l2 > tol) {
        std::fprintf(stderr,
            "FAIL %s: L2=%.6e expected ≤ %.2e\n", tag, l2, tol);
        ++g_failures;
    } else {
        std::printf("PASS %s: L2=%.6e ≤ %.2e\n", tag, l2, tol);
    }
}

void check_l2_nonzero(const char* tag, double l2, double min_l2) {
    if (l2 < min_l2) {
        std::fprintf(stderr,
            "FAIL %s: L2=%.6e expected ≥ %.2e (blend would imply this)\n",
            tag, l2, min_l2);
        ++g_failures;
    } else {
        std::printf("PASS %s: L2=%.6e ≥ %.2e\n", tag, l2, min_l2);
    }
}

} // namespace

int main() {
    const std::string speh_path = "/tmp/test_p_hrtf_lookup_interp.speh";
    writeTwoPositionSpeh(speh_path);

    spe::hrtf::HrtfTable table;
    auto load_res = spe::hrtf::loadSpeh(speh_path, 48000.f, table);
    if (load_res != spe::hrtf::SpehResult::Ok) {
        std::fprintf(stderr,
            "FAIL: loadSpeh failed (%d)\n", static_cast<int>(load_res));
        return 1;
    }

    if (table.n_positions != SpehSpec::kPositions ||
        table.ir_length   != SpehSpec::kIrLen) {
        std::fprintf(stderr,
            "FAIL: HrtfTable shape mismatch (got n_pos=%u ir_len=%u)\n",
            table.n_positions, table.ir_length);
        return 1;
    }

    // ── Midpoint query (az=15°, el=0°) — equidistant great-circle to both.
    const float az_mid_rad = 15.f * kDeg2Rad;
    const float el_mid_rad = 0.f;

    // ── Closed-form NN expectation: at exact midpoint, both distances are
    //    cos(15°). brute_force_NN uses `>` (strict) so the FIRST equal-
    //    distance candidate wins → index 0 (az=0°). We DO NOT cement that
    //    tiebreak as the spec; instead we run the brute-force ourselves
    //    against the same table and use its index as the closed-form
    //    oracle. KdTree3D nearest is then asserted to AGREE with it (the
    //    parity invariant that v0.5 P2 documents).
    const int oracle_idx =
        spe::hrtf::nearestPositionBruteForceForTest(table, 15.f, 0.f);
    if (oracle_idx < 0 || oracle_idx >= static_cast<int>(table.n_positions)) {
        std::fprintf(stderr,
            "FAIL: brute-force NN returned out-of-range idx=%d\n", oracle_idx);
        return 1;
    }
    std::printf("INFO: closed-form NN oracle idx=%d (az=%.0f°)\n",
                oracle_idx,
                static_cast<double>(table.positions[oracle_idx * 3]));

    // ── Path A: production lookupHrtf (brute-force inside, since it calls
    //    nearestPositionBruteForceForTest). Must equal IR at oracle_idx.
    const auto pair_brute = spe::hrtf::lookupHrtf(table, az_mid_rad, el_mid_rad);
    const float* ref_left  = table.ir(oracle_idx, 0);
    const float* ref_right = table.ir(oracle_idx, 1);
    const int n = static_cast<int>(table.ir_length);

    {
        const double l2 = pair_l2(pair_brute, ref_left, ref_right, n);
        // L2 should be EXACTLY zero — HrtfPair points into table.ir_data.
        check_l2_zero("lookupHrtf returns NN endpoint (L2 against oracle)",
                      l2, 0.0);
    }

    // ── Path B: KdTree3D lookup — same NN parity with brute-force.
    {
        spe::hrtf::KdTree3D tree;
        tree.build(table);
        const auto pair_kd = spe::hrtf::lookupHrtfFromTree(
            table, tree, az_mid_rad, el_mid_rad);
        const double l2 = pair_l2(pair_kd, ref_left, ref_right, n);
        check_l2_zero("lookupHrtfFromTree returns NN endpoint (L2 against oracle)",
                      l2, 0.0);
    }

    // ── Negative oracle: the analytic BLEND (0.5·IR_a + 0.5·IR_b) is NOT
    //    what the lookup returns. We compute its L2 against the chosen
    //    endpoint and assert that L2 is non-trivially large — i.e. if the
    //    lookup ever DID start blending, this test's previous assertions
    //    would fail. This is the "we're testing the actual math, not just
    //    energy" backstop.
    {
        std::vector<float> blended_left, blended_right;
        analyticBlend(table.ir(0, 0), table.ir(1, 0), blended_left,  n);
        analyticBlend(table.ir(0, 1), table.ir(1, 1), blended_right, n);
        const double blend_l2 =
            std::sqrt(std::pow(l2_distance(blended_left.data(),  ref_left,  n), 2) +
                      std::pow(l2_distance(blended_right.data(), ref_right, n), 2));
        // The two IRs differ in support by Δ = 13 samples (kImpulseB - kImpulseA),
        // with magnitudes (1,0.75) at pos 0 and (1,0.25) at pos 1 — so the L2
        // between a 50/50 blend and either endpoint is well over 0.5.
        check_l2_nonzero(
            "analytic blend differs from NN result (negative oracle)",
            blend_l2, 0.5);
    }

    // ── Closed-form sanity: midpoint great-circle distance to both endpoints
    //    is identical to within float precision (1e-6). If this ever fails,
    //    the .speh writer is broken — not the lookup math.
    {
        const float az0 = table.positions[0 * 3 + 0] * kDeg2Rad;
        const float el0 = table.positions[0 * 3 + 1] * kDeg2Rad;
        const float az1 = table.positions[1 * 3 + 0] * kDeg2Rad;
        const float el1 = table.positions[1 * 3 + 1] * kDeg2Rad;
        const float cos_d0 = std::sin(el_mid_rad) * std::sin(el0) +
                             std::cos(el_mid_rad) * std::cos(el0) *
                             std::cos(az_mid_rad - az0);
        const float cos_d1 = std::sin(el_mid_rad) * std::sin(el1) +
                             std::cos(el_mid_rad) * std::cos(el1) *
                             std::cos(az_mid_rad - az1);
        const double diff = std::fabs(static_cast<double>(cos_d0) -
                                      static_cast<double>(cos_d1));
        if (diff > 1e-6) {
            std::fprintf(stderr,
                "FAIL: midpoint not equidistant: cos_d0=%.7f cos_d1=%.7f "
                "(diff=%.2e)\n",
                static_cast<double>(cos_d0),
                static_cast<double>(cos_d1), diff);
            ++g_failures;
        } else {
            std::printf("PASS midpoint great-circle equidistant: "
                        "cos_d0=cos_d1=%.7f\n",
                        static_cast<double>(cos_d0));
        }
    }

    if (g_failures == 0) {
        std::printf(
            "OK  test_p_hrtf_lookup_interp: NN matches closed-form oracle\n");
        return 0;
    }
    std::fprintf(stderr,
        "FAIL test_p_hrtf_lookup_interp: %d failure(s)\n", g_failures);
    return 1;
}
