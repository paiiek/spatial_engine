// test_p3_dbap.cpp
// (i) Sum-of-squares = 1.0 for all positions.
// (ii) Match AlgorithmAnalyticReference::dbap_gain() within 1e-6.

#include "render/AlgorithmAnalyticReference.h"
#include "geometry/SpeakerLayout.h"
#include <cmath>
#include <cstdio>

static int failures = 0;

#define CHECK_NEAR(a, b, tol) \
    do { float _a=(a), _b=(b); \
         if (std::abs(_a-_b) > (tol)) { \
             std::printf("FAIL %s:%d |%.9f - %.9f| = %.2e > %.2e\n", \
                 __FILE__, __LINE__, (double)_a, (double)_b, \
                 (double)std::abs(_a-_b), (double)(tol)); \
             ++failures; \
         } } while(0)

using namespace spe::render;
using namespace spe::geometry;

static SpeakerLayout make_4ch_layout() {
    SpeakerLayout l;
    l.name = "test_4ch";
    l.regularity = Regularity::CIRCULAR;
    float azs[] = {0.f, 90.f, 180.f, 270.f};
    for (int i = 0; i < 4; ++i) {
        float az = azs[i] * 3.14159265f / 180.f;
        Speaker s;
        s.channel = i + 1;
        s.x = std::sin(az);
        s.y = 0.f;
        s.z = std::cos(az);
        l.speakers.push_back(s);
    }
    return l;
}

int main() {
    auto layout = make_4ch_layout();
    const float rolloff_a = 2.0f;

    // Test multiple source positions
    struct Pos { float x, y, z; };
    Pos positions[] = {
        {0.5f, 0.f, 0.5f},
        {-0.3f, 0.f, 0.7f},
        {0.f, 0.f, 0.f},   // at origin
        {2.f, 0.f, 2.f},   // far away
        {-1.f, 0.5f, -1.f},
    };

    for (const auto& p : positions) {
        auto gains = AlgorithmAnalyticReference::dbap_gain(
            layout, p.x, p.y, p.z, rolloff_a);

        // (i) Sum of squares = 1.0
        float ss = 0.f;
        for (float g : gains) ss += g * g;
        CHECK_NEAR(ss, 1.0f, 1e-5f);

        // (ii) Manually verify against formula
        // g_i = (1/d_i^a) / sqrt(Σ 1/d_i^(2a))
        int N = static_cast<int>(layout.speakers.size());
        float sum_sq2 = 0.f;
        for (int i = 0; i < N; ++i) {
            float dx = layout.speakers[i].x - p.x;
            float dy = layout.speakers[i].y - p.y;
            float dz = layout.speakers[i].z - p.z;
            float d  = std::sqrt(dx*dx + dy*dy + dz*dz);
            d = std::max(d, 1e-4f);
            float w = std::pow(d, -rolloff_a);
            sum_sq2 += w * w;
        }
        float denom = std::sqrt(sum_sq2);
        for (int i = 0; i < N; ++i) {
            float dx = layout.speakers[i].x - p.x;
            float dy = layout.speakers[i].y - p.y;
            float dz = layout.speakers[i].z - p.z;
            float d  = std::sqrt(dx*dx + dy*dy + dz*dz);
            d = std::max(d, 1e-4f);
            float expected = std::pow(d, -rolloff_a) / denom;
            CHECK_NEAR(gains[i], expected, 1e-5f);
        }
    }

    if (failures == 0) std::printf("test_p3_dbap: ALL PASS\n");
    return failures;
}
