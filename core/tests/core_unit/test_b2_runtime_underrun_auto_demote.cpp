// test_b2_runtime_underrun_auto_demote.cpp
//
// v0.6 #5 — runtime sticky-underrun auto-demote. When the B2 fan-out's
// wall-clock cost exceeds kRuntimeDemoteBudgetFraction × block deadline
// for kRuntimeDemoteStrikes consecutive blocks, BinauralMonitor must:
//
//   1. Set runtime_demoted_ (sticky for the monitor's lifetime).
//   2. Clamp effective_mode_ to Direct.
//   3. Arm the runtime_demote_warning_pending_ latch exactly once.
//
// Test approach: drive recordB2BlockTiming() directly with synthetic elapsed
// times to make the trigger deterministic (the real B2 path on CI runners is
// far faster than the deadline so we can't naturally provoke an underrun).

#include "core/Constants.h"
#include "output_backend/BinauralMonitor.h"

#include <cstdio>
#include <string>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

#define REQUIRE(cond)                                                    \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL %s (line %d)\n", #cond, __LINE__); \
            return 1;                                                     \
        }                                                                 \
    } while (0)

namespace {

constexpr int   kBlock      = 128;
constexpr float kSampleRate = 48000.f;

// Block deadline in nanoseconds for the test fixture (128 samples @ 48 kHz
// = 2666.67 µs).
constexpr long long deadlineNs() {
    return static_cast<long long>(
        (static_cast<double>(kBlock) /
         static_cast<double>(kSampleRate)) * 1e9);
}

// 95% of deadline — definitely over the 90% budget threshold.
constexpr long long overBudgetNs() {
    return static_cast<long long>(deadlineNs() * 0.95);
}

// 50% of deadline — comfortably under budget.
constexpr long long underBudgetNs() {
    return static_cast<long long>(deadlineNs() * 0.50);
}

int basicDemoteScenario(spe::output::BinauralMonitor& mon)
{
    REQUIRE(!mon.isRuntimeDemoted());

    // First (kRuntimeDemoteStrikes - 1) over-budget blocks must NOT demote.
    for (int i = 0; i < spe::output::BinauralMonitor::kRuntimeDemoteStrikes - 1; ++i) {
        mon.recordB2BlockTiming(kBlock, kSampleRate, overBudgetNs());
        REQUIRE(!mon.isRuntimeDemoted());
    }

    // Kth over-budget block triggers demote + arms warning.
    mon.recordB2BlockTiming(kBlock, kSampleRate, overBudgetNs());
    REQUIRE(mon.isRuntimeDemoted());

    // Warning latch fires exactly once.
    REQUIRE(mon.drainRuntimeDemotePending());
    REQUIRE(!mon.drainRuntimeDemotePending());

    // Effective mode is clamped to Direct.
    REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::Direct);

    // Subsequent good blocks must NOT clear the sticky demote.
    mon.recordB2BlockTiming(kBlock, kSampleRate, underBudgetNs());
    REQUIRE(mon.isRuntimeDemoted());

    // Subsequent over-budget blocks must NOT re-arm the warning latch
    // (single-event semantics).
    mon.recordB2BlockTiming(kBlock, kSampleRate, overBudgetNs());
    REQUIRE(!mon.drainRuntimeDemotePending());

    std::puts("PASS basicDemoteScenario");
    return 0;
}

int resetStrikesScenario(spe::output::BinauralMonitor& mon)
{
    mon.clearRuntimeDemoteForTest();
    REQUIRE(!mon.isRuntimeDemoted());

    // Alternating pattern: over, over, over, under, over, over, ...
    // Strike counter resets on every under-budget block — must NEVER reach
    // kRuntimeDemoteStrikes if a good block intervenes within the window.
    for (int cycle = 0; cycle < 50; ++cycle) {
        for (int i = 0;
             i < spe::output::BinauralMonitor::kRuntimeDemoteStrikes - 1;
             ++i)
        {
            mon.recordB2BlockTiming(kBlock, kSampleRate, overBudgetNs());
        }
        // Good block resets the streak.
        mon.recordB2BlockTiming(kBlock, kSampleRate, underBudgetNs());
        REQUIRE(!mon.isRuntimeDemoted());
    }

    std::puts("PASS resetStrikesScenario (50 cycles, no spurious demote)");
    return 0;
}

int injectionFastPathScenario(spe::output::BinauralMonitor& mon)
{
    mon.clearRuntimeDemoteForTest();
    REQUIRE(!mon.isRuntimeDemoted());

    // Test hook bumps strike count to (kRuntimeDemoteStrikes - 1).
    mon.injectRuntimeUnderrunStrikesForTest();
    REQUIRE(!mon.isRuntimeDemoted());

    // Single over-budget call now triggers the demote.
    mon.recordB2BlockTiming(kBlock, kSampleRate, overBudgetNs());
    REQUIRE(mon.isRuntimeDemoted());
    REQUIRE(mon.drainRuntimeDemotePending());

    std::puts("PASS injectionFastPathScenario");
    return 0;
}

int invalidArgsScenario(spe::output::BinauralMonitor& mon)
{
    mon.clearRuntimeDemoteForTest();

    // Invalid block_size / sample_rate must be silently ignored.
    mon.recordB2BlockTiming(0, kSampleRate, overBudgetNs());
    mon.recordB2BlockTiming(kBlock, 0.f, overBudgetNs());
    mon.recordB2BlockTiming(-1, kSampleRate, overBudgetNs());
    REQUIRE(!mon.isRuntimeDemoted());

    std::puts("PASS invalidArgsScenario");
    return 0;
}

// v0.6 D-M1 — regression gate for the silent bug the Architect retroactive
// review caught: initialize() (= the production "next prepareToPlay" reset
// path) must clear runtime_demote_strikes_, runtime_demoted_, and
// runtime_demote_warning_pending_. Pre-fix, the documented sticky-until-
// next-prepareToPlay contract was silently violated because the test-only
// clearRuntimeDemoteForTest() was the only path that actually cleared the
// state.
//
// Test approach: drive a deterministic demote via injectRuntimeUnderrunStrikesForTest
// + one over-budget block, then re-initialize() the monitor and verify the
// 3 atomics are all reset to fresh-start values. The post-init state must
// match the post-clearRuntimeDemoteForTest() state byte-for-byte (modulo
// effective_mode_ which initialize() does not own).
int initializeResetsDemoteScenario()
{
    // Use a fresh monitor so we exercise initialize() in its primary role
    // (first-time setup) and then again in its secondary role (re-init
    // after demote — the documented prepareToPlay sticky-reset path).
    spe::output::BinauralMonitor mon;
    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";
    cfg.sampleRate = kSampleRate;
    cfg.blockSize  = kBlock;
    REQUIRE(mon.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
    REQUIRE(!mon.isRuntimeDemoted());

    // Drive a demote.
    mon.injectRuntimeUnderrunStrikesForTest();
    mon.recordB2BlockTiming(kBlock, kSampleRate, overBudgetNs());
    REQUIRE(mon.isRuntimeDemoted());
    // Note: we deliberately do NOT drain the warning latch here. Pre-D-M1
    // initialize() would have left the latch armed too — a second silent
    // contract violation. Post-fix initialize() must clear the latch
    // alongside the demoted flag.

    // Re-initialize() — the production "next prepareToPlay" path.
    REQUIRE(mon.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);

    // All three atomics must be at fresh-start values.
    REQUIRE(!mon.isRuntimeDemoted());
    REQUIRE(!mon.drainRuntimeDemotePending());   // latch reset, not stuck-armed

    // After reset, the demote progression must be repeatable from scratch.
    // (Belt-and-suspenders: confirm the strike counter is at 0, not at
    // some lingering non-zero state that would let a single over-budget
    // call re-trigger the demote.)
    mon.recordB2BlockTiming(kBlock, kSampleRate, overBudgetNs());
    REQUIRE(!mon.isRuntimeDemoted());  // one strike of one is not a demote

    std::puts("PASS initializeResetsDemoteScenario (D-M1 regression gate)");
    return 0;
}

} // namespace

int main()
{
    spe::output::BinauralMonitor mon;
    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";
    cfg.sampleRate = kSampleRate;
    cfg.blockSize  = kBlock;
    REQUIRE(mon.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);

    if (basicDemoteScenario(mon)        != 0) return 1;
    if (resetStrikesScenario(mon)       != 0) return 1;
    if (injectionFastPathScenario(mon)  != 0) return 1;
    if (invalidArgsScenario(mon)        != 0) return 1;
    if (initializeResetsDemoteScenario() != 0) return 1;

    std::puts("PASS test_b2_runtime_underrun_auto_demote");
    return 0;
}
