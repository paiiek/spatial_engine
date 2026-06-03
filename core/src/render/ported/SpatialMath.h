// === PORTED (Dreamscape Convergence) ========================================
// Source: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   commit f2cb796 . Source/SpatialMath.h . Direct port authorized (convergence D3).
// Adaptations: include paths -> "render/ported/"; namespace iae preserved;
//   kPrototypeChannels defined locally in this module (was SpatialSessionState.h).
// Do not hand-edit logic; re-sync from upstream if the reference changes.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace iae
{

// Ported max channel count (upstream lived in SpatialSessionState.h). Sized to
// match mmhoa's 128 ceiling; used for fixed stack scratch in Vap.cpp.
constexpr int kPrototypeChannels = 128;

struct Vec3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

inline float dot(Vec3 a, Vec3 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(Vec3 a, Vec3 b) noexcept
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

inline Vec3 operator*(Vec3 v, float s) noexcept
{
    return { v.x * s, v.y * s, v.z * s };
}

inline Vec3 operator+(Vec3 a, Vec3 b) noexcept
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 operator-(Vec3 a, Vec3 b) noexcept
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline float length(Vec3 v) noexcept
{
    return std::sqrt(dot(v, v));
}

inline Vec3 normalized(Vec3 v) noexcept
{
    const float len = length(v);
    if (len < 1.0e-8f)
        return { 1.f, 0.f, 0.f };
    return { v.x / len, v.y / len, v.z / len };
}

inline Vec3 directionFromAzimuthElevationDegrees(float azimuthDeg, float elevationDeg) noexcept
{
    const float az = azimuthDeg * (3.14159265358979323846f / 180.f);
    const float el = elevationDeg * (3.14159265358979323846f / 180.f);
    const float cosEl = std::cos(el);
    return normalized({ cosEl * std::cos(az), cosEl * std::sin(az), std::sin(el) });
}

/** 균등 각도로 수평면(z=0) 링 배치. 0° 스피커는 +X 방향. */
inline Vec3 horizontalRingDirection(int speakerIndex, int numSpeakers) noexcept
{
    if (numSpeakers < 1)
        return { 1.f, 0.f, 0.f };

    const float t = (3.14159265358979323846f * 2.f) * (float) speakerIndex / (float) numSpeakers;
    return { std::cos(t), std::sin(t), 0.f };
}

/** 듣는 위치(원점) 기준 스피커 카테시안 좌표 — VBAP에는 정규화된 방향 벡터로 변환해 사용 */
inline Vec3 horizontalRingPosition(int speakerIndex, int numSpeakers, float radius = 2.f) noexcept
{
    const Vec3 d = horizontalRingDirection(speakerIndex, numSpeakers);
    return { d.x * radius, d.y * radius, d.z * radius };
}

/** 스피커 위치에서 원점(리스너)을 향하는 단위 벡터 — directionFromAzimuthElevationDegrees 와 동일 규약의 요·피치(°). */
inline void yawPitchDegTowardOrigin(Vec3 speakerPosition, float& yawDeg, float& pitchDeg) noexcept
{
    Vec3 t { -speakerPosition.x, -speakerPosition.y, -speakerPosition.z };
    const float len = length(t);
    constexpr float eps = 1.0e-8f;
    if (len < eps)
    {
        yawDeg = 0.f;
        pitchDeg = 0.f;
        return;
    }

    t.x /= len;
    t.y /= len;
    t.z /= len;

    constexpr float radToDeg = 57.295779513082320876798154814105f;
    yawDeg = std::atan2(t.y, t.x) * radToDeg;
    pitchDeg = std::atan2(t.z, std::sqrt(t.x * t.x + t.y * t.y)) * radToDeg;
}

/** 단위 진행 방향(월드) → directionFromAzimuthElevationDegrees 와 동일 규약의 yaw/pitch(°). */
inline void yawPitchDegFromWorldForward(Vec3 forward, float& yawDeg, float& pitchDeg) noexcept
{
    Vec3 f = normalized(forward);
    constexpr float radToDeg = 57.295779513082320876798154814105f;
    const float horiz = std::sqrt(f.x * f.x + f.y * f.y);
    yawDeg = std::atan2(f.y, f.x) * radToDeg;
    pitchDeg = std::atan2(f.z, std::max(1.0e-8f, horiz)) * radToDeg;
}

} // namespace iae