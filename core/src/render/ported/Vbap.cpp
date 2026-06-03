// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/Vbap.cpp . Direct port authorized (convergence D3).
// Adaptations: include paths -> "render/ported/"; namespace iae preserved;
//   kPrototypeChannels defined locally in this module (was SpatialSessionState.h).
// Do not hand-edit logic; re-sync from upstream if the reference changes.
// ============================================================================
#include "render/ported/Vbap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace iae
{
namespace detail
{
    constexpr float kMinDen = 1.0e-8f;
    constexpr float kGainEps = 1.0e-6f;

    [[nodiscard]] inline bool vbapUseIdx(const bool* participate, size_t i) noexcept
    {
        return participate == nullptr || participate[i];
    }

    /** 리스너 원점 기준 수평면 방향 단위벡터 (Pulkki VBAP 기저). XY평면 거리 0이면 실패. */
    [[nodiscard]] inline bool horizontalUnitDirFromPosition(Vec3 posMeters, float& ox, float& oy) noexcept
    {
        const float len = std::sqrt(posMeters.x * posMeters.x + posMeters.y * posMeters.y);
        if (len < kMinDen)
            return false;
        ox = posMeters.x / len;
        oy = posMeters.y / len;
        return true;
    }

    /**
     * V. Pulkki, "Virtual Sound Source Positioning Using Vector Base Amplitude Panning,"
     * J. Audio Eng. Soc., Vol. 45, No. 6, 1997.
     *
     * 2D: Eq. (7) p = g1*l1 + g2*l2, Eq. (9) g = p^T L^{-1} (동치로 열 기저 L=[l1|l2], p=Lg ⇒ g=L^{-1}p).
     *     Eq. (10) 출력 레벨 고정: g_i에 sqrt(C/(g1²+g2²)) 스케일.
     * 3D: Eq. (16)-(18) 동일하게 삼 기저, Eq. (19) g_i에 sqrt(C/(g1²+g2²+g3²)) 스케일.
     * 논문: 음수 게인은 0으로 두고 위 스케일 적용.
     */
    inline void pulkkiNormalize2(float& g0, float& g1, float C = 1.f) noexcept
    {
        const float s = g0 * g0 + g1 * g1;
        if (s < kMinDen)
            return;
        const float scale = std::sqrt(C / s);
        g0 *= scale;
        g1 *= scale;
    }

    inline void pulkkiNormalize3(float& g0, float& g1, float& g2, float C = 1.f) noexcept
    {
        const float s = g0 * g0 + g1 * g1 + g2 * g2;
        if (s < kMinDen)
            return;
        const float scale = std::sqrt(C / s);
        g0 *= scale;
        g1 *= scale;
        g2 *= scale;
    }

    inline bool solveHorizontalTwoSpeakers(float l0x,
                                           float l0y,
                                           float l1x,
                                           float l1y,
                                           float ux,
                                           float uy,
                                           float& g0,
                                           float& g1) noexcept
    {
        const float det = l0x * l1y - l1x * l0y;
        if (std::abs(det) < kMinDen)
            return false;

        g0 = (ux * l1y - uy * l1x) / det;
        g1 = (l0x * uy - l0y * ux) / det;
        return true;
    }

    inline void fallbackNearestHorizontal(const Vec3* speakers,
                                          const bool* participate,
                                          size_t numSpeakers,
                                          Vec3 uHoriz,
                                          float* gainsOut) noexcept
    {
        float best = -1.f;
        size_t bestIdx = 0;
        bool any = false;

        for (size_t i = 0; i < numSpeakers; ++i)
        {
            if (!vbapUseIdx(participate, i))
                continue;
            float lx = 0.f, ly = 0.f;
            if (!horizontalUnitDirFromPosition(speakers[i], lx, ly))
                continue;
            const float d = lx * uHoriz.x + ly * uHoriz.y;
            if (!any || d > best)
            {
                best = d;
                bestIdx = i;
                any = true;
            }
        }

        for (size_t i = 0; i < numSpeakers; ++i)
            gainsOut[i] = 0.f;

        if (any)
            gainsOut[bestIdx] = 1.f;
    }

    /** L = [c0|c1|c2] (열이 단위 스피커 방향). L^{-1} */
    inline bool invertMatrixColumns(Vec3 c0, Vec3 c1, Vec3 c2, float invOut[9]) noexcept
    {
        const float a = c0.x;
        const float b = c1.x;
        const float c = c2.x;
        const float d = c0.y;
        const float e = c1.y;
        const float f = c2.y;
        const float g = c0.z;
        const float h = c1.z;
        const float i = c2.z;

        const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);

        if (std::abs(det) < kMinDen)
            return false;

        const float s = 1.f / det;

        invOut[0] = (e * i - f * h) * s;
        invOut[1] = (c * h - b * i) * s;
        invOut[2] = (b * f - c * e) * s;
        invOut[3] = (f * g - d * i) * s;
        invOut[4] = (a * i - c * g) * s;
        invOut[5] = (c * d - a * f) * s;
        invOut[6] = (d * h - e * g) * s;
        invOut[7] = (b * g - a * h) * s;
        invOut[8] = (a * e - b * d) * s;

        return true;
    }

    inline void multiplyInverseByVector(const float inv[9], Vec3 v, float out[3]) noexcept
    {
        out[0] = inv[0] * v.x + inv[1] * v.y + inv[2] * v.z;
        out[1] = inv[3] * v.x + inv[4] * v.y + inv[5] * v.z;
        out[2] = inv[6] * v.x + inv[7] * v.y + inv[8] * v.z;
    }

    inline bool vbapTwoSpeakers3D(Vec3 l0, Vec3 l1, Vec3 v, float& g0, float& g1) noexcept
    {
        l0 = normalized(l0);
        l1 = normalized(l1);
        v = normalized(v);

        Vec3 n = cross(l0, l1);
        const float nl = length(n);

        if (nl < kMinDen)
            return false;

        n = n * (1.f / nl);

        Vec3 vp = v - n * dot(v, n);
        const float vlen = length(vp);

        if (vlen < kMinDen)
            return false;

        vp = vp * (1.f / vlen);

        const Vec3 e1 = l0;
        const Vec3 e2 = normalized(l1 - e1 * dot(l1, e1));

        const float vx = dot(vp, e1);
        const float vy = dot(vp, e2);
        const float l1x = dot(l1, e1);
        const float l1y = dot(l1, e2);

        if (!solveHorizontalTwoSpeakers(1.f, 0.f, l1x, l1y, vx, vy, g0, g1))
            return false;

        if (g0 < -kGainEps || g1 < -kGainEps)
            return false;

        g0 = std::max(0.f, g0);
        g1 = std::max(0.f, g1);
        pulkkiNormalize2(g0, g1);
        return true;
    }

    inline void fallbackNearest3d(const Vec3* dirs,
                                  const bool* participate,
                                  size_t numSpeakers,
                                  Vec3 u,
                                  float* gainsOut) noexcept
    {
        float best = -1.f;
        size_t bestIdx = 0;
        bool any = false;

        for (size_t i = 0; i < numSpeakers; ++i)
        {
            if (!vbapUseIdx(participate, i))
                continue;
            const float d = dot(dirs[i], u);
            if (!any || d > best)
            {
                best = d;
                bestIdx = i;
                any = true;
            }
        }

        for (size_t i = 0; i < numSpeakers; ++i)
            gainsOut[i] = 0.f;

        if (any)
            gainsOut[bestIdx] = 1.f;
    }

    /** 삼중항이 없을 때: 모든 스피커 쌍에 대해 2채널 VBAP 시도, 실패 시 최근접 1채널. */
    inline void fallbackSparseTwoSpeakerOrNearest(const Vec3* dirs,
                                                  const bool* participate,
                                                  size_t numSpeakers,
                                                  Vec3 u,
                                                  float* gainsOut) noexcept
    {
        bool found = false;
        float bestScore = -1.f;
        size_t bestI0 = 0;
        size_t bestI1 = 0;
        float bestG0 = 0.f;
        float bestG1 = 0.f;

        for (size_t i0 = 0; i0 < numSpeakers; ++i0)
        {
            if (!vbapUseIdx(participate, i0))
                continue;
            for (size_t i1 = i0 + 1; i1 < numSpeakers; ++i1)
            {
                if (!vbapUseIdx(participate, i1))
                    continue;
                float g0 = 0.f;
                float g1 = 0.f;
                if (!vbapTwoSpeakers3D(dirs[i0], dirs[i1], u, g0, g1))
                    continue;

                const float score = std::min(g0, g1);
                if (!found || score > bestScore)
                {
                    found = true;
                    bestScore = score;
                    bestI0 = i0;
                    bestI1 = i1;
                    bestG0 = g0;
                    bestG1 = g1;
                }
            }
        }

        if (found)
        {
            for (size_t i = 0; i < numSpeakers; ++i)
                gainsOut[i] = 0.f;

            gainsOut[bestI0] = bestG0;
            gainsOut[bestI1] = bestG1;
            return;
        }

        fallbackNearest3d(dirs, participate, numSpeakers, u, gainsOut);
    }

    inline void buildTangentBasis(Vec3 u, Vec3& e1, Vec3& e2) noexcept
    {
        u = normalized(u);
        Vec3 a = std::abs(u.x) < 0.9f ? Vec3{ 1.f, 0.f, 0.f } : Vec3{ 0.f, 1.f, 0.f };
        e1 = normalized(cross(a, u));
        e2 = normalized(cross(u, e1));
    }

    inline void normalizeGainVectorL2(float* g, size_t n) noexcept
    {
        float s = 0.f;
        for (size_t i = 0; i < n; ++i)
            s += g[i] * g[i];
        if (s < kMinDen)
            return;
        const float scale = 1.f / std::sqrt(s);
        for (size_t i = 0; i < n; ++i)
            g[i] *= scale;
    }
} // namespace detail

void computeHorizontalVbap(const Vec3* speakers,
                           const bool* participateInVbap,
                           size_t numSpeakers,
                           Vec3 sourceDirectionUnit,
                           float* gainsOut) noexcept
{
    using namespace detail;

    if (speakers == nullptr || gainsOut == nullptr || numSpeakers == 0)
        return;

    for (size_t i = 0; i < numSpeakers; ++i)
        gainsOut[i] = 0.f;

    Vec3 u = normalized(sourceDirectionUnit);
    const float horizLen = std::sqrt(u.x * u.x + u.y * u.y);
    Vec3 uHoriz =
        horizLen > kMinDen ? Vec3{ u.x / horizLen, u.y / horizLen, 0.f } : Vec3{ 1.f, 0.f, 0.f };

    if (numSpeakers == 1)
    {
        if (vbapUseIdx(participateInVbap, 0))
            gainsOut[0] = 1.f;
        return;
    }

    if (numSpeakers == 2)
    {
        if (!vbapUseIdx(participateInVbap, 0) && !vbapUseIdx(participateInVbap, 1))
            return;
        if (!vbapUseIdx(participateInVbap, 0))
        {
            gainsOut[1] = 1.f;
            return;
        }
        if (!vbapUseIdx(participateInVbap, 1))
        {
            gainsOut[0] = 1.f;
            return;
        }

        float L0x = 0.f, L0y = 0.f, L1x = 0.f, L1y = 0.f;
        if (!horizontalUnitDirFromPosition(speakers[0], L0x, L0y)
            || !horizontalUnitDirFromPosition(speakers[1], L1x, L1y))
        {
            fallbackNearestHorizontal(speakers, participateInVbap, numSpeakers, uHoriz, gainsOut);
            return;
        }

        float g0 = 0.f, g1 = 0.f;
        if (solveHorizontalTwoSpeakers(L0x,
                                       L0y,
                                       L1x,
                                       L1y,
                                       uHoriz.x,
                                       uHoriz.y,
                                       g0,
                                       g1)
            && g0 >= -kGainEps && g1 >= -kGainEps)
        {
            g0 = std::max(0.f, g0);
            g1 = std::max(0.f, g1);
            pulkkiNormalize2(g0, g1);
            gainsOut[0] = g0;
            gainsOut[1] = g1;
            return;
        }

        fallbackNearestHorizontal(speakers, participateInVbap, numSpeakers, uHoriz, gainsOut);
        return;
    }

    struct AngleIdx
    {
        float ang;
        size_t idx;
    };
    std::vector<AngleIdx> nodes;
    nodes.reserve(numSpeakers);
    for (size_t i = 0; i < numSpeakers; ++i)
    {
        if (!vbapUseIdx(participateInVbap, i))
            continue;
        float lx = 0.f, ly = 0.f;
        if (!horizontalUnitDirFromPosition(speakers[i], lx, ly))
            continue;
        nodes.push_back({ std::atan2(ly, lx), i });
    }

    if (nodes.empty())
        return;

    std::sort(nodes.begin(), nodes.end(), [](const AngleIdx& a, const AngleIdx& b)
              {
                  if (a.ang != b.ang)
                      return a.ang < b.ang;
                  return a.idx < b.idx;
              });

    constexpr float kAngleDupRad = 1.0e-4f;
    std::vector<AngleIdx> ring;
    ring.reserve(nodes.size());
    for (const auto& n : nodes)
    {
        if (!ring.empty() && std::abs(n.ang - ring.back().ang) < kAngleDupRad)
            continue;
        ring.push_back(n);
    }

    if (ring.size() == 1)
    {
        gainsOut[ring[0].idx] = 1.f;
        return;
    }

    constexpr float twoPi = 6.28318530717958647693f;
    const float tu = std::atan2(uHoriz.y, uHoriz.x);
    const size_t m = ring.size();

    std::vector<float> segEnd(m + 1);
    for (size_t i = 0; i < m; ++i)
        segEnd[i] = ring[i].ang;
    segEnd[m] = ring[0].ang + twoPi;

    float tup = tu;
    while (tup < segEnd[0])
        tup += twoPi;
    while (tup >= segEnd[0] + twoPi)
        tup -= twoPi;

    size_t seg = m;
    constexpr float boundaryEps = 1.0e-5f;
    for (size_t i = 0; i < m; ++i)
    {
        const float lo = segEnd[i];
        const float hi = segEnd[i + 1];
        if (tup >= lo - boundaryEps && tup <= hi + boundaryEps)
        {
            seg = i;
            break;
        }
    }

    if (seg == m)
    {
        fallbackNearestHorizontal(speakers, participateInVbap, numSpeakers, uHoriz, gainsOut);
        return;
    }

    const size_t i0 = ring[seg].idx;
    const size_t i1 = ring[(seg + 1) % m].idx;

    float L0x = 0.f, L0y = 0.f, L1x = 0.f, L1y = 0.f;
    if (!horizontalUnitDirFromPosition(speakers[i0], L0x, L0y)
        || !horizontalUnitDirFromPosition(speakers[i1], L1x, L1y))
    {
        fallbackNearestHorizontal(speakers, participateInVbap, numSpeakers, uHoriz, gainsOut);
        return;
    }

    float g0 = 0.f, g1 = 0.f;
    if (!solveHorizontalTwoSpeakers(L0x,
                                    L0y,
                                    L1x,
                                    L1y,
                                    uHoriz.x,
                                    uHoriz.y,
                                    g0,
                                    g1)
        || g0 < -kGainEps || g1 < -kGainEps)
    {
        fallbackNearestHorizontal(speakers, participateInVbap, numSpeakers, uHoriz, gainsOut);
        return;
    }

    g0 = std::max(0.f, g0);
    g1 = std::max(0.f, g1);
    pulkkiNormalize2(g0, g1);
    gainsOut[i0] = g0;
    gainsOut[i1] = g1;
}

void computeSpatialVbap(const Vec3* speakerDirectionsUnit,
                        const bool* participateInVbap,
                        size_t numSpeakers,
                        Vec3 sourceDirectionUnit,
                        float* gainsOut) noexcept
{
    using namespace detail;

    if (speakerDirectionsUnit == nullptr || gainsOut == nullptr || numSpeakers == 0)
        return;

    for (size_t i = 0; i < numSpeakers; ++i)
        gainsOut[i] = 0.f;

    const Vec3 u = normalized(sourceDirectionUnit);

    if (numSpeakers == 1)
    {
        if (vbapUseIdx(participateInVbap, 0))
            gainsOut[0] = 1.f;
        return;
    }

    if (numSpeakers == 2)
    {
        if (!vbapUseIdx(participateInVbap, 0) && !vbapUseIdx(participateInVbap, 1))
            return;
        if (!vbapUseIdx(participateInVbap, 0))
        {
            gainsOut[1] = 1.f;
            return;
        }
        if (!vbapUseIdx(participateInVbap, 1))
        {
            gainsOut[0] = 1.f;
            return;
        }

        float g0 = 0.f, g1 = 0.f;

        if (vbapTwoSpeakers3D(speakerDirectionsUnit[0], speakerDirectionsUnit[1], u, g0, g1))
        {
            gainsOut[0] = g0;
            gainsOut[1] = g1;
            return;
        }

        fallbackNearest3d(speakerDirectionsUnit, participateInVbap, numSpeakers, u, gainsOut);
        return;
    }

    bool found = false;
    float bestG0 = 0.f, bestG1 = 0.f, bestG2 = 0.f;
    size_t bestI0 = 0, bestI1 = 0, bestI2 = 0;
    float bestScore = -1.f;

    float inv[9];
    float gv[3];

    for (size_t i0 = 0; i0 < numSpeakers; ++i0)
    {
        if (!vbapUseIdx(participateInVbap, i0))
            continue;
        for (size_t i1 = i0 + 1; i1 < numSpeakers; ++i1)
        {
            if (!vbapUseIdx(participateInVbap, i1))
                continue;
            for (size_t i2 = i1 + 1; i2 < numSpeakers; ++i2)
            {
                if (!vbapUseIdx(participateInVbap, i2))
                    continue;
                const Vec3 c0 = speakerDirectionsUnit[i0];
                const Vec3 c1 = speakerDirectionsUnit[i1];
                const Vec3 c2 = speakerDirectionsUnit[i2];

                if (!invertMatrixColumns(c0, c1, c2, inv))
                    continue;

                multiplyInverseByVector(inv, u, gv);

                const float g0 = gv[0];
                const float g1 = gv[1];
                const float g2 = gv[2];

                if (g0 < -kGainEps || g1 < -kGainEps || g2 < -kGainEps)
                    continue;

                const float rawMin = std::min(g0, std::min(g1, g2));

                float hg0 = std::max(0.f, g0);
                float hg1 = std::max(0.f, g1);
                float hg2 = std::max(0.f, g2);
                pulkkiNormalize3(hg0, hg1, hg2);

                if (!found || rawMin > bestScore)
                {
                    found = true;
                    bestScore = rawMin;
                    bestG0 = hg0;
                    bestG1 = hg1;
                    bestG2 = hg2;
                    bestI0 = i0;
                    bestI1 = i1;
                    bestI2 = i2;
                }
            }
        }
    }

    if (found)
    {
        gainsOut[bestI0] = bestG0;
        gainsOut[bestI1] = bestG1;
        gainsOut[bestI2] = bestG2;
        return;
    }

    fallbackSparseTwoSpeakerOrNearest(speakerDirectionsUnit, participateInVbap, numSpeakers, u, gainsOut);
}

void computeHorizontalMdap(const Vec3* speakers,
                           const bool* participateInVbap,
                           size_t numSpeakers,
                           Vec3 sourceDirectionUnit,
                           float spreadDeg,
                           float* gainsOut) noexcept
{
    using namespace detail;

    if (speakers == nullptr || gainsOut == nullptr || numSpeakers == 0)
        return;

    constexpr float kSpreadEps = 1.0e-4f;
    if (spreadDeg <= kSpreadEps)
    {
        computeHorizontalVbap(speakers,
                              participateInVbap,
                              numSpeakers,
                              sourceDirectionUnit,
                              gainsOut);
        return;
    }

    std::memset(gainsOut, 0, numSpeakers * sizeof(float));
    std::vector<float> tmp(numSpeakers);

    const Vec3 u = normalized(sourceDirectionUnit);
    const float horizLen = std::sqrt(u.x * u.x + u.y * u.y);
    const Vec3 uHoriz =
        horizLen > kMinDen ? Vec3{ u.x / horizLen, u.y / horizLen, 0.f } : Vec3{ 1.f, 0.f, 0.f };
    const float th0 = std::atan2(uHoriz.y, uHoriz.x);
    const float spreadRad = spreadDeg * (3.14159265358979323846f / 180.f);

    const int K = kMdapDefaultSpreadSegments;
    for (int j = 0; j < K; ++j)
    {
        const float t = (K <= 1) ? 0.f : (-0.5f + (float) j / (float) (K - 1));
        const float th = th0 + t * spreadRad;
        const Vec3 dir{ std::cos(th), std::sin(th), 0.f };
        computeHorizontalVbap(speakers, participateInVbap, numSpeakers, dir, tmp.data());
        for (size_t i = 0; i < numSpeakers; ++i)
            gainsOut[i] += tmp[i];
    }

    normalizeGainVectorL2(gainsOut, numSpeakers);

    float chk = 0.f;
    for (size_t i = 0; i < numSpeakers; ++i)
        chk += gainsOut[i] * gainsOut[i];
    if (chk < kMinDen)
        computeHorizontalVbap(speakers,
                                participateInVbap,
                                numSpeakers,
                                sourceDirectionUnit,
                                gainsOut);
}

void computeSpatialMdap(const Vec3* speakerDirectionsUnit,
                        const bool* participateInVbap,
                        size_t numSpeakers,
                        Vec3 sourceDirectionUnit,
                        float spreadDeg,
                        float* gainsOut) noexcept
{
    using namespace detail;

    if (speakerDirectionsUnit == nullptr || gainsOut == nullptr || numSpeakers == 0)
        return;

    constexpr float kSpreadEps = 1.0e-4f;
    if (spreadDeg <= kSpreadEps)
    {
        computeSpatialVbap(speakerDirectionsUnit,
                           participateInVbap,
                           numSpeakers,
                           sourceDirectionUnit,
                           gainsOut);
        return;
    }

    std::memset(gainsOut, 0, numSpeakers * sizeof(float));
    std::vector<float> tmp(numSpeakers);

    const Vec3 uNom = normalized(sourceDirectionUnit);
    Vec3 e1 {}, e2 {};
    buildTangentBasis(uNom, e1, e2);

    const float beta = (spreadDeg * 0.5f) * (3.14159265358979323846f / 180.f);
    const float sb = std::sin(beta);
    const float cb = std::cos(beta);
    const int K = kMdapDefaultSpreadSegments;
    const float twoPi = 6.28318530717958647693f;

    for (int k = 0; k < K; ++k)
    {
        const float phi = twoPi * (float) k / (float) K;
        const float c = sb * std::cos(phi);
        const float s = sb * std::sin(phi);
        Vec3 dir = uNom * cb + e1 * c + e2 * s;
        dir = normalized(dir);
        computeSpatialVbap(speakerDirectionsUnit,
                           participateInVbap,
                           numSpeakers,
                           dir,
                           tmp.data());
        for (size_t i = 0; i < numSpeakers; ++i)
            gainsOut[i] += tmp[i];
    }

    normalizeGainVectorL2(gainsOut, numSpeakers);

    float chk = 0.f;
    for (size_t i = 0; i < numSpeakers; ++i)
        chk += gainsOut[i] * gainsOut[i];
    if (chk < kMinDen)
        computeSpatialVbap(speakerDirectionsUnit,
                           participateInVbap,
                           numSpeakers,
                           sourceDirectionUnit,
                           gainsOut);
}

} // namespace iae