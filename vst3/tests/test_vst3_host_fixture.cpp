// vst3/tests/test_vst3_host_fixture.cpp
// Phase C C2 AM-7: in-process host fixture.
// C2B postmortem A7: MockComponentHandler added (~30-50 LOC).
// Tests restartComponent(kParamValuesChanged) call after setComponentState.
//
// Total assertions: ~25 (9 original + A7 host fixture block).

#include "../SpatialEngineProcessor.hpp"
#include "../SpatialEngineController.hpp"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <cmath>

extern "C" Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory();

using namespace Steinberg;
using namespace Steinberg::Vst;

static int g_failures = 0;

#define ASSERT_OK(expr) \
    do { \
        if (!(expr)) { \
            std::fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures; \
        } else { \
            std::printf("PASS: %s\n", #expr); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// A7: MockComponentHandler — IComponentHandler stub that counts restartComponent calls.
// ---------------------------------------------------------------------------
class MockComponentHandler : public IComponentHandler
{
public:
    std::atomic<int> restart_count{0};
    int32            last_flags{0};
    virtual ~MockComponentHandler() = default;

    // FUnknown
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) SMTG_OVERRIDE
    {
        if (!obj) return kInvalidArgument;
        if (FUnknownPrivate::iidEqual(_iid, IComponentHandler_iid)) {
            addRef(); *obj = static_cast<IComponentHandler*>(this); return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(_iid, FUnknown_iid)) {
            addRef(); *obj = static_cast<IComponentHandler*>(this); return kResultOk;
        }
        *obj = nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef()  SMTG_OVERRIDE { return ++ref_; }
    uint32 PLUGIN_API release() SMTG_OVERRIDE
    { auto r=--ref_; if(r==0) delete this; return r; }

    // IComponentHandler
    tresult PLUGIN_API beginEdit(ParamID) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) SMTG_OVERRIDE
    {
        last_flags = flags;
        ++restart_count;
        return kResultOk;
    }

private:
    std::atomic<uint32> ref_{1};
};

int main()
{
    // ---- Assertion 1: GetPluginFactory() non-null ----
    IPluginFactory* factory = GetPluginFactory();
    ASSERT_OK(factory != nullptr);

    // ---- Assertion 2: getFactoryInfo returns kResultOk ----
    PFactoryInfo finfo{};
    ASSERT_OK(factory->getFactoryInfo(&finfo) == kResultOk);

    // ---- Assertion 3: countClasses() == 2 ----
    ASSERT_OK(factory->countClasses() == 2);

    // ---- Assertion 4: getClassInfo(0) Processor info OK ----
    PClassInfo cinfo0{};
    ASSERT_OK(factory->getClassInfo(0, &cinfo0) == kResultOk);
    ASSERT_OK(std::strcmp(cinfo0.name, "Spatial Engine") == 0);
    ASSERT_OK(memcmp(cinfo0.cid, spe::vst3::kSpatialEngineProcessorUID, sizeof(TUID)) == 0);

    // ---- Assertion 5: getClassInfo(1) Controller info OK ----
    PClassInfo cinfo1{};
    ASSERT_OK(factory->getClassInfo(1, &cinfo1) == kResultOk);
    ASSERT_OK(memcmp(cinfo1.cid, spe::vst3::kSpatialEngineControllerUID, sizeof(TUID)) == 0);

    // ---- Assertion 6: createInstance Processor -> non-null IComponent ----
    void* compRaw = nullptr;
    tresult r = factory->createInstance(
        reinterpret_cast<FIDString>(spe::vst3::kSpatialEngineProcessorUID),
        reinterpret_cast<FIDString>(IComponent_iid),
        &compRaw);
    ASSERT_OK(r == kResultOk);
    ASSERT_OK(compRaw != nullptr);

    // ---- Assertion 7: Component lifecycle ----
    IComponent* comp = static_cast<IComponent*>(compRaw);
    ASSERT_OK(comp->initialize(nullptr) == kResultOk);

    void* apRaw = nullptr;
    ASSERT_OK(comp->queryInterface(IAudioProcessor_iid, &apRaw) == kResultOk);
    ASSERT_OK(apRaw != nullptr);

    IAudioProcessor* ap = static_cast<IAudioProcessor*>(apRaw);

    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 64;
    setup.sampleRate         = 48000.0;
    ASSERT_OK(ap->setupProcessing(setup) == kResultOk);
    ASSERT_OK(comp->setActive(true) == kResultOk);

    // process with empty (zero-sample) block — must not crash
    ProcessData pd{};
    pd.processMode        = kRealtime;
    pd.symbolicSampleSize = kSample32;
    pd.numSamples         = 0;
    pd.numInputs          = 0;
    pd.numOutputs         = 0;
    pd.inputs             = nullptr;
    pd.outputs            = nullptr;
    pd.inputParameterChanges  = nullptr;
    pd.outputParameterChanges = nullptr;
    ASSERT_OK(ap->process(pd) == kResultOk);

    ASSERT_OK(comp->setActive(false) == kResultOk);
    ASSERT_OK(comp->terminate() == kResultOk);

    // ---- Assertion 8: IPluginFactory2 queryInterface -> kNotImplemented/kNoInterface ----
    void* factory2 = reinterpret_cast<void*>(0xDEADBEEF); // poison
    static constexpr TUID kIPluginFactory2_iid = INLINE_UID(0x0007B650, 0xF24B4C0B, 0xA464EDB9, 0xF00B2ABB);
    tresult qi2 = factory->queryInterface(kIPluginFactory2_iid, &factory2);
    ASSERT_OK(qi2 == kNotImplemented || qi2 == kNoInterface);
    ASSERT_OK(factory2 == nullptr);

    // ---- Assertion 9: ref count sanity ----
    uint32 apRef = ap->release();
    (void)apRef;
    uint32 compRef = comp->release();
    ASSERT_OK(compRef < 0x10000u);

    // ---- A7: MockComponentHandler + restartComponent verification ----
    // Create controller directly (in-process), wire up MockComponentHandler,
    // feed a v2 state stream, verify restartComponent(kParamValuesChanged) called once.
    {
        spe::vst3::SpatialEngineController ctrl;
        ASSERT_OK(ctrl.initialize(nullptr) == kResultOk);

        // Wire mock handler
        MockComponentHandler* mock = new MockComponentHandler();
        ASSERT_OK(ctrl.setComponentHandler(mock) == kResultOk);

        // Build a v2 state stream (7 params, bypass=1.0)
        static constexpr int kV2Bytes = 36;
        uint8_t v2buf[kV2Bytes]{};
        int32    magic   = 0x31455053; // 'SPE1'
        uint16_t version = 2, nparams = 7;
        std::memcpy(v2buf + 0, &magic,   4);
        std::memcpy(v2buf + 4, &version, 2);
        std::memcpy(v2buf + 6, &nparams, 2);
        float vals[7] = {0.1f,0.2f,0.35f,0.5f,0.75f,0.9f,1.0f};
        for (int i=0;i<7;++i) std::memcpy(v2buf+8+i*4,&vals[i],4);

        MemoryStream* ms = new MemoryStream();
        int32 written=0;
        ms->write(v2buf, kV2Bytes, &written);
        int64 res=0; ms->seek(0, IBStream::kIBSeekSet, &res);

        ASSERT_OK(ctrl.setComponentState(ms) == kResultOk);
        ms->release();

        // restartComponent must have been called exactly once with kParamValuesChanged
        ASSERT_OK(mock->restart_count.load() == 1);
        ASSERT_OK(mock->last_flags == kParamValuesChanged);

        // bypass param (id=6) must reflect 1.0
        ASSERT_OK(std::fabs(ctrl.getParamNormalized(6) - 1.0) < 1e-5);

        // param 0..5 restored correctly
        ASSERT_OK(std::fabs(ctrl.getParamNormalized(0) - 0.1) < 1e-5);
        ASSERT_OK(std::fabs(ctrl.getParamNormalized(5) - 0.9) < 1e-5);

        // Now test v1 stream: bypass should revert to 0, restart called again
        {
            uint8_t v1buf[32]{};
            int32 m2 = 0x31455053; uint16_t v1=1, n1=6;
            std::memcpy(v1buf+0,&m2,4); std::memcpy(v1buf+4,&v1,2); std::memcpy(v1buf+6,&n1,2);
            float v1vals[6]={0.2f,0.3f,0.4f,0.5f,0.6f,0.7f};
            for(int i=0;i<6;++i) std::memcpy(v1buf+8+i*4,&v1vals[i],4);
            MemoryStream* ms2=new MemoryStream(); ms2->write(v1buf,32,&written);
            ms2->seek(0,IBStream::kIBSeekSet,&res);
            ASSERT_OK(ctrl.setComponentState(ms2)==kResultOk);
            ms2->release();
        }
        ASSERT_OK(mock->restart_count.load() == 2);
        ASSERT_OK(std::fabs(ctrl.getParamNormalized(6) - 0.0) < 1e-5); // bypass reset

        ctrl.terminate();
        mock->release();
    }

    factory->release();

    if (g_failures == 0) {
        std::printf("\nAll assertions passed.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d assertion(s) failed.\n", g_failures);
    return 1;
}
