// test_kdtree3d_matches_brute_force.cpp
//
// v0.5 P2: assert KdTree3D::nearest matches the brute-force great-circle
// reference for a synthetic 1024-position table over 10k random queries.
//
// Construction: 1024 uniform points on the unit sphere (Fibonacci spiral).
// Queries:      10k random (az, el) draws from a deterministic RNG.
//
// On the unit sphere, squared Euclidean distance is monotone with
// great-circle distance, so the two should agree exactly.

#include "hrtf/HrtfLookup.h"
#include "hrtf/KdTree3D.h"
#include "hrtf/SofaBinReader.h"

#include <cmath>
#include <cstdio>
#include <random>

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kRad2Deg = 180.f / kPi;

int main()
{
    // Build a 1024-position table via Fibonacci spiral over unit sphere.
    constexpr int N = 1024;
    spe::hrtf::HrtfTable table{};
    table.n_positions = N;
    table.ir_length   = 64;
    table.n_receivers = 2;
    table.sample_rate = 48000.f;
    table.positions.resize(N * 3);
    // IR data is irrelevant for nearest-neighbor; allocate zeros to satisfy ir() lookups.
    table.ir_data.assign(static_cast<std::size_t>(N) * 2u * 64u, 0.f);

    const float golden = (1.f + std::sqrt(5.f)) * 0.5f;
    for (int i = 0; i < N; ++i) {
        const float t  = static_cast<float>(i) / static_cast<float>(N - 1);
        const float el = std::asin(2.f * t - 1.f);                 // [-π/2, π/2]
        const float az = 2.f * kPi * std::fmod(static_cast<float>(i) / golden, 1.f);
        const float az_norm = (az > kPi) ? (az - 2.f * kPi) : az;  // [-π, π]
        table.positions[static_cast<std::size_t>(i * 3 + 0)] = az_norm * kRad2Deg;
        table.positions[static_cast<std::size_t>(i * 3 + 1)] = el * kRad2Deg;
        table.positions[static_cast<std::size_t>(i * 3 + 2)] = 1.f;  // unit distance
    }

    spe::hrtf::KdTree3D tree;
    tree.build(table);
    if (!tree.isBuilt() || tree.size() != N) {
        std::fprintf(stderr, "FAIL: tree.size()=%d expected %d\n", tree.size(), N);
        return 1;
    }

    std::mt19937 rng(0xCAFE);
    std::uniform_real_distribution<float> az_dist(-kPi, kPi);
    std::uniform_real_distribution<float> el_dist(-kPi * 0.5f, kPi * 0.5f);

    constexpr int kQueries = 10000;
    int mismatches = 0;
    for (int q = 0; q < kQueries; ++q) {
        const float az = az_dist(rng);
        const float el = el_dist(rng);
        const int kd_idx = tree.nearest(az, el);
        const int bf_idx = spe::hrtf::nearestPositionBruteForceForTest(
            table, az * kRad2Deg, el * kRad2Deg);
        if (kd_idx != bf_idx) {
            // Tie-breaks: KD-tree may legitimately return a different position
            // when two are equidistant within float epsilon. Verify equality
            // by computing both distances and checking parity.
            float kd_x, kd_y, kd_z;
            float bf_x, bf_y, bf_z;
            const float* p = table.positions.data();
            const float kd_az = p[kd_idx * 3 + 0] / kRad2Deg;
            const float kd_el = p[kd_idx * 3 + 1] / kRad2Deg;
            const float bf_az = p[bf_idx * 3 + 0] / kRad2Deg;
            const float bf_el = p[bf_idx * 3 + 1] / kRad2Deg;
            const float qcel = std::cos(el);
            const float qx = qcel * std::cos(az), qy = std::sin(el), qz = qcel * std::sin(az);
            const float kdcel = std::cos(kd_el);
            kd_x = kdcel * std::cos(kd_az); kd_y = std::sin(kd_el); kd_z = kdcel * std::sin(kd_az);
            const float bfcel = std::cos(bf_el);
            bf_x = bfcel * std::cos(bf_az); bf_y = std::sin(bf_el); bf_z = bfcel * std::sin(bf_az);
            const float kd_d = (qx-kd_x)*(qx-kd_x)+(qy-kd_y)*(qy-kd_y)+(qz-kd_z)*(qz-kd_z);
            const float bf_d = (qx-bf_x)*(qx-bf_x)+(qy-bf_y)*(qy-bf_y)+(qz-bf_z)*(qz-bf_z);
            if (std::abs(kd_d - bf_d) > 1e-5f) {
                ++mismatches;
                if (mismatches <= 3) {
                    std::fprintf(stderr,
                        "FAIL: q=%d az=%.3f el=%.3f kd=%d (d=%.6f) bf=%d (d=%.6f)\n",
                        q, az, el, kd_idx, kd_d, bf_idx, bf_d);
                }
            }
        }
    }

    if (mismatches != 0) {
        std::fprintf(stderr, "FAIL: %d/%d queries diverged from brute force\n",
                     mismatches, kQueries);
        return 1;
    }

    std::puts("PASS test_kdtree3d_matches_brute_force");
    return 0;
}
