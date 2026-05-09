// core/src/ambi/InPhaseDecoder.hpp
// In-phase Ambisonic decoder weight computation.
//
// Two-step derivation (Daniel 2000 §3.30, SPARTA getInPhaseweights()):
//   Step 1 (raw): g_l_raw = (N!)^2 / ((N-l)! * (N+l+1)!)
//                 Note: g_0_raw = 1/(N+1) ≠ 1.0
//   Step 2 (normalise): g_l = g_l_raw * (N+1)  →  g_0 = 1.0
//
// Golden vectors (Daniel 2000, tol 1e-5):
//   N=1: {1.0000, 0.3333}
//   N=2: {1.0000, 0.5000, 0.1000}
//   N=3: {1.0000, 0.6000, 0.2000, 0.02857}
//
// Composition: SH-side pre-multiply (M2HOA-Q2 RESOLVED).
// No heap allocation: constexpr / stack arrays only.

#pragma once
#include <array>

namespace spe::ambi {

class InPhaseDecoder {
public:
    // Returns normalised in-phase gains {g_0=1, g_1, ..., g_N} (size 4).
    // Unused slots beyond order N are set to 0.
    static std::array<float, 4> compute_in_phase_weights(int order) noexcept;

    // Raw (un-normalised) weights for AC-S2.7.0 sanity test.
    // g_0_raw = 1/(N+1), should equal 1/(order+1).
    static std::array<float, 4> compute_in_phase_weights_raw(int order) noexcept;
};

} // namespace spe::ambi
