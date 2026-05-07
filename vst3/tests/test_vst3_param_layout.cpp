// vst3/tests/test_vst3_param_layout.cpp
// Step 2.6 — 10 assertions: controller param count, ParameterInfo fields,
// setParamNormalized + getParamNormalized round-trip.
// Phase C C2 Option-B (M1.b gate).

#include "SpatialEngineController.hpp"

#include <cassert>
#include <cstdio>
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_EQ(a, b, msg)                                           \
    do {                                                               \
        if ((a) == (b)) { ++g_pass; }                                  \
        else {                                                         \
            ++g_fail;                                                  \
            fprintf(stderr, "FAIL [%s]: got %lld expected %lld\n",    \
                    msg, (long long)(a), (long long)(b));              \
        }                                                              \
    } while (0)

#define ASSERT_NEAR(a, b, tol, msg)                                    \
    do {                                                               \
        double _a = (double)(a), _b = (double)(b), _t = (double)(tol);\
        if (std::fabs(_a - _b) <= _t) { ++g_pass; }                   \
        else {                                                         \
            ++g_fail;                                                  \
            fprintf(stderr, "FAIL [%s]: got %.8f expected %.8f\n",    \
                    msg, _a, _b);                                      \
        }                                                              \
    } while (0)

int main()
{
    spe::vst3::SpatialEngineController ctrl;

    // Assertion 1: initialize returns kResultOk
    tresult r = ctrl.initialize(nullptr);
    ASSERT_EQ(r, kResultOk, "initialize");

    // Assertion 2: getParameterCount == 6
    ASSERT_EQ(ctrl.getParameterCount(), 6, "paramCount");

    // Assertions 3-8: ParameterInfo for each param
    // param 0: pan_az
    {
        ParameterInfo info{};
        ctrl.getParameterInfo(0, info);
        ASSERT_EQ(info.id,                       0,                    "pan_az.id");
        ASSERT_NEAR(info.defaultNormalizedValue,  0.5,       1e-5,     "pan_az.default");
        ASSERT_EQ(info.stepCount,                0,                    "pan_az.stepCount");
        ASSERT_EQ((info.flags & ParameterInfo::kCanAutomate) != 0, 1,  "pan_az.kCanAutomate");
    }

    // param 3: master_gain (mid-skew — 0dB default ↔ norm 0.5 by definition)
    {
        ParameterInfo info{};
        ctrl.getParameterInfo(3, info);
        ASSERT_EQ(info.id,     3,                                       "gain.id");
        // mid-skew at 0dB: norm 0.5 ↔ plain 0dB (NormalisableRange::setSkewForCentre).
        // gainPlainToNorm(0.0) returns 0.5 exactly.
        ASSERT_NEAR(info.defaultNormalizedValue, 0.5, 1e-5,             "gain.default_skewed");
    }

    // param 4: ambi_order — stepCount=2 (3 choices)
    {
        ParameterInfo info{};
        ctrl.getParameterInfo(4, info);
        ASSERT_EQ(info.id,        4,   "ambi_order.id");
        ASSERT_EQ(info.stepCount, 2,   "ambi_order.stepCount");
        int isList = (info.flags & ParameterInfo::kIsList) != 0 ? 1 : 0;
        ASSERT_EQ(isList, 1,           "ambi_order.kIsList");
    }

    // param 5: room_preset_idx — stepCount=3 (4 choices)
    {
        ParameterInfo info{};
        ctrl.getParameterInfo(5, info);
        ASSERT_EQ(info.id,        5,   "room_preset.id");
        ASSERT_EQ(info.stepCount, 3,   "room_preset.stepCount");
    }

    // Assertion 9: setParamNormalized + getParamNormalized round-trip (pan_az)
    ctrl.setParamNormalized(0, 0.75);
    ASSERT_NEAR(ctrl.getParamNormalized(0), 0.75, 1e-5, "pan_az.roundtrip");

    // Assertion 10: master_gain round-trip at 0dB (norm = gainPlainToNorm(0))
    {
        ParameterInfo info{};
        ctrl.getParameterInfo(3, info);
        ctrl.setParamNormalized(3, info.defaultNormalizedValue);
        ASSERT_NEAR(ctrl.getParamNormalized(3), info.defaultNormalizedValue, 1e-5, "gain.roundtrip");
    }

    printf("param_layout: %d pass, %d fail\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
