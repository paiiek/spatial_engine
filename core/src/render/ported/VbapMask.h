// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/AudioEngine.cpp (fillVbapMaskForObject + helpers).
//   Direct port authorized (convergence D3).
// Adaptations (mechanical, not logic):
//   - Lifted out of AudioEngine.cpp into a standalone juce-free module.
//   - Signature takes raw arrays instead of SpatialAudioPull/SpatialSessionState:
//       const Vec3* speakerPositionsCartesian, int numSpeakers, const bool*
//       spatialGroupMask (nullptr => every speaker is in the spatial group).
//   - juce::MathConstants<float>::pi -> literal.
//   - kPrototypeChannels comes from render/ported/SpatialMath.h (this tree).
// Frame: ported (x=right, y=front, z=up). z is the elevation axis. Callers in
//   the mmhoa tree convert via coords::mmhoa_to_ported (Y<->Z swap) at the
//   boundary so this byte-faithful logic is preserved.
// Do not hand-edit logic; re-sync from upstream if the reference changes.
// ============================================================================
#pragma once

#include "render/ported/SpatialMath.h"

namespace iae
{

// --- 5-tier elevation layering tunables (verbatim from the reference) -------
// Object/speaker "horizontal layer" |z| (metres) tolerance. A z≈0 (flat)
// object distributes gain only to speakers on this layer.
constexpr float kVbapObjectCartesianZEpsMeters = 1.0e-4f;
constexpr float kVbapObjectElevationDegEps = 0.02f;

// Elevated source: minimum dot(speakerDir, sourceDir) per relaxation tier
// (cos of the half-angle). Progressively widened until >=3 candidates remain.
constexpr float kVbapElevAngStrictDeg = 52.f;
constexpr float kVbapElevAngMidDeg = 62.f;
constexpr float kVbapElevAngLooseDeg = 74.f;
constexpr float kVbapElevAngWideDeg = 86.f;

// When |sourceDir.z| is large (near ceiling/floor) drop the opposite layer's
// speakers from the VBAP candidate set.
constexpr float kVbapSteepSourceUz = 0.70f;
constexpr float kVbapSteepOppositeLayerZM = 0.22f;

/** |z| <= eps speaker is on the horizontal VBAP layer. */
[[nodiscard]] bool speakerOnHorizontalVbapLayer(const Vec3& speakerPosCartesianM) noexcept;

/** Count of true entries in the first numSpeakers slots of mask. */
[[nodiscard]] int countVbapMaskTrue(const bool* mask, int numSpeakers) noexcept;

/**
 * Build the per-object VBAP participation mask with 5-tier elevation layering.
 *
 * Flat object (objectFlat): only horizontal-layer (z≈0) speakers in the spatial
 *   group participate.
 * Elevated object: keep speakers angularly near the source direction (52° tier),
 *   and — if the source is steeply up/down — drop the opposite layer. If fewer
 *   than 3 candidates survive, relax through 62°/74°/86° and finally the whole
 *   spatial group.
 *
 * @param speakerPositionsCartesian  ported-frame speaker positions (z = up).
 * @param numSpeakers                speaker count (<= kPrototypeChannels).
 * @param spatialGroupMask           per-speaker spatial-group membership; nullptr
 *                                   => all speakers are in the spatial group.
 * @param objectFlat                 true when |elevation| <= kVbapObjectElevationDegEps.
 * @param objectDirUnit              ported-frame source unit direction.
 * @param maskOut                    output participation mask (size kPrototypeChannels).
 */
void fillVbapMaskForObject(const Vec3* speakerPositionsCartesian,
                           int numSpeakers,
                           const bool* spatialGroupMask,
                           bool objectFlat,
                           const Vec3& objectDirUnit,
                           bool* maskOut) noexcept;

} // namespace iae
