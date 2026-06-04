// core/src/render/ported/RoomEarly.h
//
// Dreamscape Convergence ⑥c — Shoebox early-reflection compute core ported from
// the reference Room Engine.
//
// Provenance: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   Source/RoomEngine.cpp @ commit f2cb796. Byte-faithful port of the
//   first-order image-source geometry (firstOrderImage, RoomEngine.cpp:20-39),
//   the early-reflection width-spread direction (earlySpreadDirection, :52-78),
//   and the per-reflection delay/gain computation (:421-462).
//
// This core is geometry + scalar DSP only — given a source position, room
// half-extents and sample rate it yields, for each of the 6 first-order walls,
// the reflection's unit direction, extra delay (samples vs the direct path) and
// tap gain. The per-object predelay / absorption-EQ / ring-buffer rendering and
// the live SpatialEngine wiring are the follow-on increment ⑥d; this core lets
// the geometry/timing be unit-tested in isolation (mirrors RoomFdn / ⑥a).
//
// Frame-agnostic: pure vector math in one consistent frame (listener at origin).
// juce->std adaptations: jlimit/jmax -> std::clamp/std::max, MathConstants -> literal.

#pragma once

#include "render/ported/SpatialMath.h"

namespace iae {

constexpr int   kNumFirstOrderImages = 6;
constexpr int   kEarlySpreadSamples  = 3;     // reference width-spread sample count
constexpr float kRoomSoundSpeed      = 343.f; // m/s (RoomEngine.cpp:343)

struct RoomEarlyParams {
    Vec3  halfExtents { 6.f, 5.f, 3.f }; // room half-size (m); reference default
    float earlyLateBalance01 = 0.45f;    // early vs late energy split [0,1]
};

struct EarlyReflection {
    Vec3  dir { 1.f, 0.f, 0.f }; // unit direction of the image (listener=origin)
    int   delaySamples = 0;      // extra delay vs the direct path, in [1, ringLen-2]
    float gain = 0.f;            // distance/path attenuation tap gain
    bool  valid = false;
};

// 1st-order image position for wall `wallAxis` 0..5 = +x,-x,+y,-y,+z,-z, with the
// listener at the origin and walls at ±half.{x,y,z}. (RoomEngine.cpp:20-39)
Vec3 firstOrderImage(const Vec3& p, const Vec3& half, int wallAxis) noexcept;

// Unit direction spread within a `widthDeg` cone around `u`: sample `wi` of
// `numWidthSamples` uniform angles. width<0.02 or <2 samples -> u. (:52-78)
Vec3 earlySpreadDirection(Vec3 u, float widthDeg, int wi, int numWidthSamples) noexcept;

// Reference diffuse amounts (RoomEngine.cpp): early non-WFS / WFS objects, and
// the late-field min/max (the WFS-fraction interpolation is a later increment).
constexpr float kErDiffuseNonWfs  = 0.55f;
constexpr float kErDiffuseWfsObj  = 0.87f;
constexpr float kLateDiffuseMin   = 0.64f;
constexpr float kLateDiffuseMax   = 0.93f;

// Blend per-speaker VBAP gains toward a uniform pan (spread energy across the
// array, reduce single-speaker isolation) while preserving the pre-blend RMS
// (anti level-pumping, scale clamped to [0.4,2.5]). All nSpk speakers are treated
// as spatial participants (mmhoa has no Aux/LFE speaker kinds yet — the reference
// participate mask is omitted). diffuse01 in [0,1]; <=1e-5 is a no-op.
// (RoomEngine.cpp:113-165). gainsIO has nSpk entries (nSpk <= kPrototypeChannels).
// Edge case (faithful to ref): an all-zero input (VBAP silence) is replaced by a
// uniform pan u=1/sqrt(nSpk), which injects unit RMS rather than preserving the
// zero — the only case where RMS is not preserved.
void blendVbapWithUniformDiffuse(float* gainsIO, int nSpk, float diffuse01) noexcept;

// Compute the 6 first-order reflections for a source at `objPos` (listener at the
// origin). `ringLen` bounds the delay; gains derive from the early/late balance.
// Returns the number of VALID reflections (a wall whose image collapses to the
// origin is skipped); out[0..kNumFirstOrderImages) is always written.
int computeFirstOrderReflections(const Vec3& objPos, const RoomEarlyParams& p,
                                 double sampleRate, int ringLen,
                                 EarlyReflection out[kNumFirstOrderImages]) noexcept;

} // namespace iae
