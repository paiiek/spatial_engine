// test_kdtree3d_8_canonical_directions.cpp
//
// v0.5 Q4: assert KdTree3D::nearest() returns the expected index for 8
// canonical directions against a small known SH-direction dataset.
//
// Build a 9-point HrtfTable at exact canonical positions, then query each of
// the 8 canonical directions and verify the returned index matches a
// hand-computed reference table.

#include "hrtf/KdTree3D.h"
#include "hrtf/SofaBinReader.h"

#include <cmath>
#include <cstdio>

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kDeg2Rad = kPi / 180.f;

int main()
{
    // Build a small HrtfTable whose positions ARE the 8 canonical directions
    // plus one extra point (directly behind-up) to give the tree a non-trivial
    // structure. Positions stored as (az_deg, el_deg, distance).
    //
    // Index → (az_deg, el_deg):
    //   0 = front        az=  0, el=0   → (0, 1, 0) Cartesian
    //   1 = back         az=180, el=0   → (0,-1, 0)
    //   2 = left         az= 90, el=0   → (-1,0, 0)  [engine: az=+90 → left]
    //   3 = right        az=-90, el=0   → (1, 0, 0)
    //   4 = up           az=  0, el=90  → (0, 0, 1) (sin(el)=1 → y=1... wait)
    //   5 = down         az=  0, el=-90
    //   6 = FL30         az=-30, el=0
    //   7 = FR30         az=+30, el=0
    //   8 = extra        az= 45, el=45  (tie-breaker padding)
    //
    // Coordinate convention (from KdTree3D.h):
    //   x = cos(el)*cos(az)
    //   y = sin(el)
    //   z = cos(el)*sin(az)
    // Engine frame az=0 → front (x=1 if el=0? No: cos(0)*cos(0)=1 x, sin(az)=0 z)
    // Wait: az=0, el=0 → x=cos(0)*cos(0)=1, y=sin(0)=0, z=cos(0)*sin(0)=0 → (1,0,0)
    // So:
    //   front  az=0,   el=0   → (1,0,0)
    //   back   az=π,   el=0   → (-1,0,0)
    //   right  az=π/2, el=0   → (0,0,1) ... hmm, depends on convention
    //
    // Let's just use the exact same coordinate mapping as the existing tests.
    // From test_kdtree3d_matches_brute_force.cpp the table stores (az_deg, el_deg, 1.0)
    // and tree.nearest(az_rad, el_rad) is called directly in radians.
    // We query at known az_rad/el_rad and verify the nearest point is the one we placed
    // at that exact location.

    constexpr int N = 9;
    spe::hrtf::HrtfTable table{};
    table.n_positions = N;
    table.ir_length   = 16;
    table.n_receivers = 2;
    table.sample_rate = 48000.f;
    table.positions.resize(static_cast<std::size_t>(N * 3));
    table.ir_data.assign(static_cast<std::size_t>(N) * 2u * 16u, 0.f);

    // Positions: (az_deg, el_deg, 1.0)
    // az convention: az_rad passed to nearest() == az_deg stored * deg2rad.
    struct Pos { float az_deg; float el_deg; };
    const Pos pts[N] = {
        {   0.f,   0.f },  // 0 front
        { 180.f,   0.f },  // 1 back
        {  90.f,   0.f },  // 2 left  (az=+90)
        { -90.f,   0.f },  // 3 right (az=-90)
        {   0.f,  90.f },  // 4 up
        {   0.f, -90.f },  // 5 down
        { -30.f,   0.f },  // 6 FL30
        {  30.f,   0.f },  // 7 FR30
        {  45.f,  45.f },  // 8 extra
    };
    for (int i = 0; i < N; ++i) {
        table.positions[static_cast<std::size_t>(i * 3 + 0)] = pts[i].az_deg;
        table.positions[static_cast<std::size_t>(i * 3 + 1)] = pts[i].el_deg;
        table.positions[static_cast<std::size_t>(i * 3 + 2)] = 1.f;
    }

    spe::hrtf::KdTree3D tree;
    tree.build(table);
    if (!tree.isBuilt() || tree.size() != N) {
        std::fprintf(stderr, "FAIL: tree.size()=%d expected %d\n", tree.size(), N);
        return 1;
    }

    // Hand-computed reference: query at exact stored az/el → should return same index.
    struct Query { float az_deg; float el_deg; int expected_idx; const char* name; };
    const Query queries[] = {
        {   0.f,   0.f, 0, "front"  },
        { 180.f,   0.f, 1, "back"   },
        {  90.f,   0.f, 2, "left"   },
        { -90.f,   0.f, 3, "right"  },
        {   0.f,  90.f, 4, "up"     },
        {   0.f, -90.f, 5, "down"   },
        { -30.f,   0.f, 6, "FL30"   },
        {  30.f,   0.f, 7, "FR30"   },
    };

    int failures = 0;
    for (const auto& q : queries) {
        const float az_rad = q.az_deg * kDeg2Rad;
        const float el_rad = q.el_deg * kDeg2Rad;
        const int got = tree.nearest(az_rad, el_rad);
        if (got != q.expected_idx) {
            std::fprintf(stderr,
                "FAIL: %s: expected idx=%d got=%d\n",
                q.name, q.expected_idx, got);
            ++failures;
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "FAIL: %d/8 canonical direction queries wrong\n", failures);
        return 1;
    }

    std::puts("PASS test_kdtree3d_8_canonical_directions");
    return 0;
}
