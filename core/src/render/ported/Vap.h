// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/Vap.h . Direct port authorized (convergence D3).
// Adaptations: include paths -> "render/ported/"; namespace iae preserved;
//   kPrototypeChannels defined locally in this module (was SpatialSessionState.h).
// Do not hand-edit logic; re-sync from upstream if the reference changes.
// ============================================================================
#pragma once

#include "render/ported/SpatialMath.h"

#include <cstddef>

namespace iae
{

/**
 * Volumetric Amplitude Panning (Kareer & Sunder, AES 153rd Convention paper 공개 요지).
 * 리스너는 roomCenter, 스피커·오브젝트는 동일 좌표계(m).
 *
 * - 벽 위(원점에서 반경 wallRadius 부근): 방향은 normalize(sourcePosition)로 VBAP과 동일 계열.
 * - 벽 안쪽: 같은 방향 VBAP 게인과, 소스–스피커 거리 기반 체적 게인을 β=r/R 커브로 혼합 후 Σg²=1 정규화.
 *
 * wallRadiusMeters ≤ 0 이면 활성 스피커들의 |p_i - roomCenter| 평균으로 자동 설정.
 */
/** @param participateInVbap VBAP 삼중항 후보(서브/Aux 제외). nullptr이면 전부 사용. */
void computeVolumetricAmplitudePanning(const iae::Vec3* speakerPositions,
                                       const iae::Vec3* speakerDirectionsUnit,
                                       const bool* participateInVbap,
                                       size_t numSpeakers,
                                       iae::Vec3 objectPositionMeters,
                                       iae::Vec3 roomCenterMeters,
                                       float wallRadiusMeters,
                                       float insideRadialCurvePower,
                                       float volumetricDistanceExponent,
                                       float* gainsOut) noexcept;

} // namespace iae