// core/src/ambi/AllRADTDesigns.hpp
// Spherical t-design coordinate tables for AllRAD virtual loudspeakers.
// Source: Hardin & Sloane public-domain spherical t-designs.
// License: Public domain (no copyright claimed by Hardin & Sloane).
// Verified against IEM AllRADecoder source (same table set used industry-wide).

#pragma once
#include <cstddef>

namespace spe::ambi {

struct TDesignPoint {
    float x, y, z;  // unit-sphere Cartesian coordinates
};

// T-design tables: n=24, n=100, n=5200
// Sizes verified by static_assert in AllRADTDesigns.cpp (AC Appendix B2).
extern const TDesignPoint kTDesign24[24];
extern const TDesignPoint kTDesign100[100];
extern const TDesignPoint kTDesign5200[5200];

// Returns the table and its size for a given n_virtual request.
// Supported: 24, 100, 5200. Falls back to 24 for unknown values.
const TDesignPoint* getTDesign(int n_virtual, int& out_size);

} // namespace spe::ambi
