// vst3/tests/test_vst3_audio_smoke.cpp
// Phase C C2 Step 1.6.5: audio smoke test.
// - In-process loader -> 16-frame block, 1 process() call -> output buffer finite check
// - 1000-iteration loop tagged with RT_ASSERT_NO_ALLOC guard (if enabled)
// - Must be compiled with -UNDEBUG (check_test_ndebug.sh enforces this)

#include "../SpatialEngineProcessor.hpp"
#include "../SpatialEngineController.hpp"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

#ifdef SPATIAL_ENGINE_RT_ASSERTS
#  include "util/NoAllocGuard.h"
#  define AUDIO_LOOP_GUARD() spe::util::NoAllocGuard _guard
#else
#  define AUDIO_LOOP_GUARD() (void)0
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

static int g_fail = 0;

#define ASSERT_OK(expr) \
    do { \
        if (!(expr)) { \
            std::fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
            ++g_fail; \
        } \
    } while(0)

int main()
{
    // Create processor directly (in-process, no dlopen)
    auto* proc = new spe::vst3::SpatialEngineProcessor();
    ASSERT_OK(proc != nullptr);

    IComponent* comp = nullptr;
    ASSERT_OK(proc->queryInterface(IComponent_iid, reinterpret_cast<void**>(&comp)) == kResultOk);
    ASSERT_OK(comp->initialize(nullptr) == kResultOk);

    IAudioProcessor* ap = nullptr;
    ASSERT_OK(proc->queryInterface(IAudioProcessor_iid, reinterpret_cast<void**>(&ap)) == kResultOk);

    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 16;
    setup.sampleRate         = 48000.0;
    ASSERT_OK(ap->setupProcessing(setup) == kResultOk);
    ASSERT_OK(comp->setActive(true) == kResultOk);

    // Buffers: stereo in + stereo out, 16 frames
    static constexpr int kFrames = 16;
    static constexpr int kCh = 2;

    std::array<float, kFrames> inL{}, inR{};
    std::array<float, kFrames> outL{}, outR{};

    float* inPtrs[kCh]  = { inL.data(), inR.data() };
    float* outPtrs[kCh] = { outL.data(), outR.data() };

    AudioBusBuffers inBus{};
    inBus.numChannels     = kCh;
    inBus.channelBuffers32 = inPtrs;

    AudioBusBuffers outBus{};
    outBus.numChannels     = kCh;
    outBus.channelBuffers32 = outPtrs;

    ProcessData pd{};
    pd.processMode        = kRealtime;
    pd.symbolicSampleSize = kSample32;
    pd.numSamples         = kFrames;
    pd.numInputs          = 1;
    pd.numOutputs         = 1;
    pd.inputs             = &inBus;
    pd.outputs            = &outBus;
    pd.inputParameterChanges  = nullptr;
    pd.outputParameterChanges = nullptr;

    // Single-block finite check
    ASSERT_OK(ap->process(pd) == kResultOk);
    for (int ch = 0; ch < kCh; ++ch)
        for (int s = 0; s < kFrames; ++s)
            ASSERT_OK(std::isfinite(outPtrs[ch][s]));

    // 1000-iteration loop (RT_ASSERT_NO_ALLOC if enabled)
    {
        AUDIO_LOOP_GUARD();
        for (int i = 0; i < 1000; ++i) {
            ASSERT_OK(ap->process(pd) == kResultOk);
        }
    }

    ASSERT_OK(comp->setActive(false) == kResultOk);
    ASSERT_OK(comp->terminate() == kResultOk);

    ap->release();
    comp->release();
    proc->release();

    if (g_fail == 0) {
        std::printf("audio smoke: all assertions passed.\n");
        return 0;
    }
    std::fprintf(stderr, "audio smoke: %d failure(s).\n", g_fail);
    return 1;
}
