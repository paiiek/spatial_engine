// Phase 1 PoC (Dreamscape Convergence): ported VAP kernel end-to-end.
//
// Proves the porting thesis works: a reference algorithm (Vap.cpp, namespace
// iae, juce-free) compiles inside the mmhoa tree and produces correct gains
// when fed geometry through the canonical coordinate adapter (coords/Coords.h).
//
// Setup: octahedron speaker rig defined in the mmhoa frame (+X right,+Y up,
// +Z front), converted to the ported frame (+X right,+Y front,+Z up) via the
// Y<->Z swap adapter. Object position likewise comes from a mmhoa (az,el).
//
// Asserts:
//   1. equal-power: sum(g^2) ~= 1 for both center and wall objects.
//   2. L/R invariant through the FULL VAP path: object on the RIGHT (mmhoa
//      az>0) drives the RIGHT speaker harder than the LEFT.
//   3. volumetric vs directional: a wall object concentrates energy (high peak
//      gain) while a centre object spreads it (lower peak, more uniform).

#include "coords/Coords.h"
#include "render/ported/SpatialMath.h"
#include "render/ported/Vap.h"

#include <cmath>
#include <cstdio>
#include <cstddef>

static int failures = 0;
#define CHECK(cond, msg)                              \
    do {                                              \
        if (!(cond)) {                                \
            std::fprintf(stderr, "FAIL: %s\n", msg);  \
            ++failures;                               \
        }                                             \
    } while (0)

namespace {
constexpr float kPi = 3.14159265358979323846f;

iae::Vec3 mmhoaToPorted(float x, float y, float z) {
    auto p = spe::coords::mmhoa_to_ported(x, y, z);
    return {p[0], p[1], p[2]};
}

// Object unit direction from mmhoa (az,el) -> ported-frame position at `dist`.
iae::Vec3 objPorted(float az_rad, float el_rad, float dist_m) {
    auto d = spe::coords::pipeline_dir_to_ported(az_rad, el_rad);
    return {d[0] * dist_m, d[1] * dist_m, d[2] * dist_m};
}

float sumSq(const float* g, size_t n) {
    float s = 0.f;
    for (size_t i = 0; i < n; ++i) s += g[i] * g[i];
    return s;
}
float peak(const float* g, size_t n) {
    float m = 0.f;
    for (size_t i = 0; i < n; ++i) m = std::fmax(m, g[i]);
    return m;
}
}  // namespace

int main() {
    // Octahedron rig, defined in mmhoa frame then converted to ported frame.
    // Index: 0 RIGHT(+Xr) 1 LEFT(-Xr) 2 FRONT(+Zr) 3 BACK(-Zr) 4 UP(+Yr) 5 DOWN(-Yr)
    const float R = 2.0f;
    iae::Vec3 spkPos[6] = {
        mmhoaToPorted( R, 0, 0),  // right
        mmhoaToPorted(-R, 0, 0),  // left
        mmhoaToPorted( 0, 0, R),  // front
        mmhoaToPorted( 0, 0,-R),  // back
        mmhoaToPorted( 0, R, 0),  // up
        mmhoaToPorted( 0,-R, 0),  // down
    };
    iae::Vec3 spkDir[6];
    for (int i = 0; i < 6; ++i) spkDir[i] = iae::normalized(spkPos[i]);
    const size_t N = 6;
    const iae::Vec3 center{0, 0, 0};
    const float curve = 2.0f, expo = 2.0f;

    constexpr int RIGHT = 0, LEFT = 1;

    // --- wall object on the RIGHT: mmhoa az=+90deg, el=0, near the wall ---
    float gWall[6] = {};
    {
        iae::Vec3 obj = objPorted(+kPi / 2.f, 0.f, 0.95f * R);
        iae::computeVolumetricAmplitudePanning(
            spkPos, spkDir, nullptr, N, obj, center, R, curve, expo, gWall);

        CHECK(std::fabs(sumSq(gWall, N) - 1.0f) < 1e-3f,
              "VAP wall: sum(g^2) == 1 (equal power)");
        // L/R invariant through the full port: right > left.
        CHECK(gWall[RIGHT] > gWall[LEFT],
              "VAP wall: RIGHT object -> right speaker > left speaker (L/R lock)");
        CHECK(gWall[RIGHT] == peak(gWall, N),
              "VAP wall: right speaker is the peak");
    }

    // --- centre object: volumetric, energy spread across the rig ---
    float gCtr[6] = {};
    {
        // tiny offset to the right so direction is defined, but ~at centre.
        iae::Vec3 obj = objPorted(+kPi / 2.f, 0.f, 0.02f * R);
        iae::computeVolumetricAmplitudePanning(
            spkPos, spkDir, nullptr, N, obj, center, R, curve, expo, gCtr);

        CHECK(std::fabs(sumSq(gCtr, N) - 1.0f) < 1e-3f,
              "VAP centre: sum(g^2) == 1 (equal power)");
        // centre still nudged right, so no inversion.
        CHECK(gCtr[RIGHT] >= gCtr[LEFT] - 1e-4f,
              "VAP centre: no L/R inversion");
    }

    // --- volumetric vs directional: wall concentrates, centre spreads ---
    CHECK(peak(gWall, N) > peak(gCtr, N),
          "VAP: wall peak gain > centre peak gain (directional vs volumetric)");

    if (failures == 0) {
        std::printf("convergence_vap_port OK\n");
        return 0;
    }
    std::fprintf(stderr, "convergence_vap_port FAILED: %d assertion(s)\n", failures);
    return 1;
}
