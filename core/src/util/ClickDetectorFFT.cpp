// core/src/util/ClickDetectorFFT.cpp
// FFT-based spectral spike detector.
// Active only when SPE_RT_ASSERTS=1.

#include "util/ClickDetectorFFT.h"

#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace spe::util {

// Simple DFT for short blocks (power-of-2 sizes up to 512).
// Not optimized — test-only code path.
static void naive_dft(const float* in, int N, std::vector<float>& mag_db) {
    mag_db.resize(N / 2 + 1);
    const float pi2 = 2.f * 3.14159265358979f;
    for (int k = 0; k <= N / 2; ++k) {
        float re = 0.f, im = 0.f;
        for (int n = 0; n < N; ++n) {
            float angle = pi2 * k * n / static_cast<float>(N);
            re += in[n] * std::cos(angle);
            im -= in[n] * std::sin(angle);
        }
        float mag = std::sqrt(re * re + im * im) / static_cast<float>(N);
        mag_db[k] = (mag > 1e-10f) ? 20.f * std::log10(mag) : -200.f;
    }
}

ClickDetectorReport detect_click(std::span<const float> mono_block,
                                  float threshold_db)
{
    const int N = static_cast<int>(mono_block.size());
    if (N < 4) return {};

    std::vector<float> mag_db;
    // Apply a simple rectangular window (test-only, no Hann needed for spike detection)
    std::vector<float> buf(mono_block.begin(), mono_block.end());
    naive_dft(buf.data(), N, mag_db);

    const int M = static_cast<int>(mag_db.size());
    ClickDetectorReport report;

    for (int k = 1; k < M - 1; ++k) {
        float neighbors_avg = (mag_db[k-1] + mag_db[k+1]) * 0.5f;
        float above = mag_db[k] - neighbors_avg;
        if (above > threshold_db) {
            if (!report.spike_detected || above > report.peak_db_above_neighbors) {
                report.spike_detected        = true;
                report.spike_bin             = k;
                report.peak_db_above_neighbors = above;
            }
        }
    }
    return report;
}

} // namespace spe::util

#endif // SPE_RT_ASSERTS
