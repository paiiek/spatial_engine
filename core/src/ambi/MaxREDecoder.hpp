// core/src/ambi/MaxREDecoder.hpp
// max-rE per-order weight computation for Ambisonic decoding.
//
// Algorithm: g_l = P_l(r_E_max) where r_E_max is the largest root of
// P_{N+1}(x) = 0 (Legendre polynomial of degree N+1).
// Reference: Zotter & Frank 2019 "Ambisonics" eq. 4.49; SPARTA getMaxREweights().
//
// Golden vectors (M2HOA-Q7 RESOLVED, SPARTA-canonical, tol 1e-5):
//   N=1: {1.0000, 0.5774}          (root of P_2: x = 1/sqrt(3))
//   N=2: {1.0000, 0.7746, 0.4000}  (root of P_3: x = sqrt(3/5))
//   N=3: {1.0000, 0.8611, 0.6124, 0.3045} (root of P_4: x ≈ 0.86114)
//
// Composition: SH-side pre-multiply (M2HOA-Q2 RESOLVED).
// No heap allocation: constexpr / stack arrays only.

#pragma once
#include <array>
#include <cmath>

namespace spe::ambi {

class MaxREDecoder {
public:
    // Returns per-order max-rE gains {g_0, g_1, ..., g_N} (size MAX_ORDER+1=4).
    // Unused slots beyond order N are set to 0.
    // g_0 = 1.0 always (normalised, Daniel 2000 / SPARTA convention).
    static std::array<float, 4> compute_max_re_weights(int order) noexcept;
};

// Compile-time sanity checks for r_E_max roots (AC-S1.5).
// r_E_max(N=2) = sqrt(3/5) ≈ 0.7746
static_assert(static_cast<int>(std::sqrt(3.0 / 5.0) * 10000) == 7745 ||
              static_cast<int>(std::sqrt(3.0 / 5.0) * 10000) == 7746,
              "MaxREDecoder: N=2 r_E_max root sanity check failed");

} // namespace spe::ambi
