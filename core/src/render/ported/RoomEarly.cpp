// core/src/render/ported/RoomEarly.cpp
// See RoomEarly.h for provenance (immersive-audio-engine RoomEngine @ f2cb796).

#include "render/ported/RoomEarly.h"

#include <algorithm>
#include <cmath>

namespace iae {

Vec3 firstOrderImage(const Vec3& p, const Vec3& half, int wallAxis) noexcept {
    // RoomEngine.cpp:20-39 — listener at origin, walls at ±half.{x,y,z}.
    switch (wallAxis) {
    case 0:  return { 2.f * half.x - p.x, p.y, p.z };
    case 1:  return { -2.f * half.x - p.x, p.y, p.z };
    case 2:  return { p.x, 2.f * half.y - p.y, p.z };
    case 3:  return { p.x, -2.f * half.y - p.y, p.z };
    case 4:  return { p.x, p.y, 2.f * half.z - p.z };
    default: return { p.x, p.y, -2.f * half.z - p.z };
    }
}

Vec3 earlySpreadDirection(Vec3 u, float widthDeg, int wi, int numWidthSamples) noexcept {
    // RoomEngine.cpp:52-78.
    u = normalized(u);
    if (widthDeg < 0.02f || numWidthSamples < 2)
        return u;

    Vec3 ref { 0.f, 0.f, 1.f };
    if (std::abs(dot(u, ref)) > 0.92f)
        ref = { 0.f, 1.f, 0.f };

    Vec3 v = normalized(cross(u, ref));
    if (length(v) < 1.0e-5f)
        v = normalized(cross(u, Vec3 { 1.f, 0.f, 0.f }));

    const float halfRad = (widthDeg * 0.5f) * (3.14159265358979323846f / 180.f);
    const float span =
        (numWidthSamples > 1) ? (2.f * halfRad / (float) (numWidthSamples - 1)) : 0.f;
    const float t  = -halfRad + (float) wi * span;
    const float ct = std::cos(t);
    const float st = std::sin(t);
    return normalized(Vec3 { u.x * ct + v.x * st,
                             u.y * ct + v.y * st,
                             u.z * ct + v.z * st });
}

int computeFirstOrderReflections(const Vec3& objPos, const RoomEarlyParams& p,
                                 double sampleRate, int ringLen,
                                 EarlyReflection out[kNumFirstOrderImages]) noexcept {
    // RoomEngine.cpp:335-485 (geometry/timing/gain only — no per-object state).
    const float hx = std::max(0.5f, std::abs(p.halfExtents.x));
    const float hy = std::max(0.5f, std::abs(p.halfExtents.y));
    const float hz = std::max(0.5f, std::abs(p.halfExtents.z));
    const Vec3  h { hx, hy, hz };

    const float dDirect = std::max(1.0e-4f, length(objPos));
    const float sr      = (float) sampleRate;
    const float earlyBal = std::clamp(p.earlyLateBalance01, 0.f, 1.f);
    const float erGlobal = 0.62f * std::sqrt(earlyBal + 0.05f);
    const int   maxDelay = std::max(1, ringLen - 2);

    int count = 0;
    for (int tap = 0; tap < kNumFirstOrderImages; ++tap) {
        out[tap] = EarlyReflection{};
        const Vec3  img  = firstOrderImage(objPos, h, tap);
        const float dImg = length(img);
        if (dImg < 1.0e-5f)
            continue;

        const float extra    = std::max(0.f, dImg - dDirect);
        const int   delaySmp = std::clamp(
            (int) std::lround((double) extra / (double) kRoomSoundSpeed * (double) sr),
            1, maxDelay);

        Vec3 dir { img.x / dImg, img.y / dImg, img.z / dImg };
        if (const float dl = length(dir); dl > 1.0e-5f)
            dir = { dir.x / dl, dir.y / dl, dir.z / dl };
        else
            dir = { 1.f, 0.f, 0.f };

        const float tapGain =
            erGlobal * 0.52f / std::sqrt(1.f + extra * extra * 0.08f);

        out[tap] = EarlyReflection{ dir, delaySmp, tapGain, true };
        ++count;
    }
    return count;
}

} // namespace iae
