// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/Vbap.h . Direct port authorized (convergence D3).
// Adaptations: include paths -> "render/ported/"; namespace iae preserved;
//   kPrototypeChannels defined locally in this module (was SpatialSessionState.h).
// Do not hand-edit logic; re-sync from upstream if the reference changes.
// ============================================================================
#pragma once

#include "render/ported/SpatialMath.h"

#include <cstddef>

namespace iae
{

/** MDAP(spread): 명목 방향 주변에 균등 샘플링해 각각 VBAP을 돌린 뒤 게인을 합산·정규화할 때의 방향 개수. */
constexpr int kMdapDefaultSpreadSegments = 8;

/**
 * 수평면 2기저 VBAP (Pulkki 1997 §1).
 *
 * 2D 레이아웃에서의 **유효 스피커 쌍**은 방위각으로 정렬했을 때 연속한 스피커들 사이의 쌍뿐이다.
 * Politis/Pulkki MATLAB 참고 구현의 findLsPairs.m 과 동일한 전제
 * (https://github.com/polarch/Vector-Base-Amplitude-Panning ).
 * invertLsMtx + vbap 의 vbap2 단계는 각 쌍에 대해 g = L^{-1} u 를 시도하고,
 * 음수 성분이 거의 없으면 Σg² 정규화된 게인을 쓴다.
 *
 * 여기서는 소스 방향이 끼는 호 하나만 골라 그 양끝 스피커로 VBAP을 푼다 (동일 기하에서 한 쌍만 활성).
 *
 * @param participateInVbap i번 스피커를 삼각/쌍 후보에 넣을지. nullptr이면 전부 참.
 */
void computeHorizontalVbap(const Vec3* speakers,
                             const bool* participateInVbap,
                             size_t numSpeakers,
                             Vec3 sourceDirectionUnit,
                             float* gainsOut) noexcept;

/**
 * 3D VBAP (Pulkki 1997 §2).
 * - 참가 스피커 삼중항 전수 탐색 후, 음수 게인 없는 해 중 §1.4처럼 min(g0,g1,g2) 가 최대인 삼중항 선택.
 * - 삼중항 실패 시 모든 쌍에 대해 2기저 VBAP, 그다음 최근접 1채널.
 * @param participateInVbap 서브우퍼·Aux 등 비공간 채널은 false로 제외.
 */
void computeSpatialVbap(const Vec3* speakerDirectionsUnit,
                        const bool* participateInVbap,
                        size_t numSpeakers,
                        Vec3 sourceDirectionUnit,
                        float* gainsOut) noexcept;

/**
 * 수평 MDAP (다중 방향 진폭 패닝): 수평면에서 명목 방향을 중심으로 spreadDeg(전체 각폭, 도) 범위를
 * kMdapDefaultSpreadSegments 방향으로 나눠 각각 computeHorizontalVbap 후 게인을 합산하고 Σg²=1로 맞춘다.
 * spreadDeg≈0 이면 computeHorizontalVbap와 동등.
 */
void computeHorizontalMdap(const Vec3* speakers,
                           const bool* participateInVbap,
                           size_t numSpeakers,
                           Vec3 sourceDirectionUnit,
                           float spreadDeg,
                           float* gainsOut) noexcept;

/**
 * 3D MDAP: 명목 방향 축에 수직인 원뿔에서 동일 세그먼트 수만큼 보조 방향을 두고 각각 computeSpatialVbap 후 합산·정규화.
 */
void computeSpatialMdap(const Vec3* speakerDirectionsUnit,
                        const bool* participateInVbap,
                        size_t numSpeakers,
                        Vec3 sourceDirectionUnit,
                        float spreadDeg,
                        float* gainsOut) noexcept;

} // namespace iae