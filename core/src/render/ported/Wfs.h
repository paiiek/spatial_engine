// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/Wfs.h . Direct port authorized (convergence D3).
// Adaptations (mechanical, not logic):
//   - Include paths -> "render/ported/".
//   - SpeakerKind / WfsDrivingParams / WfsDelayReferenceMode now live in
//     namespace iae (see those headers); references stay unqualified inside iae.
// Do not hand-edit logic; re-sync from upstream if the reference changes.
// ============================================================================
#pragma once

#include "render/ported/SpatialMath.h"
#include "render/ported/SpeakerKind.h"
#include "render/ported/WfsDrivingParams.h"

#include <cstddef>

namespace iae
{

/**
 * @param speakerWfsLayerMask nullptr이면 공간 그룹 스피커 전부 사용. 비-null이면 true인 인덱스만
 *        WFS 게인·지연에 참여(예: 오브젝트 2D 모드에서 z≈0 레이어만).
 */
void computeWavefieldSynthesisDriving(const iae::Vec3* speakerPositionsMeters,
                                      const iae::Vec3* speakerForwardWorldUnit,
                                      const SpeakerKind* speakerKinds,
                                      size_t numSpeakers,
                                      iae::Vec3 virtualSourcePositionMeters,
                                      float sampleRateHz,
                                      WfsDelayReferenceMode delayReference,
                                      const WfsDrivingParams& params,
                                      bool planeWave,
                                      const bool* speakerWfsLayerMask,
                                      float* gainsOut,
                                      float* delaySamplesOut) noexcept;

} // namespace iae
