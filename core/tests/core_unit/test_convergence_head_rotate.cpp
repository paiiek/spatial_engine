// Phase 2 increment 2.6a (Dreamscape Convergence): head-rotation core golden.
// Locks the binaural headtracking direction rotation BEFORE any audio/OSC wiring
// (2.6b). The single L/R-critical piece — rotate_engine_dir_by_head — is a pure
// stack function (no alloc by construction), so this unit test is the full gate.
// Guarantees:
//   1. Yaw-only is ADDITIVE: az' = az + yaw, el' unchanged (the locked form).
//   2. P0 L/R LOCK: yaw +30° on a front source -> az' = +30° (RIGHT, R-louder).
//   3. Zero rotation is identity over a grid.
//   4. Pitch / roll change elevation sensibly, az stays sane, all outputs finite.

#include "coords/Coords.h"
#include "coords/CoordsTests.h"

#include <cmath>
#include <cstdio>

static constexpr float kTol = 1e-5f;
static bool near(float a, float b) { return std::fabs(a - b) < kTol; }
static int failures = 0;

#define CHECK(cond, msg)                              \
    do {                                              \
        if (!(cond)) {                                \
            std::fprintf(stderr, "FAIL: %s\n", msg);  \
            ++failures;                               \
        }                                             \
    } while (0)

// Wrap an angle into (-pi, pi] for comparison across atan2's branch cut.
static float wrap(float a) { return std::atan2(std::sin(a), std::cos(a)); }
static bool near_angle(float a, float b) { return near(wrap(a - b), 0.0f); }

int main() {
    using namespace spe::coords;
    using namespace spe::coords::tests;

    const float deg = kPi / 180.0f;

    // --- (1) yaw-only is additive: az' = az + yaw, el' = el ---
    const float azs[]  = {-2.0f, -kPi4, 0.0f, 0.3f, kPi4, 1.9f};
    const float els[]  = {-1.0f, -0.2f, 0.0f, 0.5f, 1.2f};
    const float yaws[] = {-2.5f, -kPi4, 0.0f, 0.4f, kPi2, 2.7f};
    for (float az : azs)
        for (float el : els)
            for (float yaw : yaws) {
                auto [azp, elp] = rotate_engine_dir_by_head(az, el, yaw, 0.0f, 0.0f);
                CHECK(near_angle(azp, az + yaw), "yaw-only: az' = az + yaw");
                CHECK(near(elp, el), "yaw-only: el' = el");
            }

    // --- (2) P0 L/R LOCK: yaw +30deg, front source (az=0,el=0) -> az'=+30deg ---
    {
        auto [azp, elp] = rotate_engine_dir_by_head(0.0f, 0.0f, 30.0f * deg, 0.0f, 0.0f);
        CHECK(near_angle(azp, 30.0f * deg), "P0: yaw+30 front -> az'=+30deg");
        CHECK(azp > 0.0f, "P0: az' > 0 (RIGHT, R-louder) — NOT inverted");
        CHECK(near(elp, 0.0f), "P0: el' stays 0");
        // sanity vs the locked stereo pan authority in Coords.h
        CHECK(stereo_pan_from_pipeline_az(azp) > 0.0f, "P0: pan(az') > 0 -> R louder");
    }

    // --- (3) zero rotation is identity ---
    for (float az : azs)
        for (float el : els) {
            auto [azp, elp] = rotate_engine_dir_by_head(az, el, 0.0f, 0.0f, 0.0f);
            CHECK(near_angle(azp, az) && near(elp, el), "zero rotation = identity");
        }

    // --- (4a) pure pitch on a front source raises/lowers elevation, az stays 0 ---
    {
        auto [azp, elp] = rotate_engine_dir_by_head(0.0f, 0.0f, 0.0f, 30.0f * deg, 0.0f);
        CHECK(near_angle(azp, 0.0f), "pitch: front source stays az=0");
        CHECK(near(std::fabs(elp), 30.0f * deg), "pitch: |el'| = 30deg");
    }
    // --- (4b) pure roll on a RIGHT source tilts it in elevation, finite ---
    {
        auto [azp, elp] = rotate_engine_dir_by_head(kPi2, 0.0f, 0.0f, 0.0f, 30.0f * deg);
        CHECK(near(std::fabs(elp), 30.0f * deg), "roll: |el'| = 30deg on side source");
        CHECK(std::isfinite(azp) && std::isfinite(elp), "roll: finite");
    }
    // --- (4c) full 3-axis composition stays finite & in-range over a grid ---
    for (float az : azs)
        for (float el : els)
            for (float r : yaws) {
                auto [azp, elp] = rotate_engine_dir_by_head(az, el, r, r * 0.5f, -r * 0.3f);
                CHECK(std::isfinite(azp) && std::isfinite(elp), "3-axis finite");
                CHECK(elp >= -kPi2 - kTol && elp <= kPi2 + kTol, "el' in [-pi/2, pi/2]");
            }

    if (failures == 0) {
        std::printf("convergence_head_rotate OK\n");
        return 0;
    }
    std::fprintf(stderr, "convergence_head_rotate FAILED: %d assertion(s)\n", failures);
    return 1;
}
