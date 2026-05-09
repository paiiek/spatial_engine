// core/src/ambi/InPhaseDecoder.cpp
// In-phase Ambisonic decoder weights.
//
// Raw kernel (Daniel 2000 §3.30):
//   g_l_raw = (N!)^2 / ((N-l)! * (N+l+1)!)
//
// Normalised (g_0 = 1.0):
//   g_l = g_l_raw * (N+1)
//       = (N+1)! * N! / ((N-l)! * (N+l+1)!)
//
// For small N ≤ 3, factorial values are small enough for exact integer arithmetic.
// References: Daniel 2000 §3.30; SPARTA getInPhaseweights(); Heller et al. 2014 AES.

#include "ambi/InPhaseDecoder.hpp"
#include <cstddef>
#include <cstdint>

namespace spe::ambi {

// Integer factorial (exact for n ≤ 7 used here)
static constexpr uint64_t factorial(int n) noexcept {
    uint64_t r = 1;
    for (int i = 2; i <= n; ++i) r *= static_cast<uint64_t>(i);
    return r;
}

std::array<float, 4> InPhaseDecoder::compute_in_phase_weights_raw(int order) noexcept {
    std::array<float, 4> w{};
    const int N = order;
    const double N_fact_sq = static_cast<double>(factorial(N)) * static_cast<double>(factorial(N));
    for (int l = 0; l <= N; ++l) {
        const double denom = static_cast<double>(factorial(N - l)) *
                             static_cast<double>(factorial(N + l + 1));
        w[static_cast<size_t>(l)] = static_cast<float>(N_fact_sq / denom);
    }
    return w;
}

std::array<float, 4> InPhaseDecoder::compute_in_phase_weights(int order) noexcept {
    auto w = compute_in_phase_weights_raw(order);
    // Normalise: multiply all by (N+1) so g_0 = 1.0
    const float scale = static_cast<float>(order + 1);
    for (int l = 0; l <= order; ++l)
        w[static_cast<size_t>(l)] *= scale;
    return w;
}

} // namespace spe::ambi
