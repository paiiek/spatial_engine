// Phase 0 (Dreamscape Convergence): canonical render-frame adapter test.
// Verifies the mmhoa<->ported (Y<->Z swap) conversion that ALL ported panning
// kernels (VBAP/VAP/DBAP/WFS) will consume. Two guarantees:
//   1. Involution: swapping twice is identity (no drift, no scale).
//   2. L/R invariant: mmhoa az>0 (RIGHT) -> ported x>0 (RIGHT). This is the
//      anti-regression lock for the L/R inversion bug class (2026-02/03).

#include "coords/Coords.h"
#include "coords/CoordsTests.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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

int main() {
    using namespace spe::coords;
    using namespace spe::coords::tests;

    // --- involution: mmhoa_to_ported is its own inverse over a grid ---
    const float grid[] = {-2.3f, -1.0f, -0.25f, 0.0f, 0.5f, 1.7f, 3.0f};
    for (float x : grid)
        for (float y : grid)
            for (float z : grid) {
                auto p = mmhoa_to_ported(x, y, z);
                auto m = ported_to_mmhoa(p[0], p[1], p[2]);
                CHECK(near(m[0], x) && near(m[1], y) && near(m[2], z),
                      "mmhoa<->ported involution identity");
                // Y<->Z swap definition.
                CHECK(near(p[0], x) && near(p[1], z) && near(p[2], y),
                      "mmhoa_to_ported == (x,z,y)");
            }

    // --- pipeline_dir_to_ported golden table ---
    for (const auto& c : kPipelineDirToPorted) {
        auto d = pipeline_dir_to_ported(c.az_rad, c.el_rad);
        CHECK(near(d[0], c.exp_x), "pipeline_dir_to_ported x");
        CHECK(near(d[1], c.exp_y), "pipeline_dir_to_ported y");
        CHECK(near(d[2], c.exp_z), "pipeline_dir_to_ported z");
        // every direction is a unit vector
        CHECK(near(std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]), 1.0f),
              "pipeline_dir_to_ported unit length");
    }

    // --- L/R INVARIANT (the keystone) ---
    // RIGHT source (az>0) must land on ported +X; LEFT (az<0) on ported -X.
    {
        auto right = pipeline_dir_to_ported( kPi4, 0.0f);
        auto left  = pipeline_dir_to_ported(-kPi4, 0.0f);
        CHECK(right[0] > 0.0f, "L/R: RIGHT (az>0) -> ported x>0");
        CHECK(left[0]  < 0.0f, "L/R: LEFT  (az<0) -> ported x<0");
        CHECK(right[0] > left[0], "L/R: right.x > left.x (no inversion)");
        // front component (y) equal & positive for symmetric +/- az
        CHECK(near(right[1], left[1]) && right[1] > 0.0f,
              "L/R: symmetric az share forward(+Y) component");
    }
    // UP source (el>0) must land on ported +Z; DOWN on -Z.
    {
        auto up   = pipeline_dir_to_ported(0.0f,  kPi4);
        auto down = pipeline_dir_to_ported(0.0f, -kPi4);
        CHECK(up[2] > 0.0f && down[2] < 0.0f, "el>0 -> +Z up; el<0 -> -Z down");
    }
    // Consistency with the engine-native pipeline cartesian: the ported vector
    // is exactly the Y<->Z swap of yaml_speaker_to_cartesian(az,el) (deg).
    {
        const float azDeg = 30.0f, elDeg = 15.0f;
        auto mm  = yaml_speaker_to_cartesian(azDeg, elDeg, 1.0f);
        auto pr  = pipeline_dir_to_ported(azDeg * (kPi / 180.0f),
                                          elDeg * (kPi / 180.0f));
        auto mm2 = mmhoa_to_ported(mm[0], mm[1], mm[2]);
        CHECK(near(pr[0], mm2[0]) && near(pr[1], mm2[1]) && near(pr[2], mm2[2]),
              "pipeline_dir_to_ported == swap(yaml_speaker_to_cartesian)");
    }

    if (failures == 0) {
        std::printf("convergence_coords OK\n");
        return 0;
    }
    std::fprintf(stderr, "convergence_coords FAILED: %d assertion(s)\n", failures);
    return 1;
}
