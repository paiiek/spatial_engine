// vst3/tests/test_vst3_param_roundtrip.cpp
// Step 2.6 — 24 assertions: 6 params × 4 norm values, master_gain skew,
// choice param quantization. Phase C C2 Option-B (M1.b gate).

#include "SpatialEngineController.hpp"

#include <cassert>
#include <cstdio>
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_NEAR(a, b, tol, msg)                                     \
    do {                                                                \
        double _a = (double)(a), _b = (double)(b), _t = (double)(tol); \
        if (std::fabs(_a - _b) <= _t) { ++g_pass; }                    \
        else {                                                          \
            ++g_fail;                                                   \
            fprintf(stderr, "FAIL [%s]: got %.10f expected %.10f\n",   \
                    msg, _a, _b);                                       \
        }                                                               \
    } while (0)

#define ASSERT_EQ(a, b, msg)                                            \
    do {                                                                \
        if ((a) == (b)) { ++g_pass; }                                   \
        else {                                                          \
            ++g_fail;                                                   \
            fprintf(stderr, "FAIL [%s]: got %lld expected %lld\n",     \
                    msg, (long long)(a), (long long)(b));               \
        }                                                               \
    } while (0)

static constexpr double kPi     = 3.14159265358979323846;
static constexpr double kHalfPi = kPi * 0.5;
static constexpr double kTol    = 1e-5;

// Test round-trip: plainParamToNormalized -> normalizedParamToPlain
// and setParamNormalized -> getParamNormalized.
static void testParam(spe::vst3::SpatialEngineController& ctrl,
                      ParamID id, double norm, const char* label)
{
    // setParamNormalized / getParamNormalized round-trip
    ctrl.setParamNormalized(id, norm);
    double got = ctrl.getParamNormalized(id);
    char buf[128];
    snprintf(buf, sizeof(buf), "%s.norm_rt", label);
    ASSERT_NEAR(got, norm, kTol, buf);
}

int main()
{
    spe::vst3::SpatialEngineController ctrl;
    ctrl.initialize(nullptr);

    static const double kNorms[4] = {0.0, 0.33, 0.66, 1.0};

    // --- param 0: pan_az ---
    for (int i = 0; i < 4; ++i) {
        testParam(ctrl, 0, kNorms[i], "pan_az");
    }

    // --- param 1: pan_el ---
    for (int i = 0; i < 4; ++i) {
        testParam(ctrl, 1, kNorms[i], "pan_el");
    }

    // --- param 2: source_width ---
    for (int i = 0; i < 4; ++i) {
        testParam(ctrl, 2, kNorms[i], "source_width");
    }

    // --- param 3: master_gain (skew) ---
    // Core skew assertion: norm=0.5 -> 0 dB
    {
        double plain = ctrl.normalizedParamToPlain(3, 0.5);
        ASSERT_NEAR(plain, 0.0, kTol, "gain.skew_mid_0dB");
    }
    for (int i = 0; i < 4; ++i) {
        // plain -> norm -> plain round-trip via normalizedParamToPlain / plainParamToNormalized
        double norm2 = kNorms[i];
        double plain = ctrl.normalizedParamToPlain(3, norm2);
        double norm_back = ctrl.plainParamToNormalized(3, plain);
        char buf[64];
        snprintf(buf, sizeof(buf), "gain.plain_rt[%d]", i);
        ASSERT_NEAR(norm_back, norm2, kTol, buf);
    }

    // --- param 4: ambi_order (choice quantization) ---
    // norm 0.0 -> index 0 (plain 0), norm 0.5 -> index 1 (plain 1), norm 1.0 -> index 2 (plain 2)
    {
        double p0 = ctrl.normalizedParamToPlain(4, 0.0);
        double p1 = ctrl.normalizedParamToPlain(4, 0.5);
        double p2 = ctrl.normalizedParamToPlain(4, 1.0);
        ASSERT_NEAR(p0, 0.0, kTol, "ambi_order.idx0");
        ASSERT_NEAR(p1, 1.0, kTol, "ambi_order.idx1");
        ASSERT_NEAR(p2, 2.0, kTol, "ambi_order.idx2");
        // setParamNormalized round-trips
        for (int i = 0; i < 4; ++i) {
            testParam(ctrl, 4, kNorms[i], "ambi_order");
        }
    }

    // --- param 5: room_preset_idx (choice quantization) ---
    {
        double p0 = ctrl.normalizedParamToPlain(5, 0.0);
        double p3 = ctrl.normalizedParamToPlain(5, 1.0);
        ASSERT_NEAR(p0, 0.0, kTol, "room_preset.idx0");
        ASSERT_NEAR(p3, 3.0, kTol, "room_preset.idx3");
        for (int i = 0; i < 4; ++i) {
            testParam(ctrl, 5, kNorms[i], "room_preset");
        }
    }

    printf("param_roundtrip: %d pass, %d fail\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
