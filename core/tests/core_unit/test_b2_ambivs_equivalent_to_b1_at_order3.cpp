// test_b2_ambivs_equivalent_to_b1_at_order3.cpp
//
// v0.5 P4 — B2 perceptual merge gate.
//
// For a single point source, the 3rd-order AmbiVS path (encode → 24-pt
// t-design decode → 24× HRTF convolution → L/R sum) should produce an
// output of comparable energy and lateralisation to direct B1 HRTF panning.
//
// The synthetic_min.speh fixture only ships 4 cardinal HRIRs (az ∈ {0, ±90,
// 180}, el=0) and encodes pure ITD (unit-impulse on each ear, shifted by 0..4
// samples — no ILD). The full -20 dBFS RMS error gate described in the v0.5
// plan requires a real ARI/CIPIC dataset and is exercised in soak runs.
// What this test enforces on the CI fixture:
//   1. B1 and B2 both produce non-zero, finite output for a non-zero
//      mono input.
//   2. RMS energy ratio |20*log10(rms_b2 / rms_b1)| ≤ 12 dB (gross level
//      equivalence — the AmbiVS sum-of-24 path doesn't blow up or collapse).
//   3. Both paths agree on lateral side: source at az=+π/2 (left) yields
//      L-ear temporal centroid earlier than R-ear (ITD precedence — L
//      leads R). With this ITD-only fixture L/R RMS are equal by
//      construction; lateralisation lives in time of arrival, not level.
// On a full HRTF table that adds ILD the time-centroid check still holds
// while the RMS-magnitude gate (used in soak) tightens automatically.

#include "ambi/AmbisonicEncoder.h"
#include "output_backend/BinauralMonitor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

#define REQUIRE(cond)                                                  \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr,                                        \
                         "FAIL: %s (line %d)\n", #cond, __LINE__);      \
            return 1;                                                   \
        }                                                               \
    } while (0)

namespace {

float rms(const std::vector<float>& v)
{
    double acc = 0.0;
    for (float x : v) acc += static_cast<double>(x) * x;
    return static_cast<float>(std::sqrt(acc / std::max<std::size_t>(v.size(), 1)));
}

// Energy-weighted temporal centroid (in samples). For an HRIR that only
// encodes ITD, the centroid of the ear that receives signal later sits at a
// larger sample index than the ear that receives signal first.
double timeCentroid(const std::vector<float>& v)
{
    double num = 0.0, den = 0.0;
    for (std::size_t n = 0; n < v.size(); ++n) {
        const double e = static_cast<double>(v[n]) * v[n];
        num += static_cast<double>(n) * e;
        den += e;
    }
    return den > 0.0 ? num / den : 0.0;
}

} // namespace

int main()
{
    constexpr int   blockSize = 256;
    constexpr float sr        = 48000.f;
    const std::string sofa    = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";

    // Source direction: +π/2 azimuth (engine convention: LEFT), elevation 0.
    const float az = static_cast<float>(M_PI_2);
    const float el = 0.f;

    // Test input: 16-sample tone burst (avoids the discrete-impulse worst
    // case for 3rd-order SH; gives meaningful RMS).
    std::vector<float> mono(blockSize, 0.f);
    for (int n = 0; n < 16; ++n)
        mono[static_cast<std::size_t>(n)] =
            std::sin(2.f * static_cast<float>(M_PI) * 1000.f
                     * static_cast<float>(n) / sr);

    spe::output::BinauralMonitor mon;
    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = sofa;
    cfg.sampleRate = sr;
    cfg.blockSize  = blockSize;
    REQUIRE(mon.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
    REQUIRE(mon.hasHrtf());

    // ─── B1 path ────────────────────────────────────────────────────────
    // setDirection auto-prime on initialize() targeted (0,0); send a few
    // warm-up blocks of silence at the target direction to clear the
    // 2-block crossfade before measuring.
    mon.setDirection(0, az, el);
    std::vector<float> silence(blockSize, 0.f);
    std::vector<float> warmL(blockSize, 0.f), warmR(blockSize, 0.f);
    for (int b = 0; b < 4; ++b)
        mon.processBlockForObject(0, silence.data(), blockSize,
                                  warmL.data(), warmR.data());

    std::vector<float> b1L(blockSize, 0.f), b1R(blockSize, 0.f);
    mon.processBlockForObject(0, mono.data(), blockSize,
                              b1L.data(), b1R.data());

    // ─── B2 path ────────────────────────────────────────────────────────
    // Encode the same mono into 3rd-order ACN-ordered SH (16 channels).
    const auto coeffs = spe::ambi::AmbisonicEncoder::encode_3rd_order(az, el);
    std::vector<std::vector<float>> sh(16, std::vector<float>(blockSize, 0.f));
    std::vector<const float*> sh_ptrs(16, nullptr);
    for (int k = 0; k < 16; ++k) {
        const float ck = coeffs[static_cast<std::size_t>(k)];
        for (int n = 0; n < blockSize; ++n)
            sh[static_cast<std::size_t>(k)][static_cast<std::size_t>(n)] =
                ck * mono[static_cast<std::size_t>(n)];
        sh_ptrs[static_cast<std::size_t>(k)] =
            sh[static_cast<std::size_t>(k)].data();
    }

    mon.setRequestedMode(spe::output::BinauralMode::AmbiVS);
    REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::AmbiVS);

    std::vector<float> b2L(blockSize, 0.f), b2R(blockSize, 0.f);
    mon.processBlockB2(sh_ptrs.data(), 3, blockSize, b2L.data(), b2R.data());

    // ─── Invariants ────────────────────────────────────────────────────
    const float r1L = rms(b1L), r1R = rms(b1R);
    const float r2L = rms(b2L), r2R = rms(b2R);

    REQUIRE(std::isfinite(r1L) && std::isfinite(r1R));
    REQUIRE(std::isfinite(r2L) && std::isfinite(r2R));
    REQUIRE(r1L > 0.f); REQUIRE(r1R > 0.f);
    REQUIRE(r2L > 0.f); REQUIRE(r2R > 0.f);

    // Lateralisation agreement: az=+π/2 (LEFT) → L-ear leads R-ear in time
    // for both paths. The synthetic_min.speh fixture is ITD-only (equal-
    // amplitude unit impulses, delayed on the contralateral ear) so the
    // lateralisation cue is temporal precedence, not level.
    const double t1L = timeCentroid(b1L), t1R = timeCentroid(b1R);
    const double t2L = timeCentroid(b2L), t2R = timeCentroid(b2R);
    if (!(t1R > t1L)) {
        std::fprintf(stderr,
                     "FAIL: B1 lateralisation — L centroid=%.3f, R centroid=%.3f "
                     "(expected R > L for LEFT source)\n", t1L, t1R);
        return 1;
    }
    if (!(t2R > t2L)) {
        std::fprintf(stderr,
                     "FAIL: B2 lateralisation — L centroid=%.3f, R centroid=%.3f "
                     "(expected R > L for LEFT source)\n", t2L, t2R);
        return 1;
    }

    // Gross energy equivalence: the combined L+R RMS should be within
    // ±12 dB between the two paths (sparse fixture; tightens with real HRTF).
    const float e1 = std::sqrt(r1L * r1L + r1R * r1R);
    const float e2 = std::sqrt(r2L * r2L + r2R * r2R);
    const float dB = 20.f * std::log10(e2 / e1);
    if (std::fabs(dB) > 12.f) {
        std::fprintf(stderr,
                     "FAIL: B2/B1 energy ratio = %.2f dB (|.|>12 dB on sparse "
                     "fixture); B1 e=%.4f B2 e=%.4f\n",
                     static_cast<double>(dB),
                     static_cast<double>(e1),
                     static_cast<double>(e2));
        return 1;
    }

    std::printf("B2/B1 energy delta: %+.2f dB (L %+.2f, R %+.2f); "
                "ITD centroids B1 (L=%.2f R=%.2f) B2 (L=%.2f R=%.2f)\n",
                static_cast<double>(dB),
                static_cast<double>(20.f * std::log10(r2L / r1L)),
                static_cast<double>(20.f * std::log10(r2R / r1R)),
                t1L, t1R, t2L, t2R);
    std::puts("PASS test_b2_ambivs_equivalent_to_b1_at_order3");
    return 0;
}
