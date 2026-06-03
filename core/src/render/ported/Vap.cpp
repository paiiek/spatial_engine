// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/Vap.cpp . Direct port authorized (convergence D3).
// Adaptations: include paths -> "render/ported/"; namespace iae preserved;
//   kPrototypeChannels defined locally in this module (was SpatialSessionState.h).
// Do not hand-edit logic; re-sync from upstream if the reference changes.
// ============================================================================
#include "render/ported/SpatialMath.h"  // kPrototypeChannels (was SpatialSessionState.h)
#include "render/ported/Vap.h"
#include "render/ported/Vbap.h"

#include <algorithm>
#include <cmath>

namespace iae
{
namespace
{
    constexpr float kEps = 1.0e-8f;

    inline void normalizeSumSquares(float* g, size_t n) noexcept
    {
        float s = 0.f;
        for (size_t i = 0; i < n; ++i)
            s += g[i] * g[i];
        if (s < kEps)
            return;
        const float scale = std::sqrt(1.f / s);
        for (size_t i = 0; i < n; ++i)
            g[i] *= scale;
    }

    inline float autoWallRadius(const Vec3* speakerPositions,
                                size_t numSpeakers,
                                Vec3 roomCenter) noexcept
    {
        if (numSpeakers == 0)
            return 2.f;
        float sum = 0.f;
        for (size_t i = 0; i < numSpeakers; ++i)
            sum += length(speakerPositions[i] - roomCenter);
        return std::max(0.25f, sum / (float) numSpeakers);
    }
} // namespace

void computeVolumetricAmplitudePanning(const Vec3* speakerPositions,
                                       const Vec3* speakerDirectionsUnit,
                                       const bool* participateInVbap,
                                       size_t numSpeakers,
                                       Vec3 objectPositionMeters,
                                       Vec3 roomCenterMeters,
                                       float wallRadiusMeters,
                                       float insideRadialCurvePower,
                                       float volumetricDistanceExponent,
                                       float* gainsOut) noexcept
{
    if (speakerPositions == nullptr || speakerDirectionsUnit == nullptr || gainsOut == nullptr
        || numSpeakers == 0)
        return;

    for (size_t i = 0; i < numSpeakers; ++i)
        gainsOut[i] = 0.f;

    const Vec3 sRel = objectPositionMeters - roomCenterMeters;
    const float r = length(sRel);

    float R = wallRadiusMeters;
    if (R <= kEps)
        R = autoWallRadius(speakerPositions, numSpeakers, roomCenterMeters);

    const Vec3 dirWall =
        r > kEps ? normalized(sRel) : normalized(speakerDirectionsUnit[0]); // 중앙 근처 시 편의상 첫 스피커 방향

    if (numSpeakers > (size_t) kPrototypeChannels)
        return;

    float vbap[(size_t) kPrototypeChannels];
    float vol[(size_t) kPrototypeChannels];

    for (size_t i = 0; i < numSpeakers; ++i)
        vbap[i] = 0.f;

    computeSpatialVbap(speakerDirectionsUnit, participateInVbap, numSpeakers, dirWall, vbap);
    float volSumSq = 0.f;
    const float expo = std::clamp(volumetricDistanceExponent, 0.25f, 8.f);

    for (size_t i = 0; i < numSpeakers; ++i)
    {
        const float d = length(objectPositionMeters - speakerPositions[i]);
        const float base = 1.f / std::pow(std::max(kEps, d), expo);
        vol[i] = base;
        volSumSq += base * base;
    }

    if (volSumSq >= kEps)
    {
        const float inv = 1.f / std::sqrt(volSumSq);
        for (size_t i = 0; i < numSpeakers; ++i)
            vol[i] *= inv;
    }
    else
    {
        vol[0] = 1.f;
        for (size_t i = 1; i < numSpeakers; ++i)
            vol[i] = 0.f;
    }

    const float rr = std::clamp(r / std::max(kEps, R), 0.f, 1.f);
    const float curvePow = std::clamp(insideRadialCurvePower, 0.15f, 8.f);
    const float beta = std::pow(rr, curvePow); // 벽에 가까울수록 1 → VBAP 비중↑

    for (size_t i = 0; i < numSpeakers; ++i)
        gainsOut[i] = beta * vbap[i] + (1.f - beta) * vol[i];

    normalizeSumSquares(gainsOut, numSpeakers);
}

} // namespace iae