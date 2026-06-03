// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/Wfs.cpp . Direct port authorized (convergence D3).
// Adaptations (mechanical, not logic):
//   - Include "render/ported/Wfs.h".
//   - Dropped #include "SpatialSessionState.h": its only use here was the
//     kPrototypeChannels constant, which now comes from render/ported/SpatialMath.h
//     (pulled in via Wfs.h). No logic change.
// Frame-agnostic: gains/delays are pure dot products + Euclidean lengths over
// vectors all expressed in ONE consistent frame, so callers may feed any
// consistent frame (mmhoa native x=right/y=up/z=front is fine — no Y<->Z swap).
// Do not hand-edit logic; re-sync from upstream if the reference changes.
// ============================================================================
#include "render/ported/Wfs.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace iae
{
namespace
{
    constexpr float kEps = 1.0e-8f;
    constexpr float kMinPathMeters = 0.05f;

    [[nodiscard]] bool participates(SpeakerKind k) noexcept
    {
        return k == SpeakerKind::Frontal || k == SpeakerKind::Surround || k == SpeakerKind::Height;
    }

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

    [[nodiscard]] inline float lerp3(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }

    /** Plane wave 가 아닐 때만: 방향 유지, 거리만 파면 곡률에 따라 보정. */
    [[nodiscard]] Vec3 effectiveVirtualSourceForWavefront(Vec3 virtualSourcePositionMeters,
                                                          float wavefront0to200,
                                                          bool planeWave) noexcept
    {
        if (planeWave)
            return virtualSourcePositionMeters;

        const float wf = std::clamp(wavefront0to200, 0.f, 200.f);
        const float d = length(virtualSourcePositionMeters);
        if (d < kMinPathMeters)
            return virtualSourcePositionMeters;

        const Vec3 u = normalized(virtualSourcePositionMeters);
        float dEff = d;
        if (wf <= 100.f)
        {
            const float t = 1.f - (wf / 100.f);
            const float dPlane = std::max(120.f, d * 60.f);
            dEff = d + (dPlane - d) * t;
        }
        else
        {
            const float t = (wf - 100.f) / 100.f;
            dEff = d * (1.f - 0.88f * t);
            dEff = std::max(kMinPathMeters, dEff);
        }
        return Vec3 { u.x * dEff, u.y * dEff, u.z * dEff };
    }

    /** UI 0–200 → 지연 상대 배율. 100 → 1.f (기본 WFS). */
    [[nodiscard]] inline float delayMultiplierFromUi200(float ui0to200) noexcept
    {
        const float u = std::clamp(ui0to200, 0.f, 200.f);
        if (u <= 100.f)
            return lerp3(0.05f, 1.f, u / 100.f);
        return lerp3(1.f, 4.f, (u - 100.f) / 100.f);
    }

    /** UI 0–200 → 게인 지수. 100 → 1.f (기본 WFS). */
    [[nodiscard]] inline float gainExponentFromUi200(float ui0to200) noexcept
    {
        const float u = std::clamp(ui0to200, 0.f, 200.f);
        if (u <= 100.f)
            return lerp3(0.12f, 1.f, u / 100.f);
        return lerp3(1.f, 4.f, (u - 100.f) / 100.f);
    }

    [[nodiscard]] bool speakerDrivesWfs(SpeakerKind k, const bool* layerMask, size_t i) noexcept
    {
        if (!participates(k))
            return false;
        if (layerMask != nullptr && !layerMask[i])
            return false;
        return true;
    }

    void applyDelayShapeScale(float* delaySamplesOut,
                              const SpeakerKind* speakerKinds,
                              const bool* layerMask,
                              size_t numSpeakers,
                              float multiplier) noexcept
    {
        const float s = std::clamp(multiplier, 0.02f, 8.f);
        if (std::abs(s - 1.f) < 1.0e-6f)
            return;
        for (size_t i = 0; i < numSpeakers; ++i)
        {
            if (!speakerDrivesWfs(speakerKinds[i], layerMask, i))
                continue;
            delaySamplesOut[i] *= s;
        }
    }

    void applyGainShapeScale(float* gainsScratch,
                             const SpeakerKind* speakerKinds,
                             const bool* layerMask,
                             size_t numSpeakers,
                             float exponent) noexcept
    {
        const float e = std::clamp(exponent, 0.05f, 6.f);
        if (std::abs(e - 1.f) < 1.0e-6f)
            return;
        for (size_t i = 0; i < numSpeakers; ++i)
        {
            if (!speakerDrivesWfs(speakerKinds[i], layerMask, i))
                continue;
            gainsScratch[i] = std::pow(std::max(gainsScratch[i], 1.0e-12f), e);
        }
    }
} // namespace

void computeWavefieldSynthesisDriving(const Vec3* speakerPositionsMeters,
                                      const Vec3* speakerForwardWorldUnit,
                                      const SpeakerKind* speakerKinds,
                                      size_t numSpeakers,
                                      Vec3 virtualSourcePositionMeters,
                                      float sampleRateHz,
                                      WfsDelayReferenceMode delayReference,
                                      const WfsDrivingParams& params,
                                      bool planeWave,
                                      const bool* speakerWfsLayerMask,
                                      float* gainsOut,
                                      float* delaySamplesOut) noexcept
{
    if (speakerPositionsMeters == nullptr || speakerForwardWorldUnit == nullptr || speakerKinds == nullptr
        || gainsOut == nullptr || delaySamplesOut == nullptr || numSpeakers == 0)
        return;

    const float c = std::max(200.f, params.speedOfSound);
    const float sr = std::max(8000.f, sampleRateHz);
    const float expo = std::clamp(params.amplitudeDistanceExponent, 0.25f, 3.f);
    const float radBlend = std::clamp(params.obliquityRadialBlend, 0.f, 1.f);

    for (size_t i = 0; i < numSpeakers; ++i)
    {
        gainsOut[i] = 0.f;
        delaySamplesOut[i] = 0.f;
    }

    if (numSpeakers > (size_t) kPrototypeChannels)
        return;

    if (numSpeakers == 1 && speakerDrivesWfs(speakerKinds[0], speakerWfsLayerMask, 0))
    {
        gainsOut[0] = 1.f;
        delaySamplesOut[0] = 0.f;
        return;
    }

    const Vec3 virtEff = effectiveVirtualSourceForWavefront(virtualSourcePositionMeters,
                                                           params.wavefrontCurvature,
                                                           planeWave);

    const float distVirt = length(virtualSourcePositionMeters);
    if (planeWave && distVirt >= kMinPathMeters && numSpeakers > 1)
    {
        const Vec3 uSrc = normalized(virtualSourcePositionMeters);
        std::array<float, kPrototypeChannels> rawPw {};
        float minRaw = 1.0e15f;
        bool anyPart = false;

        for (size_t i = 0; i < numSpeakers; ++i)
        {
            if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
            {
                rawPw[i] = 0.f;
                continue;
            }
            anyPart = true;
            rawPw[i] = (float) (((double) dot(uSrc, speakerPositionsMeters[i])) * (double) sr / (double) c);
            minRaw = std::min(minRaw, rawPw[i]);
        }

        if (!anyPart || minRaw > 1.0e14f)
            return;

        for (size_t i = 0; i < numSpeakers; ++i)
        {
            if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
            {
                delaySamplesOut[i] = 0.f;
                continue;
            }
            delaySamplesOut[i] = rawPw[i] - minRaw;
        }

        std::array<float, kPrototypeChannels> scratch {};
        float energy = 0.f;
        for (size_t i = 0; i < numSpeakers; ++i)
        {
            if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
            {
                scratch[i] = 0.f;
                continue;
            }

            const Vec3 nOrientIn = normalized(speakerForwardWorldUnit[i]);
            const float cosOrient =
                dot(Vec3 { -nOrientIn.x, -nOrientIn.y, -nOrientIn.z }, uSrc);

            float cosRad = cosOrient;
            const float lenSp = length(speakerPositionsMeters[i]);
            if (lenSp >= kMinPathMeters)
            {
                const Vec3 nFromOrigin = Vec3 { speakerPositionsMeters[i].x / lenSp,
                                              speakerPositionsMeters[i].y / lenSp,
                                              speakerPositionsMeters[i].z / lenSp };
                cosRad = dot(nFromOrigin, uSrc);
            }

            const float ob =
                lerp3(std::max(0.f, cosOrient), std::max(0.f, cosRad), radBlend);
            scratch[i] = ob;
            energy += scratch[i] * scratch[i];
        }

        if (energy < kEps)
        {
            energy = 0.f;
            for (size_t i = 0; i < numSpeakers; ++i)
            {
                if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
                {
                    scratch[i] = 0.f;
                    continue;
                }
                scratch[i] = 1.f;
                energy += scratch[i] * scratch[i];
            }
        }

        applyGainShapeScale(scratch.data(),
                            speakerKinds,
                            speakerWfsLayerMask,
                            numSpeakers,
                            gainExponentFromUi200(params.gainShapeScale));

        for (size_t i = 0; i < numSpeakers; ++i)
            gainsOut[i] = scratch[i];

        normalizeSumSquares(gainsOut, numSpeakers);
        applyDelayShapeScale(delaySamplesOut,
                             speakerKinds,
                             speakerWfsLayerMask,
                             numSpeakers,
                             delayMultiplierFromUi200(params.delayShapeScale));
        return;
    }

    std::array<float, kPrototypeChannels> scratch {};
    std::array<float, kPrototypeChannels> dList {};
    std::array<float, kPrototypeChannels> rawDelay {};
    float minD = 1.0e12f;

    for (size_t i = 0; i < numSpeakers; ++i)
    {
        if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
            continue;

        const Vec3 r = virtEff - speakerPositionsMeters[i];
        const float d = length(r);
        dList[i] = d;
        if (d < minD)
            minD = d;
    }

    if (minD > 1.0e11f)
        return;

    const float Dlv = length(virtEff);

    for (size_t i = 0; i < numSpeakers; ++i)
    {
        if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
        {
            rawDelay[i] = 0.f;
            continue;
        }

        switch (delayReference)
        {
        case WfsDelayReferenceMode::MinimumToNearestSecondary:
            rawDelay[i] = (float) (((double) dList[i] - (double) minD) * (double) sr / (double) c);
            break;
        case WfsDelayReferenceMode::ListenerVirtualPath:
        default:
            rawDelay[i] = (float) (((double) dList[i] - (double) Dlv) * (double) sr / (double) c);
            break;
        }
    }

    float minRaw = 1.0e15f;
    for (size_t i = 0; i < numSpeakers; ++i)
    {
        if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
            continue;
        minRaw = std::min(minRaw, rawDelay[i]);
    }

    for (size_t i = 0; i < numSpeakers; ++i)
    {
        if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
        {
            delaySamplesOut[i] = 0.f;
            continue;
        }
        delaySamplesOut[i] = rawDelay[i] - minRaw;
    }

    applyDelayShapeScale(delaySamplesOut,
                         speakerKinds,
                         speakerWfsLayerMask,
                         numSpeakers,
                         delayMultiplierFromUi200(params.delayShapeScale));

    float energy = 0.f;
    for (size_t i = 0; i < numSpeakers; ++i)
    {
        if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
        {
            scratch[i] = 0.f;
            continue;
        }

        const Vec3 r = virtEff - speakerPositionsMeters[i];
        const float dPath = std::max(kMinPathMeters, length(r));
        const Vec3 u = normalized(r);

        /** 오리엔테이션은 리스너 쪽을 향하도록 저장된 경우가 많아, 복합재 방사에 맞게 반전해
         *  소스 방향(u)과 같은 편을 가산한다(배열 바깥 전방 소스에서 근접 스피커 게인 소실 방지). */
        const Vec3 nOrientIn = normalized(speakerForwardWorldUnit[i]);
        const float cosOrient = dot(Vec3 { -nOrientIn.x, -nOrientIn.y, -nOrientIn.z }, u);

        /** 원점에서 스피커 위치로의 방향(배열 바깥 레일 상에서는 바깥 편과 소스 방향 정렬). */
        float cosRad = cosOrient;
        const float lenSp = length(speakerPositionsMeters[i]);
        if (lenSp >= kMinPathMeters)
        {
            const Vec3 nFromOrigin = Vec3 { speakerPositionsMeters[i].x / lenSp,
                                            speakerPositionsMeters[i].y / lenSp,
                                            speakerPositionsMeters[i].z / lenSp };
            cosRad = dot(nFromOrigin, u);
        }

        const float ob =
            lerp3(std::max(0.f, cosOrient), std::max(0.f, cosRad), radBlend);

        const float att = 1.f / std::pow(dPath, expo);
        scratch[i] = ob * att;
        energy += scratch[i] * scratch[i];
    }

    if (energy < kEps)
    {
        energy = 0.f;
        for (size_t i = 0; i < numSpeakers; ++i)
        {
            if (!speakerDrivesWfs(speakerKinds[i], speakerWfsLayerMask, i))
            {
                scratch[i] = 0.f;
                continue;
            }
            const float dPath = std::max(kMinPathMeters, dList[i]);
            scratch[i] = 1.f / std::pow(dPath, expo);
            energy += scratch[i] * scratch[i];
        }
    }

    applyGainShapeScale(scratch.data(),
                        speakerKinds,
                        speakerWfsLayerMask,
                        numSpeakers,
                        gainExponentFromUi200(params.gainShapeScale));

    for (size_t i = 0; i < numSpeakers; ++i)
        gainsOut[i] = scratch[i];

    normalizeSumSquares(gainsOut, numSpeakers);
}

} // namespace iae
