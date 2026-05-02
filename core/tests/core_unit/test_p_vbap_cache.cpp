// test_p_vbap_cache.cpp — VBAPRenderer gain cache: hit/miss/invalidation tests
#include "render/VBAPRenderer.h"
#include "render/AlgorithmAnalyticReference.h"
#include "geometry/SpeakerLayout.h"
#include <cmath>
#include <cstdio>
#include <array>

#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS
#include "util/RtAssertNoAlloc.h"
#endif

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

#define CHECK_NEAR(a, b, tol) \
    do { float _a=(a), _b=(b); \
         if (std::abs(_a-_b) > (tol)) { \
             std::printf("FAIL %s:%d  |%.8f-%.8f|=%.2e > %.2e\n", \
                 __FILE__,__LINE__,(double)_a,(double)_b, \
                 (double)std::abs(_a-_b),(double)(tol)); \
             ++failures; \
         } } while(0)

using namespace spe::render;
using namespace spe::geometry;

static constexpr float kPi = 3.14159265358979323846f;

static SpeakerLayout make_8ch_3d() {
    SpeakerLayout l;
    l.name = "test_8ch_3d";
    l.regularity = Regularity::IRREGULAR;
    float azs[] = {0.f, 90.f, 180.f, 270.f};
    for (int i = 0; i < 4; ++i) {
        float az = azs[i] * kPi / 180.f;
        Speaker s; s.channel = i+1;
        s.x = std::sin(az); s.y = 0.f; s.z = std::cos(az);
        l.speakers.push_back(s);
    }
    float el = 45.f * kPi / 180.f;
    for (int i = 0; i < 4; ++i) {
        float az = azs[i] * kPi / 180.f;
        Speaker s; s.channel = i+5;
        s.x = std::cos(el)*std::sin(az); s.y = std::sin(el); s.z = std::cos(el)*std::cos(az);
        l.speakers.push_back(s);
    }
    return l;
}

static constexpr int BLOCK = 256;
static constexpr int SPKS  = 8;
static float silence[BLOCK] = {};
static float out[BLOCK * SPKS] = {};

// Sub-test 1: cache hit on repeated (az,el)
static void test1_cache_hit() {
    auto layout = make_8ch_3d();
    VBAPRenderer r;
    r.prepareToPlay(layout, 48000.0);
    r.resetCacheStats();

    ObjectState obj{};
    obj.az_rad = 0.3f; obj.el_rad = 0.1f; obj.dist_m = 1.f; obj.active = true;
    std::array<ObjectState,1> objs = {obj};
    const float* dry[1] = {silence};

    // First call — cold miss
    r.processBlock(std::span<const ObjectState>(objs.data(),1),
                   std::span<const float* const>(dry,1), out, BLOCK);
    CHECK(r.cacheMisses() == 1);
    CHECK(r.cacheHits()   == 0);

    // Second call — warm hit
    r.processBlock(std::span<const ObjectState>(objs.data(),1),
                   std::span<const float* const>(dry,1), out, BLOCK);
    CHECK(r.cacheHits() >= 1);
    std::printf("[PASS] test1: cache hit on repeated (az,el)\n");
}

// Sub-test 2: cache miss on different (az,el); gains match analytic reference
static void test2_different_bin_matches_analytic() {
    auto layout = make_8ch_3d();
    VBAPRenderer r;
    r.prepareToPlay(layout, 48000.0);
    r.resetCacheStats();

    // Use DC input so we can read steady-state output
    static float dc[4096];
    for (int i = 0; i < 4096; ++i) dc[i] = 1.f;
    static float big_out[4096 * SPKS] = {};

    // Snap angles to 0.5deg bin centers so the cached gains computed by the
    // renderer use the SAME (az,el) as the reference call below — cache binning
    // is identity at bin centers, so we can compare gains exactly.
    auto snap_to_bin = [](float rad) {
        const float deg = rad * (180.0f / kPi);
        const float snapped_deg = std::round(deg / 0.5f) * 0.5f;
        return snapped_deg * (kPi / 180.0f);
    };
    const float az0 = snap_to_bin(0.3f);
    const float el0 = snap_to_bin(0.1f);
    const float az1 = snap_to_bin(0.7f);
    const float el1 = snap_to_bin(0.2f);

    ObjectState obj{};
    obj.az_rad = az0; obj.el_rad = el0; obj.dist_m = 1.f; obj.active = true;
    std::array<ObjectState,1> objs = {obj};
    const float* dry[1] = {dc};

    r.processBlock(std::span<const ObjectState>(objs.data(),1),
                   std::span<const float* const>(dry,1), big_out, 4096);

    obj.az_rad = az1; obj.el_rad = el1;
    objs[0] = obj;
    r.processBlock(std::span<const ObjectState>(objs.data(),1),
                   std::span<const float* const>(dry,1), big_out, 4096);

    CHECK(r.cacheMisses() == 2);

    // Compare steady-state output (last sample) against analytic gains.
    // Tolerance accounts for cache-quantization (renderer rounds to 0.5deg bins
    // via deg = rad * (180/3.14159265f), which differs from kPi by ~3.6e-8 rad
    // and yields negligible bin-center drift) and ramp-settle residual.
    auto ref_gains = AlgorithmAnalyticReference::vbap_gain(layout, az1, el1);
    for (int s = 0; s < SPKS; ++s) {
        float out_val = big_out[(4096-1)*SPKS + s];
        CHECK_NEAR(out_val, ref_gains[s], 1e-3f); // ramp-settled, bin-aligned
    }
    std::printf("[PASS] test2: different bin matches analytic reference\n");
}

// Sub-test 3: layout change clears cache
static void test3_layout_invalidation() {
    auto layout_a = make_8ch_3d();
    VBAPRenderer r;
    r.prepareToPlay(layout_a, 48000.0);

    ObjectState obj{};
    obj.az_rad = 0.3f; obj.el_rad = 0.1f; obj.dist_m = 1.f; obj.active = true;
    std::array<ObjectState,1> objs = {obj};
    const float* dry[1] = {silence};

    r.processBlock(std::span<const ObjectState>(objs.data(),1),
                   std::span<const float* const>(dry,1), out, BLOCK);

    auto layout_b = make_8ch_3d(); // fresh layout
    r.prepareToPlay(layout_b, 48000.0);
    r.resetCacheStats();

    CHECK(r.cacheHits()   == 0);
    CHECK(r.cacheMisses() == 0);

    r.processBlock(std::span<const ObjectState>(objs.data(),1),
                   std::span<const float* const>(dry,1), out, BLOCK);
    CHECK(r.cacheMisses() == 1);
    std::printf("[PASS] test3: layout change clears cache\n");
}

#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS
// Sub-test 4: warm-hit path is RT-alloc-free
static void test4_warm_hit_rt_alloc_free() {
    auto layout = make_8ch_3d();
    VBAPRenderer r;
    r.prepareToPlay(layout, 48000.0);

    ObjectState obj{};
    obj.az_rad = 0.3f; obj.el_rad = 0.1f; obj.dist_m = 1.f; obj.active = true;
    std::array<ObjectState,1> objs = {obj};
    const float* dry[1] = {silence};

    // Cold miss (populates cache)
    r.processBlock(std::span<const ObjectState>(objs.data(),1),
                   std::span<const float* const>(dry,1), out, BLOCK);

    // Warm hit — must be alloc-free
    spe::util::rt_alloc_violations_reset();
    {
        SPE_RT_NO_ALLOC_SCOPE();
        r.processBlock(std::span<const ObjectState>(objs.data(),1),
                       std::span<const float* const>(dry,1), out, BLOCK);
    }
    CHECK(spe::util::rt_alloc_violations() == 0);
    std::printf("[PASS] test4: warm-hit path is RT-alloc-free\n");
}
#endif

int main() {
    test1_cache_hit();
    test2_different_bin_matches_analytic();
    test3_layout_invalidation();
#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS
    test4_warm_hit_rt_alloc_free();
#endif
    if (failures == 0) {
        std::printf("[RESULT] PASS\n");
        return 0;
    }
    std::printf("[RESULT] FAIL (%d failures)\n", failures);
    return 1;
}
