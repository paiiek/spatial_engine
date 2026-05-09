// core/src/ambi/EPADDecoder.hpp
// Energy-Preserving Ambisonic Decoding (EPAD).
//
// Algorithm: SVD of encoding matrix E via two-sided Jacobi rotation,
// then rescale singular values to enforce uniform energy across directions.
// Reference: Zotter & Frank 2012 ICSA "All-Round Ambisonic Panning and Decoding";
//            Politis 2018 thesis (SPARTA getEPADdecoder implementation).
//
// Implementation details (M2HOA-Q11, Critic A7):
//   - Jacobi eigendecomposition on min(E E^T [S×S], E^T E [K×K]) — smaller form.
//   - Sweep cap: 100 iterations, convergence tolerance: 1e-12 * trace(A).
//   - If cond(A) > 1e10 OR non-convergence: fall back to Tikhonov PINV.
//   - Latency budget: ≤ 30 ms for 16-speaker layout.
//
// No heap allocation in decode() path. All computation in prepare() only.

#pragma once
#include "geometry/SpeakerLayout.h"
#include <vector>

namespace spe::ambi {

class EPADDecoder {
public:
    // Returns S×K decode matrix (row-major: M[s*K+k]).
    // Falls back to Tikhonov PINV on numerical instability.
    static std::vector<float> build_epad_matrix(int order,
                                                 const geometry::SpeakerLayout& layout);
};

} // namespace spe::ambi
