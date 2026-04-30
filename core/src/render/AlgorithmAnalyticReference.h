// core/src/render/AlgorithmAnalyticReference.h
// Closed-form analytic gain baselines — independent of engine code.
// Pulkki-1997 VBAP (2D pair / 3D triangle) and Lossius-2009 DBAP.
// Used only by P9 accuracy harness and P3 unit tests.

#pragma once
#include "geometry/SpeakerLayout.h"
#include <array>
#include <cmath>
#include <vector>

namespace spe::render {

class AlgorithmAnalyticReference {
public:
    // --- VBAP (Pulkki 1997) -----------------------------------------------
    // 2D: find the loudspeaker pair that spans the source azimuth.
    // Returns gains indexed by speaker position in `layout.speakers`.
    // Gains are energy-normalised (sum-of-squares = 1).
    static std::vector<float> vbap_gain(
        const geometry::SpeakerLayout& layout,
        float az_rad, float el_rad = 0.f);

    // --- DBAP (Lossius 2009) -----------------------------------------------
    // g_i = (1/d_i^a) / sqrt(Σ 1/d_i^(2a))
    // Source position in Cartesian metres (same frame as layout speakers).
    static std::vector<float> dbap_gain(
        const geometry::SpeakerLayout& layout,
        float src_x, float src_y, float src_z,
        float rolloff_a = 2.0f);

private:
    // Azimuth of a speaker in radians (atan2(x, z) in our frame).
    static float speaker_az(const geometry::Speaker& s) noexcept {
        return std::atan2(s.x, s.z);
    }
};

} // namespace spe::render
