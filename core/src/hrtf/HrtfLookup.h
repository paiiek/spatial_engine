// core/src/hrtf/HrtfLookup.h
// Spherical nearest-neighbor HRTF direction lookup.
//
// v0.5 P2: primary path is KdTree3D::nearest (O(log N) unit-Cartesian).
//          Brute-force trig path remains exposed for parity tests
//          (`nearestPositionBruteForceForTest`).

#pragma once

#include "hrtf/SofaBinReader.h"

namespace spe::hrtf {

class KdTree3D;  // fwd

// Returns index of the nearest position in `table` for (az_deg, el_deg).
// Brute-force great-circle distance (O(N) trig). Retained as ground-truth
// for parity tests against KdTree3D; production callers should use
// `lookupHrtf` or query a KdTree3D directly.
int nearestPositionBruteForceForTest(const HrtfTable& table, float az_deg, float el_deg);

// Back-compat shim — same as the brute-force above. Existing call-sites that
// reference `nearestPosition()` continue to compile; v0.5 lookups via
// BinauralMonitor go through the KdTree3D path instead.
int nearestPosition(const HrtfTable& table, float az_deg, float el_deg);

// HRIR pair returned by lookupHrtf.
// Engine convention: az=+90 deg → LEFT (AmbiX).
// SOFA convention:   az=+90 deg → LEFT (AES69) — no sign flip needed.
struct HrtfPair {
    const float* left;
    const float* right;
    int          ir_length;
};

// Lookup the nearest HRIR pair for (az_rad, el_rad) in engine convention.
// Brute-force path (back-compat). For O(log N) lookup, build a KdTree3D
// once and use `lookupHrtfFromTree`.
HrtfPair lookupHrtf(const HrtfTable& table, float az_rad, float el_rad);

// O(log N) HRIR pair lookup via a pre-built KdTree3D.
// `tree` must have been built from `table`. Alloc-free.
HrtfPair lookupHrtfFromTree(const HrtfTable& table,
                            const KdTree3D&  tree,
                            float            az_rad,
                            float            el_rad) noexcept;

} // namespace spe::hrtf
