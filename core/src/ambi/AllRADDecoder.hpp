// core/src/ambi/AllRADDecoder.hpp
// All-Round Ambisonic Decoding (AllRAD) via virtual loudspeaker VBAP composition.
//
// Algorithm (Zotter & Frank 2012, IEM AllRADecoder):
//   1. Load spherical t-design virtual loudspeaker positions.
//   2. Build pinv decode matrix D_virt from virtual loudspeakers onto SH basis.
//   3. For each virtual loudspeaker, run VBAP (nearest-triplet) onto real layout.
//   4. Compose: D_AllRAD = G_VBAP * D_virt  (real speakers × SH channels).
//
// T-design tables: Hardin & Sloane public-domain sets, n_virtual ∈ {24, 100, 5200}.
// Default n_virtual = 5200 (3rd-order de-facto standard, ~125 kB .rodata).
// Build knob: SPATIAL_ENGINE_ALLRAD_TDESIGN_SIZE (default 5200).
//
// Reference: Zotter & Frank 2012 ICSA; IEM AllRADecoder paper.

#pragma once
#include "geometry/SpeakerLayout.h"
#include <vector>

namespace spe::ambi {

class AllRADDecoder {
public:
    // Returns S×K decode matrix (row-major: M[s*K+k]).
    // n_virtual: number of virtual loudspeakers from t-design table (24, 100, or 5200).
    static std::vector<float> build_allrad_matrix(int order,
                                                   const geometry::SpeakerLayout& layout,
                                                   int n_virtual = 5200);
};

} // namespace spe::ambi
