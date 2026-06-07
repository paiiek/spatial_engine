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
    //
    // NOT RT-SAFE — allocates std::vector. Used by tests + non-RT callers.
    // RT code (VBAPRenderer audio-thread cold-miss) MUST use the scratch
    // overload below instead.
    static std::vector<float> vbap_gain(
        const geometry::SpeakerLayout& layout,
        float az_rad, float el_rad = 0.f);

    // v0.8 P1.3 (DSP-3) — RT-SAFE overload. Writes gains into the
    // caller-provided fixed scratch buffer `out` (capacity ≥ num_speakers).
    // Zero allocation. Returns the number of speakers written (== layout
    // size on success, 0 on capacity violation). Used by VBAPRenderer to
    // remove the std::vector temporaries from the audio-thread cold-miss
    // path; the pre-P1.3 vector-returning vbap_gain() allocated inside
    // SPE_RT_NO_ALLOC_SCOPE().
    static int vbap_gain_into(
        const geometry::SpeakerLayout& layout,
        float az_rad, float el_rad,
        float* out, int out_capacity) noexcept;

    // --- MDAP (Multiple-Direction Amplitude Panning) ----------------------
    // Source spread/width: sample K=8 directions around the nominal (az,el)
    // and sum their VBAP gains, then energy-normalise (Σg²=1). 2D layouts
    // (max|y| < 1e-3) sample an azimuth arc; 3D layouts sample a cone of
    // half-angle spread/2 about the nominal direction (Pulkki MDAP). Each
    // sample reuses vbap_gain_into, so the 5-tier elevation participation
    // mask and the 2D/3D dispatch apply per sample. spread_deg is clamped to
    // [0, 40] (kMdapSpreadMaxDegrees); spread ≈ 0 degenerates to a point
    // source (== vbap_gain_into). RT-SAFE: stack scratch, zero allocation.
    // Returns N on success, 0 on capacity violation.
    static int vbap_mdap_gain_into(
        const geometry::SpeakerLayout& layout,
        float az_rad, float el_rad, float spread_deg,
        float* out, int out_capacity) noexcept;

    // --- DBAP (Lossius 2009) -----------------------------------------------
    // g_i = (1/d_i^a) / sqrt(Σ 1/d_i^(2a))
    // Source position in Cartesian metres (same frame as layout speakers).
    static std::vector<float> dbap_gain(
        const geometry::SpeakerLayout& layout,
        float src_x, float src_y, float src_z,
        float rolloff_a = 2.0f);

    // RT-SAFE overload (v1.0 Phase 1.3): writes DBAP gains into the caller's
    // fixed buffer `out` (capacity >= num_speakers). Zero allocation — the
    // per-speaker weights are folded into `out` itself, then normalised in
    // place, so no std::vector temporaries (unlike dbap_gain() above, which
    // allocated two vectors per call on the audio thread). Returns N on
    // success, 0 if the layout is empty or out_capacity < N. Mirrors
    // vbap_gain_into. Bit-identical results to dbap_gain().
    static int dbap_gain_into(
        const geometry::SpeakerLayout& layout,
        float src_x, float src_y, float src_z,
        float rolloff_a,
        float* out, int out_capacity) noexcept;

private:
    // Azimuth of a speaker in radians (atan2(x, z) in our frame).
    static float speaker_az(const geometry::Speaker& s) noexcept {
        return std::atan2(s.x, s.z);
    }
};

} // namespace spe::render
