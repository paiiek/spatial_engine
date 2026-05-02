// core/src/hrtf/HrtfLookup.h
// Spherical nearest-neighbor HRTF direction lookup.
// Great-circle distance: d = acos(sin(el1)*sin(el2) + cos(el1)*cos(el2)*cos(az1-az2))

#pragma once

#include "hrtf/SofaBinReader.h"

namespace spe::hrtf {

// Returns index of the nearest position in table for (az_deg, el_deg).
// Uses brute-force great-circle distance — fast enough for <64K positions
// in a non-real-time monitor path.
int nearestPosition(const HrtfTable& table, float az_deg, float el_deg);

// Returns the left/right IR pointers for (az_rad, el_rad) in engine convention.
// Engine convention: az=+90 deg → LEFT (AmbiX).
// SOFA convention:   az=+90 deg → LEFT (AES69) — no sign flip needed.
struct HrtfPair {
    const float* left;
    const float* right;
    int          ir_length;
};
HrtfPair lookupHrtf(const HrtfTable& table, float az_rad, float el_rad);

} // namespace spe::hrtf
