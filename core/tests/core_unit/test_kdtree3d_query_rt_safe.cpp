// test_kdtree3d_query_rt_safe.cpp
//
// v0.5 P2: assert KdTree3D::nearest() is alloc-free over 10k queries.

#include "hrtf/KdTree3D.h"
#include "hrtf/SofaBinReader.h"
#include "util/RtAssertNoAlloc.h"

#include <cmath>
#include <cstdio>
#include <random>

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kRad2Deg = 180.f / kPi;

int main()
{
    spe::util::rt_alloc_violations_reset();

    constexpr int N = 1024;
    spe::hrtf::HrtfTable table{};
    table.n_positions = N;
    table.ir_length   = 64;
    table.n_receivers = 2;
    table.sample_rate = 48000.f;
    table.positions.resize(N * 3);
    table.ir_data.assign(static_cast<std::size_t>(N) * 2u * 64u, 0.f);

    // Fibonacci spiral.
    const float golden = (1.f + std::sqrt(5.f)) * 0.5f;
    for (int i = 0; i < N; ++i) {
        const float t  = static_cast<float>(i) / static_cast<float>(N - 1);
        const float el = std::asin(2.f * t - 1.f);
        const float az = 2.f * kPi * std::fmod(static_cast<float>(i) / golden, 1.f);
        const float az_norm = (az > kPi) ? (az - 2.f * kPi) : az;
        table.positions[static_cast<std::size_t>(i * 3 + 0)] = az_norm * kRad2Deg;
        table.positions[static_cast<std::size_t>(i * 3 + 1)] = el * kRad2Deg;
        table.positions[static_cast<std::size_t>(i * 3 + 2)] = 1.f;
    }

    spe::hrtf::KdTree3D tree;
    tree.build(table);

    // Precompute queries OUTSIDE the no-alloc scope so the RNG's internal
    // state churn doesn't trip the assert.
    constexpr int kQueries = 10000;
    std::vector<std::pair<float, float>> queries;
    queries.reserve(kQueries);
    std::mt19937 rng(0xBEEF);
    std::uniform_real_distribution<float> az_dist(-kPi, kPi);
    std::uniform_real_distribution<float> el_dist(-kPi * 0.5f, kPi * 0.5f);
    for (int q = 0; q < kQueries; ++q) queries.emplace_back(az_dist(rng), el_dist(rng));

    int sink = 0;
    {
        SPE_RT_NO_ALLOC_SCOPE();
        for (const auto& qp : queries) {
            sink ^= tree.nearest(qp.first, qp.second);
        }
    }

    const auto v = spe::util::rt_alloc_violations();
    if (v != 0u) {
        std::fprintf(stderr, "FAIL: rt_alloc_violations()=%llu (expected 0)\n",
                     static_cast<unsigned long long>(v));
        return 1;
    }

    // Tiny sanity: north pole query → some index returned.
    const int north = tree.nearest(0.f, kPi * 0.5f);
    if (north < 0 || north >= N) {
        std::fprintf(stderr, "FAIL: north-pole query out of range: %d\n", north);
        return 1;
    }
    (void)sink;
    std::puts("PASS test_kdtree3d_query_rt_safe");
    return 0;
}
