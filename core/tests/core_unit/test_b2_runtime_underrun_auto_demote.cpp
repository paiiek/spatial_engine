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

// v0.6 D-M2 — steady_clock vDSO probe + rt_timing_unavailable latch.
// On a fast CI runner the real probe in initialize() returns "fast", so
// we use injectSteadyClockSlowForTest() to drive the slow path
// deterministically. The contract: after the inject, isSteadyClockFast()
// is false, drainRtTimingUnavailablePending() returns true exactly once,
// and a subsequent initialize() re-probe must restore the fast path
// (since the CI machine actually IS fast).
int steadyClockSlowPathScenario(spe::output::BinauralMonitor& mon)
{
    // The first initialize() in main() ran the real probe on a fast CI
    // runner, so we start from the fast state.
    REQUIRE(mon.isSteadyClockFast());
    REQUIRE(!mon.drainRtTimingUnavailablePending());

    // Force the slow path.
    mon.injectSteadyClockSlowForTest();
    REQUIRE(!mon.isSteadyClockFast());
    REQUIRE(mon.drainRtTimingUnavailablePending());
    REQUIRE(!mon.drainRtTimingUnavailablePending());  // single-fire

    // Re-initialize. On a fast CI runner the real probe restores the
    // fast path. The latch must NOT be re-armed because the probe
    // succeeded (the latch is only set on slow result).
    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";
    cfg.sampleRate = kSampleRate;
    cfg.blockSize  = kBlock;
    REQUIRE(mon.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
    REQUIRE(mon.isSteadyClockFast());          // re-probe restored fast
    REQUIRE(!mon.drainRtTimingUnavailablePending());  // latch not re-armed

    std::puts("PASS steadyClockSlowPathScenario (D-M2 vDSO probe + latch)");
    return 0;
}

// v0.7 D-S3 — Scenario: max-ratio snapshot atomic equals the over-budget ratio
// of the most-recent strike at the demote latch.
//
// The AM-2 read-then-store accumulator keeps the highest ratio_x1000 seen
// across all over-budget blocks in the current strike run. At CAS-success
// (demote latch fires), runtime_demote_max_ratio_at_event_x1000_ is
// snapshotted from runtime_demote_max_ratio_x1000_. This scenario drives
// exactly kRuntimeDemoteStrikes blocks all at the SAME ratio (95% of
// deadline) and verifies the snapshot accessor returns that ratio.
int demoteSnapshotRatioScenario(spe::output::BinauralMonitor& mon)
{
    mon.clearRuntimeDemoteForTest();
    REQUIRE(!mon.isRuntimeDemoted());

    // 95% of deadline -> ratio_x1000 = 950 (over the 90% budget threshold).
    const long long elapsed = overBudgetNs(); // 95% of deadline
    const long long dl      = deadlineNs();
    const int expected_ratio = static_cast<int>(
        (static_cast<double>(elapsed) / static_cast<double>(dl)) * 1000.0);

    // Drive (kRuntimeDemoteStrikes - 1) over-budget blocks, then the final one.
    for (int i = 0; i < spe::output::BinauralMonitor::kRuntimeDemoteStrikes - 1; ++i) {
        mon.recordB2BlockTiming(kBlock, kSampleRate, elapsed);
    }
    REQUIRE(!mon.isRuntimeDemoted());

    mon.recordB2BlockTiming(kBlock, kSampleRate, elapsed);
    REQUIRE(mon.isRuntimeDemoted());

    // The snapshot must equal the ratio of the blocks that triggered the demote.
    REQUIRE(mon.snapshotRuntimeDemoteMaxRatioX1000() == expected_ratio);
    // Block size and sample rate snapshots must match what was passed in.
    REQUIRE(mon.snapshotRuntimeDemoteBlockSizeAtEvent() == kBlock);
    REQUIRE(mon.snapshotRuntimeDemoteSampleRateAtEvent() == static_cast<int>(kSampleRate));

    std::puts("PASS demoteSnapshotRatioScenario (D-S3 max-ratio snapshot)");
    return 0;
}

// v0.7 D-S3 — Scenario: snapshot persists across drainRuntimeDemotePending()
// (cleared only on D-S1 reset or initialize(), NOT by the IO drain).
//
// The heartbeat drain reads the snapshot AFTER consuming the pending latch.
// The snapshot must survive the drain so the IO thread can emit the diag
// packet even if it reads the snapshot after the latch has been cleared.
int demoteSnapshotPersistsAcrossDrainScenario(spe::output::BinauralMonitor& mon)
{
    mon.clearRuntimeDemoteForTest();
    REQUIRE(!mon.isRuntimeDemoted());

    // Drive a demote.
    mon.injectRuntimeUnderrunStrikesForTest();
    mon.recordB2BlockTiming(kBlock, kSampleRate, overBudgetNs());
    REQUIRE(mon.isRuntimeDemoted());

    // Drain the pending latch (simulates the heartbeat consuming the warning).
    REQUIRE(mon.drainRuntimeDemotePending());
    REQUIRE(!mon.drainRuntimeDemotePending()); // single-fire

    // Snapshot must still be readable — it is NOT cleared by drainRuntimeDemotePending().
    REQUIRE(mon.snapshotRuntimeDemoteBlockSizeAtEvent() == kBlock);
    REQUIRE(mon.snapshotRuntimeDemoteSampleRateAtEvent() == static_cast<int>(kSampleRate));
    REQUIRE(mon.snapshotRuntimeDemoteMaxRatioX1000() > 0);

    // A D-S1 user reset (simulated via clearRuntimeDemoteForTest + re-init)
    // must clear the snapshot. Use clearRuntimeDemoteForTest() which resets
    // all atomics to fresh-start values.
    mon.clearRuntimeDemoteForTest();
    REQUIRE(mon.snapshotRuntimeDemoteMaxRatioX1000()   == 0);
    REQUIRE(mon.snapshotRuntimeDemoteBlockSizeAtEvent() == 0);
    REQUIRE(mon.snapshotRuntimeDemoteSampleRateAtEvent() == 0);

    std::puts("PASS demoteSnapshotPersistsAcrossDrainScenario (D-S3 snapshot persistence)");
    return 0;
}

// v0.7 D-S2 — Item #2: block-size-aware effective_strikes scenarios.
//
// Scenario 1: block_size=32 / 48000 → block_seconds=0.000667s
//   effective_strikes = max(8, ceil(0.020/0.000667)) = max(8,30) = 30
//   Drive 29 over-budget calls → NOT demoted. 30th → demoted.
int blockSizeAwareSmallBlockScenario(spe::output::BinauralMonitor& mon)
{
    mon.clearRuntimeDemoteForTest();
    REQUIRE(!mon.isRuntimeDemoted());

    constexpr int   bs  = 32;
    constexpr float sr  = 48000.f;
    const long long dl  = static_cast<long long>(
        (static_cast<double>(bs) / static_cast<double>(sr)) * 1e9);
    const long long over = static_cast<long long>(dl * 0.95);

    // effective_strikes = ceil(0.020 / (32/48000)) = ceil(30) = 30
    constexpr int expected_eff = 30;

    for (int i = 0; i < expected_eff - 1; ++i) {
        mon.recordB2BlockTiming(bs, sr, over);
        REQUIRE(!mon.isRuntimeDemoted());
    }
    mon.recordB2BlockTiming(bs, sr, over);
    REQUIRE(mon.isRuntimeDemoted());

    std::puts("PASS blockSizeAwareSmallBlockScenario (D-S2, bs=32 -> eff=30)");
    return 0;
}

// Scenario 2: block_size=128 / 48000 → block_seconds=2.667ms
//   effective_strikes = max(8, ceil(7.5)) = max(8,8) = 8 (floor applied)
//   Behavior identical to pre-v0.7 — existing basicDemoteScenario covers this.
//   We add an explicit assertion here for documentation completeness.
int blockSizeAwareFloorScenario(spe::output::BinauralMonitor& mon)
{
    mon.clearRuntimeDemoteForTest();
    REQUIRE(!mon.isRuntimeDemoted());

    constexpr int   bs  = 128;
    constexpr float sr  = 48000.f;
    const long long dl  = static_cast<long long>(
        (static_cast<double>(bs) / static_cast<double>(sr)) * 1e9);
    const long long over = static_cast<long long>(dl * 0.95);

    // effective_strikes = max(8, ceil(0.020/0.002667)) = max(8, ceil(7.5)) = 8
    constexpr int expected_eff = 8;

    for (int i = 0; i < expected_eff - 1; ++i) {
        mon.recordB2BlockTiming(bs, sr, over);
        REQUIRE(!mon.isRuntimeDemoted());
    }
    mon.recordB2BlockTiming(bs, sr, over);
    REQUIRE(mon.isRuntimeDemoted());

    std::puts("PASS blockSizeAwareFloorScenario (D-S2, bs=128 -> eff=8 floor)");
    return 0;
}

// Scenario 3: block_size=1024 / 48000 → block_seconds=21.3ms
//   effective_strikes = max(8, ceil(0.020/0.02133)) = max(8, ceil(0.94)) = max(8,1) = 8 (floor)
int blockSizeAwareLargeBlockScenario(spe::output::BinauralMonitor& mon)
{
    mon.clearRuntimeDemoteForTest();
    REQUIRE(!mon.isRuntimeDemoted());

    constexpr int   bs  = 1024;
    constexpr float sr  = 48000.f;
    const long long dl  = static_cast<long long>(
        (static_cast<double>(bs) / static_cast<double>(sr)) * 1e9);
    const long long over = static_cast<long long>(dl * 0.95);

    // effective_strikes = max(8, ceil(0.020/0.02133)) = max(8,1) = 8
    constexpr int expected_eff = 8;

    for (int i = 0; i < expected_eff - 1; ++i) {
        mon.recordB2BlockTiming(bs, sr, over);
        REQUIRE(!mon.isRuntimeDemoted());
    }
    mon.recordB2BlockTiming(bs, sr, over);
    REQUIRE(mon.isRuntimeDemoted());

    std::puts("PASS blockSizeAwareLargeBlockScenario (D-S2, bs=1024 -> eff=8 floor)");
    return 0;
}

// v0.7 Item #8 — saturation cap option (b): verify that adding the saturation
// guard does NOT regress the existing auto-demote flow. Drive a full demote
// sequence on bs=128 and confirm isRuntimeDemoted() fires at the expected
// effective_strikes=8, not earlier or later. The guard fires only at
// kRuntimeDemoteStrikesSaturationCeiling=1000 which is never reached in normal
// operation — so this test proves the cap is inert on the happy path.
int saturationCapNonRegressionScenario(spe::output::BinauralMonitor& mon)
{
    mon.clearRuntimeDemoteForTest();
    REQUIRE(!mon.isRuntimeDemoted());

    constexpr int   bs  = 128;
    constexpr float sr  = 48000.f;
    const long long dl  = static_cast<long long>(
        (static_cast<double>(bs) / static_cast<double>(sr)) * 1e9);
    const long long over = static_cast<long long>(dl * 0.95);

    // 7 over-budget blocks: counter at 7, not yet at effective_strikes=8
    for (int i = 0; i < 7; ++i) {
        mon.recordB2BlockTiming(bs, sr, over);
        REQUIRE(!mon.isRuntimeDemoted());
    }
    // 8th triggers demote — saturation ceiling (1000) not involved
    mon.recordB2BlockTiming(bs, sr, over);
    REQUIRE(mon.isRuntimeDemoted());

    std::puts("PASS saturationCapNonRegressionScenario (Item #8 option-b)");
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

    if (basicDemoteScenario(mon)         != 0) return 1;
    if (resetStrikesScenario(mon)        != 0) return 1;
    if (injectionFastPathScenario(mon)   != 0) return 1;
    if (invalidArgsScenario(mon)         != 0) return 1;
    if (initializeResetsDemoteScenario() != 0) return 1;
    if (steadyClockSlowPathScenario(mon) != 0) return 1;
    if (demoteSnapshotRatioScenario(mon)             != 0) return 1;
    if (demoteSnapshotPersistsAcrossDrainScenario(mon) != 0) return 1;
    if (blockSizeAwareSmallBlockScenario(mon)          != 0) return 1;
    if (blockSizeAwareFloorScenario(mon)               != 0) return 1;
    if (blockSizeAwareLargeBlockScenario(mon)          != 0) return 1;
    if (saturationCapNonRegressionScenario(mon)        != 0) return 1;

    std::puts("PASS test_b2_runtime_underrun_auto_demote");
    return 0;
}
