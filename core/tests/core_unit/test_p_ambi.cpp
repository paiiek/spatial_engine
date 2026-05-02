// test_p_ambi.cpp
// Ambisonics 1st/2nd/3rd-order encoder numerical tests
//
// Test 1:  az=0, el=0 → front (X=1, Y=0, Z=0)
// Test 2:  az=π/2, el=0 → right (X=0, Y=1, Z=0)
// Test 3:  az=π, el=0 → back (X=-1, Y≈0, Z=0)
// Test 4:  az=0, el=+π/2 → directly above (X≈0, Y≈0, Z=1)
// Test 5:  az=0, el=-π/2 → directly below (X≈0, Y≈0, Z=-1)
// Test 6:  NaN propagation — W=1.0 unchanged, X/Y/Z = NaN
// Test 7:  encode_2nd_order(0, 0)
// Test 8:  encode_2nd_order(π/2, 0)
// Test 9:  encode_3rd_order(0, 0)
// Test 10: 2nd-order NaN propagation
// Test 11: 3rd-order NaN propagation
// Test 12: ACN vs legacy 1st-order cross-check

#include "ambi/AmbisonicEncoder.h"
#include <cmath>
#include <cstdio>
#include <limits>

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
        CHECK(!std::isnan(c.Z));  // Z = sin(el=0) = 0, not az-dependent
    }

    constexpr float kSqrt3_2_f = 0.8660254037844386f;  // sqrt(3)/2
    constexpr float kSqrt6_4_f = 0.6123724356957945f;  // sqrt(6)/4
    constexpr float kSqrt10_4_f = 0.7905694150420949f; // sqrt(10)/4

    // Test 7: encode_2nd_order(0, 0) — az=0, el=0
    {
        auto c = AmbisonicEncoder::encode_2nd_order(0.0f, 0.0f);
        CHECK_NEAR(c[0], 1.0f, kTol);
        CHECK_NEAR(c[1], 0.0f, kTol);
        CHECK_NEAR(c[2], 0.0f, kTol);
        CHECK_NEAR(c[3], 1.0f, kTol);
        CHECK_NEAR(c[4], 0.0f, kTol);
        CHECK_NEAR(c[5], 0.0f, kTol);
        CHECK_NEAR(c[6], -0.5f, kTol);
        CHECK_NEAR(c[7], 0.0f, kTol);
        CHECK_NEAR(c[8], kSqrt3_2_f, kTol);
    }

    // Test 8: encode_2nd_order(π/2, 0) — az=π/2, el=0
    {
        auto c = AmbisonicEncoder::encode_2nd_order(kPi / 2.0f, 0.0f);
        CHECK_NEAR(c[0], 1.0f, kTol);
        CHECK_NEAR(c[1], 1.0f, kTol);
        CHECK_NEAR(c[2], 0.0f, kTol);
        CHECK_NEAR(c[3], 0.0f, kTol);
        CHECK_NEAR(c[4], 0.0f, kTol);   // sin(π) ≈ 0
        CHECK_NEAR(c[5], 0.0f, kTol);
        CHECK_NEAR(c[6], -0.5f, kTol);
        CHECK_NEAR(c[7], 0.0f, kTol);
        CHECK_NEAR(c[8], -kSqrt3_2_f, kTol);  // cos(π) = -1
    }

    // Test 9: encode_3rd_order(0, 0) — az=0, el=0
    {
        auto c = AmbisonicEncoder::encode_3rd_order(0.0f, 0.0f);
        CHECK_NEAR(c[0], 1.0f, kTol);
        CHECK_NEAR(c[1], 0.0f, kTol);
        CHECK_NEAR(c[2], 0.0f, kTol);
        CHECK_NEAR(c[3], 1.0f, kTol);
        CHECK_NEAR(c[4], 0.0f, kTol);
        CHECK_NEAR(c[5], 0.0f, kTol);
        CHECK_NEAR(c[6], -0.5f, kTol);
        CHECK_NEAR(c[7], 0.0f, kTol);
        CHECK_NEAR(c[8], kSqrt3_2_f, kTol);
        CHECK_NEAR(c[9],  0.0f, kTol);
        CHECK_NEAR(c[10], 0.0f, kTol);
        CHECK_NEAR(c[11], 0.0f, kTol);
        CHECK_NEAR(c[12], 0.0f, kTol);
        CHECK_NEAR(c[13], -kSqrt6_4_f, kTol);  // (sqrt(6)/4)·1·(-1)·1
        CHECK_NEAR(c[14], 0.0f, kTol);
        CHECK_NEAR(c[15], kSqrt10_4_f, kTol);  // (sqrt(10)/4)·1·1
    }

    // Test 10: 2nd-order NaN propagation
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        auto c = AmbisonicEncoder::encode_2nd_order(nan, 0.0f);
        // m=0 channels (no az dependency): finite
        CHECK(c[0] == 1.0f);
        CHECK(!std::isnan(c[2]));
        CHECK_NEAR(c[2], 0.0f, kTol);
        CHECK(!std::isnan(c[6]));
        CHECK_NEAR(c[6], -0.5f, kTol);
        // m≠0 channels: NaN
        CHECK(std::isnan(c[1]));
        CHECK(std::isnan(c[3]));
        CHECK(std::isnan(c[4]));
        CHECK(std::isnan(c[5]));
        CHECK(std::isnan(c[7]));
        CHECK(std::isnan(c[8]));
    }

    // Test 11: 3rd-order NaN propagation
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        auto c = AmbisonicEncoder::encode_3rd_order(nan, 0.0f);
        // m=0 channels finite
        CHECK(c[0] == 1.0f);
        CHECK(!std::isnan(c[2]));
        CHECK_NEAR(c[2], 0.0f, kTol);
        CHECK(!std::isnan(c[6]));
        CHECK_NEAR(c[6], -0.5f, kTol);
        CHECK(!std::isnan(c[12]));
        CHECK_NEAR(c[12], 0.0f, kTol);
        // m≠0 channels NaN
        CHECK(std::isnan(c[1]));
        CHECK(std::isnan(c[3]));
        CHECK(std::isnan(c[4]));
        CHECK(std::isnan(c[5]));
        CHECK(std::isnan(c[7]));
        CHECK(std::isnan(c[8]));
        CHECK(std::isnan(c[9]));
        CHECK(std::isnan(c[10]));
        CHECK(std::isnan(c[11]));
        CHECK(std::isnan(c[13]));
        CHECK(std::isnan(c[14]));
        CHECK(std::isnan(c[15]));
    }

    // Test 12: ACN vs legacy 1st-order cross-check at az=0.5, el=0.3
    {
        const float az = 0.5f, el = 0.3f;
        auto legacy = AmbisonicEncoder::encode_1st_order(az, el);
        auto c2 = AmbisonicEncoder::encode_2nd_order(az, el);
        CHECK_NEAR(c2[0], legacy.W, kTol);
        CHECK_NEAR(c2[1], legacy.Y, kTol);
        CHECK_NEAR(c2[2], legacy.Z, kTol);
        CHECK_NEAR(c2[3], legacy.X, kTol);
        auto c3 = AmbisonicEncoder::encode_3rd_order(az, el);
        CHECK_NEAR(c3[0], legacy.W, kTol);
        CHECK_NEAR(c3[1], legacy.Y, kTol);
        CHECK_NEAR(c3[2], legacy.Z, kTol);
        CHECK_NEAR(c3[3], legacy.X, kTol);
    }

    if (failures == 0) {
        std::printf("OK  test_p_ambi: all checks passed\n");
        return 0;
    }
    std::printf("FAIL test_p_ambi: %d failure(s)\n", failures);
    return 1;
}
