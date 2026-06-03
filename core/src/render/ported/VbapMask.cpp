// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/AudioEngine.cpp (fillVbapMaskForObject + helpers).
//   Direct port authorized (convergence D3).
// Adaptations: see VbapMask.h header. Logic is byte-faithful to upstream;
//   only the container types at the boundary changed (std::array<bool,N> /
//   SpatialAudioPull -> raw pointers) and juce::MathConstants -> literal.
// Do not hand-edit logic; re-sync from upstream if the reference changes.
// ============================================================================
#include "render/ported/VbapMask.h"

#include <array>
#include <cmath>

namespace iae
{
namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    [[nodiscard]] inline bool inSpatialGroup(const bool* spatialGroupMask, int i) noexcept
    {
        return spatialGroupMask == nullptr || spatialGroupMask[(size_t) i];
    }

    void buildElevatedObjectVbapMask(const Vec3* speakerPositionsCartesian,
                                     int numSpeakers,
                                     const bool* spatialGroupMask,
                                     const Vec3& objectDirUnit,
                                     float cosMinDot,
                                     bool applySteepOppositeLayerCut,
                                     bool* maskOut) noexcept
    {
        const Vec3 u = normalized(objectDirUnit);
        const bool steepUp = u.z > kVbapSteepSourceUz;
        const bool steepDown = u.z < -kVbapSteepSourceUz;
        const bool noAngular = cosMinDot < -1.f;

        for (int i = 0; i < numSpeakers; ++i)
        {
            maskOut[(size_t) i] = false;
            if (!inSpatialGroup(spatialGroupMask, i))
                continue;

            const Vec3 sp = speakerPositionsCartesian[(size_t) i];
            const float sl = length(sp);
            if (sl < 1.0e-6f)
                continue;

            const Vec3 ls { sp.x / sl, sp.y / sl, sp.z / sl };
            if (!noAngular)
            {
                const float dotp = dot(ls, u);
                if (dotp < cosMinDot)
                    continue;
            }

            if (applySteepOppositeLayerCut)
            {
                if (steepUp && sp.z <= kVbapSteepOppositeLayerZM)
                    continue;
                if (steepDown && sp.z >= kVbapSteepOppositeLayerZM)
                    continue;
            }

            maskOut[(size_t) i] = true;
        }
        for (int i = numSpeakers; i < kPrototypeChannels; ++i)
            maskOut[(size_t) i] = false;
    }
} // namespace

bool speakerOnHorizontalVbapLayer(const Vec3& speakerPosCartesianM) noexcept
{
    return std::abs(speakerPosCartesianM.z) <= kVbapObjectCartesianZEpsMeters;
}

int countVbapMaskTrue(const bool* mask, int numSpeakers) noexcept
{
    int c = 0;
    for (int i = 0; i < numSpeakers; ++i)
        if (mask[(size_t) i])
            ++c;
    return c;
}

void fillVbapMaskForObject(const Vec3* speakerPositionsCartesian,
                           int numSpeakers,
                           const bool* spatialGroupMask,
                           bool objectFlat,
                           const Vec3& objectDirUnit,
                           bool* maskOut) noexcept
{
    if (objectFlat)
    {
        for (int i = 0; i < numSpeakers; ++i)
        {
            const bool inSpatial = inSpatialGroup(spatialGroupMask, i);
            maskOut[(size_t) i] =
                inSpatial && speakerOnHorizontalVbapLayer(speakerPositionsCartesian[(size_t) i]);
        }
        for (int i = numSpeakers; i < kPrototypeChannels; ++i)
            maskOut[(size_t) i] = false;
        return;
    }

    const float rad = kPi / 180.f;
    const float cosStrict = std::cos(kVbapElevAngStrictDeg * rad);
    const float cosMid = std::cos(kVbapElevAngMidDeg * rad);
    const float cosLoose = std::cos(kVbapElevAngLooseDeg * rad);
    const float cosWide = std::cos(kVbapElevAngWideDeg * rad);

    struct Try
    {
        float cosMin;
        bool zCut;
    };
    const Try tries[] = {
        { cosStrict, true },
        { cosMid, true },
        { cosLoose, false },
        { cosWide, false },
        { -2.f, false } // whole spatial group
    };

    std::array<bool, kPrototypeChannels> scratch {};

    for (const Try& t : tries)
    {
        buildElevatedObjectVbapMask(speakerPositionsCartesian,
                                    numSpeakers,
                                    spatialGroupMask,
                                    objectDirUnit,
                                    t.cosMin,
                                    t.zCut,
                                    scratch.data());
        if (countVbapMaskTrue(scratch.data(), numSpeakers) >= 3 || t.cosMin < -1.f)
        {
            for (int i = 0; i < kPrototypeChannels; ++i)
                maskOut[(size_t) i] = scratch[(size_t) i];
            return;
        }
    }

    for (int i = 0; i < kPrototypeChannels; ++i)
        maskOut[(size_t) i] = scratch[(size_t) i];
}

} // namespace iae
