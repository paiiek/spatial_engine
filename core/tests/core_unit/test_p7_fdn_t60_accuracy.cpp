// core/tests/core_unit/test_p7_fdn_t60_accuracy.cpp
// P3.3 (v0.8 audit): FdnReverb T60 accuracy oracle.
//
// Closed-form approximation (uniform-Hadamard FDN, no damping):
//   T60_seconds ≈ -3 · D_mean / (sr · log10(feedback))
// where D_mean is the mean of FdnReverb::kBaseDelaysSamples at the 48 kHz
// reference rate. The approximation is loose for short T60 (low feedback,
// poor resolution) and tightens as feedback → 1; empirical measurement
// against the post-DSP-6-fix code shows ≤30 % error for feedback ∈ {0.5, 0.7,
// 0.9} when RMS is referenced to the PEAK window (after the FDN first
// "fills" via the early-reflection cluster around D_max), not sample 0.
//
// Without the DSP-6 fix the FDN collapses to a ~14 ms tail regardless of
// feedback — this oracle would fail by ~50× on the pre-fix code.

#include "reverb/FdnReverb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

namespace {

constexpr std::array<int, 16> kBaseDelaysSamples = {
    1499, 1601, 1699, 1801, 1901, 2003, 2099, 2203,
    2311, 2417, 2503, 2609, 2707, 2801, 2903, 3001
};

double window_rms(const float* buf, int start, int win) {
    double sumsq = 0.0;
    for (int i = 0; i < win; ++i) {
        const double s = static_cast<double>(buf[start + i]);
        sumsq += s * s;
    }
    return std::sqrt(sumsq / static_cast<double>(win));
}

// Returns measured T60 in seconds (peak-RMS → -60 dB), or -1.0 if the
// signal never decays 60 dB below the peak within max_samples.
double measure_t60_seconds(spe::reverb::FdnReverb& fdn,
                            double sr,
                            int max_samples) {
    std::vector<float> buf(max_samples, 0.f);
    buf[0] = 1.f;

    constexpr int kBlock = 512;
    for (int n = 0; n < max_samples; n += kBlock) {
        const int chunk = std::min(kBlock, max_samples - n);
        fdn.process(buf.data() + n, chunk);
    }

    constexpr int kWin = 4800;   // 0.1 s @ 48 k — averages over ~2 D_max cycles
    constexpr int kHop = 480;    // 10 ms

    // Find the PEAK RMS window within the first ~0.5 s. This is the
    // early-reflection cluster (all 16 delay lines have first returned by
    // ~D_max / sr ≈ 62 ms); measuring T60 from sample 0 would lock against
    // the pre-reverb silence and never trip the -60 dB threshold.
    const int peak_search_end = std::min(max_samples, 24000);
    int peak_start = 0;
    double peak_rms = 0.0;
    for (int s = 0; s + kWin < peak_search_end; s += kHop) {
        const double rms = window_rms(buf.data(), s, kWin);
        if (rms > peak_rms) { peak_rms = rms; peak_start = s; }
    }
    if (peak_rms < 1e-12) return -1.0;

    const double target_rms = peak_rms * 1e-3;   // -60 dB

    for (int s = peak_start + kHop; s + kWin < max_samples; s += kHop) {
        const double rms = window_rms(buf.data(), s, kWin);
        if (rms < target_rms) {
            return static_cast<double>(s + kWin / 2 - peak_start) / sr;
        }
    }
    return -1.0;
}

} // namespace

int main() {
    const double sr = 48000.0;
    const double D_mean =
        std::accumulate(kBaseDelaysSamples.begin(), kBaseDelaysSamples.end(), 0)
        / static_cast<double>(kBaseDelaysSamples.size());

    struct Case { float feedback; double tolerance_pct; };
    const Case cases[] = {
        { 0.5f, 30.0 },
        { 0.7f, 30.0 },
        { 0.9f, 30.0 },
    };

    int failures = 0;

    for (const auto& c : cases) {
        spe::reverb::FdnReverb fdn;
        fdn.prepareToPlay(sr, 512);
        fdn.setFeedback(c.feedback);
        fdn.setDamping(0.f);    // isolate the delay-line decay; no LP smear
        fdn.setWetMix(1.f);

        const double t60_expected =
            -3.0 * D_mean / (sr * std::log10(static_cast<double>(c.feedback)));

        // Process 6× the expected T60 so the threshold has room to be crossed
        // even when the FDN's effective stretch factor is non-trivial.
        const int max_samples = static_cast<int>(t60_expected * sr * 6.0);

        const double t60_measured = measure_t60_seconds(fdn, sr, max_samples);

        if (t60_measured < 0.0) {
            std::fprintf(stderr,
                "p7_fdn_t60_accuracy FAIL: feedback=%.2f — RMS did not drop "
                "-60 dB within %.2f s (expected ~%.3f s).\n",
                static_cast<double>(c.feedback),
                max_samples / sr, t60_expected);
            ++failures;
            continue;
        }

        const double err_pct =
            std::abs(t60_measured - t60_expected) / t60_expected * 100.0;

        const bool ok = err_pct <= c.tolerance_pct;
        std::fprintf(stderr,
            "p7_fdn_t60_accuracy [%s] feedback=%.2f: measured=%.3f s "
            "expected=%.3f s err=%.1f%% (tol=%.0f%%)\n",
            ok ? "OK" : "FAIL",
            static_cast<double>(c.feedback),
            t60_measured, t60_expected, err_pct, c.tolerance_pct);
        if (!ok) ++failures;
    }

    // Bug-vs-fix discrimination: at feedback=0.5 the post-fix T60 must be
    // >= 100 ms. The pre-DSP-6-fix code gave ~14 ms regardless of feedback.
    spe::reverb::FdnReverb fdn;
    fdn.prepareToPlay(sr, 512);
    fdn.setFeedback(0.5f);
    fdn.setDamping(0.f);
    fdn.setWetMix(1.f);
    const double t60_lower_bound = measure_t60_seconds(fdn, sr, 48000 * 5);
    if (t60_lower_bound > 0.0 && t60_lower_bound < 0.100) {
        std::fprintf(stderr,
            "p7_fdn_t60_accuracy FAIL: feedback=0.5 T60=%.4f s < 100 ms "
            "(pre-DSP-6 regression — readPos must be writePos, not writePos-1)\n",
            t60_lower_bound);
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
