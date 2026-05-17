// vst3/tests/test_vst3_heartbeat_shutdown_fast.cpp
// v0.5.1 Q1 — Verify that stopHeartbeatTimer() returns in well under 1 s
// (the legacy sleep_for(1000ms) stall), thanks to the condvar wakeup path.
//
// Acceptance criterion: wall-clock time from setActive(false) to return < 200 ms.

#include "../SpatialEngineProcessor.hpp"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <cassert>
#include <chrono>
#include <cstdio>

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
    auto* proc = new spe::vst3::SpatialEngineProcessor();
    ASSERT_OK(proc != nullptr);

    IComponent* comp = nullptr;
    ASSERT_OK(proc->queryInterface(IComponent_iid,
              reinterpret_cast<void**>(&comp)) == kResultOk);
    ASSERT_OK(comp->initialize(nullptr) == kResultOk);

    IAudioProcessor* ap = nullptr;
    ASSERT_OK(proc->queryInterface(IAudioProcessor_iid,
              reinterpret_cast<void**>(&ap)) == kResultOk);

    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 64;
    setup.sampleRate         = 48000.0;
    ASSERT_OK(ap->setupProcessing(setup) == kResultOk);

    // Start heartbeat (setActive(true) calls startHeartbeatTimer internally).
    ASSERT_OK(comp->setActive(true) == kResultOk);

    // Let the heartbeat thread park in its wait_for().
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Measure teardown time — should wake immediately via condvar.
    const auto t0 = std::chrono::steady_clock::now();
    ASSERT_OK(comp->setActive(false) == kResultOk);
    const auto t1 = std::chrono::steady_clock::now();

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    std::fprintf(stderr, "heartbeat shutdown elapsed: %lld ms\n",
                 static_cast<long long>(elapsed_ms));

    // Must be well under the legacy 1-second stall.
    ASSERT_OK(elapsed_ms < 200);

    comp->terminate();
    proc->release();

    if (g_fail == 0) {
        std::fprintf(stderr, "PASS\n");
        return 0;
    }
    return 1;
}
