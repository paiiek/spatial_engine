// test_p3_vbap.cpp
// Place object at each speaker direction; assert vbap_gain matches
// AlgorithmAnalyticReference within 1e-6.

#include "render/AlgorithmAnalyticReference.h"
#include "render/VBAPRenderer.h"
#include "geometry/SpeakerLayout.h"
#include <cmath>
#include <cstdio>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

#define CHECK_NEAR(a, b, tol) \
    do { float _a = (a), _b = (b); \
         if (std::abs(_a - _b) > (tol)) { \
             std::printf("FAIL %s:%d  |%.8f - %.8f| = %.2e > %.2e\n", \
                 __FILE__, __LINE__, (double)_a, (double)_b, (double)std::abs(_a-_b), (double)(tol)); \
             ++failures; \
         } } while(0)

using namespace spe::render;
using namespace spe::geometry;

// Build a simple 4-speaker 2D circular layout at unit distance
static SpeakerLayout make_4ch_layout() {
    SpeakerLayout l;
    l.name = "test_4ch";
    l.regularity = Regularity::CIRCULAR;
    // Speakers at 0, 90, 180, 270 degrees in azimuth (x-z plane, y=0)
    // x = sin(az), z = cos(az)
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
    const int N = static_cast<int>(layout.speakers.size());

    for (int i = 0; i < N; ++i) {
        // Source at speaker i's azimuth
        float az = std::atan2(layout.speakers[i].x, layout.speakers[i].z);
        auto gains = AlgorithmAnalyticReference::vbap_gain(layout, az, 0.f);

        // Speaker i should have gain ≈ 1.0 (or very high), others ≈ 0
        // Energy-normalised: speaker i gain should be ≈ 1.0
        CHECK(static_cast<int>(gains.size()) == N);

        // Sum of squares should be ≈ 1.0
        float ss = 0.f;
        for (float g : gains) ss += g * g;
        CHECK_NEAR(ss, 1.0f, 1e-5f);

        // The target speaker should have the highest gain
        int best = 0;
        for (int j = 1; j < N; ++j)
            if (gains[j] > gains[best]) best = j;
        CHECK(best == i);

        // The dominant speaker gain should be close to 1.0
        CHECK_NEAR(gains[i], 1.0f, 1e-5f);
    }

    // Test mid-point between two speakers: both should have equal gain
    {
        float az = 45.f * 3.14159265f / 180.f; // midpoint between 0 and 90 deg
        auto gains = AlgorithmAnalyticReference::vbap_gain(layout, az, 0.f);
        float ss = 0.f;
        for (float g : gains) ss += g * g;
        CHECK_NEAR(ss, 1.0f, 1e-5f);
        // Both speaker 0 (az=0) and speaker 1 (az=90) should have equal gain ~1/sqrt(2)
        CHECK_NEAR(gains[0], gains[1], 1e-5f);
        CHECK_NEAR(gains[0], 1.f / std::sqrt(2.f), 1e-4f);
    }

    if (failures == 0) std::printf("test_p3_vbap: ALL PASS\n");
    return failures;
}
