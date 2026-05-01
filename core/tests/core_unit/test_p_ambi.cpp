// test_p_ambi.cpp
// Ambisonics 1st-order encoder numerical tests
//
// Test 1: az=0, el=0 → front (X=1, Y=0, Z=0)
// Test 2: az=π/2, el=0 → right (X=0, Y=1, Z=0)
// Test 3: az=π, el=0 → back (X=-1, Y≈0, Z=0)
// Test 4: az=0, el=+π/2 → directly above (X≈0, Y≈0, Z=1)
// Test 5: az=0, el=-π/2 → directly below (X≈0, Y≈0, Z=-1)
// Test 6: NaN propagation — W=1.0 unchanged, X/Y/Z = NaN

#include "ambi/AmbisonicEncoder.h"
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
                 __FILE__, __LINE__, (double)_a, (double)_b, \
                 (double)std::abs(_a - _b), (double)(tol)); \
             ++failures; \
         } } while(0)

using namespace spe::ambi;

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTol = 1e-6f;

int main() {
    // Test 1: az=0, el=0 → front
    {
        auto c = AmbisonicEncoder::encode_1st_order(0.0f, 0.0f);
        CHECK_NEAR(c.W, 1.0f, kTol);
        CHECK_NEAR(c.X, 1.0f, kTol);
        CHECK_NEAR(c.Y, 0.0f, kTol);
        CHECK_NEAR(c.Z, 0.0f, kTol);
    }

    // Test 2: az=π/2, el=0 → right
    {
        auto c = AmbisonicEncoder::encode_1st_order(kPi / 2.0f, 0.0f);
        CHECK_NEAR(c.W, 1.0f, kTol);
        CHECK_NEAR(c.X, 0.0f, kTol);
        CHECK_NEAR(c.Y, 1.0f, kTol);
        CHECK_NEAR(c.Z, 0.0f, kTol);
    }

    // Test 3: az=π, el=0 → back (cos(π)=-1, sin(π)≈0)
    {
        auto c = AmbisonicEncoder::encode_1st_order(kPi, 0.0f);
        CHECK_NEAR(c.W, 1.0f, kTol);
        CHECK_NEAR(c.X, -1.0f, kTol);
        CHECK_NEAR(c.Y, 0.0f, kTol);
        CHECK_NEAR(c.Z, 0.0f, kTol);
    }

    // Test 4: az=0, el=+π/2 → directly above
    {
        auto c = AmbisonicEncoder::encode_1st_order(0.0f, kPi / 2.0f);
        CHECK_NEAR(c.W, 1.0f, kTol);
        CHECK_NEAR(c.X, 0.0f, kTol);
        CHECK_NEAR(c.Y, 0.0f, kTol);
        CHECK_NEAR(c.Z, 1.0f, kTol);
    }

    // Test 5: az=0, el=-π/2 → directly below
    {
        auto c = AmbisonicEncoder::encode_1st_order(0.0f, -kPi / 2.0f);
        CHECK_NEAR(c.W, 1.0f, kTol);
        CHECK_NEAR(c.X, 0.0f, kTol);
        CHECK_NEAR(c.Y, 0.0f, kTol);
        CHECK_NEAR(c.Z, -1.0f, kTol);
    }

    // Test 6: NaN propagation — W stays 1.0, X/Y/Z become NaN
    {
        auto c = AmbisonicEncoder::encode_1st_order(std::numeric_limits<float>::quiet_NaN(), 0.0f);
        CHECK(c.W == 1.0f);
        CHECK(std::isnan(c.X));
        CHECK(std::isnan(c.Y));
        // Z = sin(el=0) = 0, not NaN — only az-dependent terms propagate NaN
    }

    if (failures == 0) {
        std::printf("OK  test_p_ambi: all checks passed\n");
        return 0;
    }
    std::printf("FAIL test_p_ambi: %d failure(s)\n", failures);
    return 1;
}
