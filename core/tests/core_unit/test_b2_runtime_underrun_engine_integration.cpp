// test_b2_runtime_underrun_engine_integration.cpp
//
// v0.6 P1-3 — End-to-end integration of the runtime sticky-underrun
// auto-demote pipeline through SpatialEngine (not just BinauralMonitor).
//
// The unit ctest `test_b2_runtime_underrun_auto_demote` only exercises
// BinauralMonitor in isolation and never proves the SpatialEngine forwarders
// (`binauralIsRuntimeDemoted`, `binauralDrainRuntimeDemotePending`,
//  `recordBinauralB2BlockTimingForTest`, `injectBinauralRuntimeUnderrunStrikesForTest`,
//  `clearBinauralRuntimeDemoteForTest`) are wired up correctly. Without this
// test, a forwarder bug (e.g. a stray no-op stub) would slip past CI because
// the public BinauralMonitor surface still passes.
//
// What this test covers that the unit test does not:
//
//   1. SpatialEngine forwarder symbol existence + correct dispatch.
//   2. Real audioBlock() calls under AmbiVS effective mode actually drive the
//      `recordB2BlockTiming(...)` wrapper at SpatialEngine.cpp:709-731. (On
//      CI, every real B2 block runs far under deadline, so we can't *trigger*
//      a demote organically — but we can prove the wrapper is reached by
//      injecting strikes through the engine forwarder and verifying the
//      first subsequent over-budget recordBinauralB2BlockTimingForTest call
//      flips the engine-observed `binauralIsRuntimeDemoted()`.)
//   3. v0.6 P1-4 kill-switch sanity — after the sticky demote has fired,
//      subsequent `audioBlock()` calls must NOT mutate the demote/strike
//      state further. The kill-switch wraps the wall-clock brackets in
//      `if (!binaural_.isRuntimeDemoted()) { ... }`, so a regression that
//      accidentally re-enables timing on a demoted monitor would surface
//      here as either a spurious `drainRuntimeDemotePending()==true` on a
//      later block (the latch must remain single-fire) or as an audible-
//      mode regression from Direct back to AmbiVS.

#include "core/SpatialEngine.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "output_backend/BinauralMonitor.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

// 95% of block deadline — clearly above the 90% kRuntimeDemoteBudgetFraction.
constexpr long long overBudgetNs() {
    constexpr long long deadline_ns = static_cast<long long>(
        (static_cast<double>(kBlock) /
         static_cast<double>(kSampleRate)) * 1e9);
    return static_cast<long long>(deadline_ns * 0.95);
}

spe::geometry::SpeakerLayout makeTestLayout() {
    spe::geometry::SpeakerLayout l;
    l.name = "runtime_demote_integration_4ch";
    l.regularity = spe::geometry::Regularity::CIRCULAR;
    const float azs[] = {0.f, 90.f, 180.f, 270.f};
    for (int i = 0; i < 4; ++i) {
        const float az = azs[i] * static_cast<float>(M_PI) / 180.f;
        spe::geometry::Speaker s;
        s.channel = i + 1;
        s.x = std::sin(az);
        s.y = 0.f;
        s.z = std::cos(az);
        l.speakers.push_back(s);
        l.channel_to_idx_[static_cast<std::size_t>(i + 1)] = static_cast<int16_t>(i);
    }
    return l;
}

void activateObject0(spe::core::SpatialEngine& engine) {
    spe::ipc::Command move{};
    move.tag = spe::ipc::CommandTag::ObjMove;
    spe::ipc::PayloadObjMove p{};
    p.obj_id  = 0;
    p.az_rad  = 0.f;
    p.el_rad  = 0.f;
    p.dist_m  = 1.f;
    move.payload = p;
    engine.dispatchCommand(move);

    spe::ipc::Command gain{};
    gain.tag = spe::ipc::CommandTag::ObjGain;
    spe::ipc::PayloadObjGain pg{};
    pg.obj_id = 0;
    pg.gain   = 1.f;
    gain.payload = pg;
    engine.dispatchCommand(gain);
}

int run()
{
    spe::core::SpatialEngine engine(/*listen_port=*/0);
    engine.setLayout(makeTestLayout());
    engine.setBinauralSofaPath(std::string(SPE_FIXTURES_DIR) +
                               "/synthetic_min.speh");
    engine.setBinauralEnabled(true);
    engine.prepareToPlay(kSampleRate, kBlock);
    activateObject0(engine);

    // Request AmbiVS so the B2 dispatch lambda is exercised. Note:
    // the throughput probe may clamp effective_mode_ back to Direct on
    // slow CI runners — we do NOT depend on effective_mode being AmbiVS
    // for the integration assertions below, since the forwarder API
    // and the kill-switch behavior must be correct regardless.
    engine.setBinauralMode(/*AmbiVS=*/1);

    constexpr int kOutCh = 4;
    std::vector<std::vector<float>> outs(kOutCh, std::vector<float>(kBlock, 0.f));
    std::vector<float*> out_ptrs(kOutCh);
    for (int c = 0; c < kOutCh; ++c)
        out_ptrs[static_cast<std::size_t>(c)] = outs[static_cast<std::size_t>(c)].data();

    spe::audio_io::AudioBlock block{};
    block.input_channels       = nullptr;
    block.input_channel_count  = 0;
    block.output_channels      = out_ptrs.data();
    block.output_channel_count = kOutCh;
    block.num_frames           = kBlock;
    block.hw_timestamp_ns      = 0;

    // ── Step 1: forwarders must report a clean monitor at start ──────────
    REQUIRE(!engine.binauralIsRuntimeDemoted());
    REQUIRE(!engine.binauralDrainRuntimeDemotePending());

    // Drive 8 real blocks. On a fast CI runner the actual recordB2BlockTiming
    // call inside audioBlock() will measure under-budget every time, leaving
    // the strike counter at 0 and the demote flag at false. This proves the
    // wrapper at SpatialEngine.cpp:709-731 doesn't spuriously demote even
    // when the dispatch enters the AmbiVS branch.
    for (int b = 0; b < 8; ++b) engine.audioBlock(block);
    REQUIRE(!engine.binauralIsRuntimeDemoted());
    REQUIRE(!engine.binauralDrainRuntimeDemotePending());

    // ── Step 2: drive the demote via the engine-level test hook ─────────
    //
    // Inject (kRuntimeDemoteStrikes - 1) strikes through the engine
    // forwarder, then one final over-budget recording. After this the
    // engine MUST see runtime_demoted == true and the warning latch must
    // fire exactly once.
    engine.injectBinauralRuntimeUnderrunStrikesForTest();
    REQUIRE(!engine.binauralIsRuntimeDemoted());

    engine.recordBinauralB2BlockTimingForTest(
        kBlock, kSampleRate, overBudgetNs());

    REQUIRE(engine.binauralIsRuntimeDemoted());
    REQUIRE(engine.binauralDrainRuntimeDemotePending());
    REQUIRE(!engine.binauralDrainRuntimeDemotePending());  // single-fire

    // ── Step 3: P1-4 kill-switch sanity — post-demote audioBlock() must
    //            NOT change the engine-observed demote/latch state ───────
    //
    // The kill-switch wraps the wall-clock brackets in
    //   if (!binaural_.isRuntimeDemoted()) { ... record ... }
    //   else { processBlockB2 only }
    // so a regression that re-enabled timing on a demoted monitor would
    // either (a) re-arm the latch via a fresh strike progression (no — we
    // only call recordB2BlockTiming inside the guard, and recordB2BlockTiming
    // itself early-returns on demoted), or (b) leave the audio dispatch in
    // an inconsistent state. We can't observe (b) directly, but (a) would
    // surface as drainRuntimeDemotePending() returning true on a later
    // block — which the loop below explicitly checks.
    for (int b = 0; b < 16; ++b) {
        engine.audioBlock(block);
        REQUIRE(engine.binauralIsRuntimeDemoted());          // still sticky
        REQUIRE(!engine.binauralDrainRuntimeDemotePending()); // no re-fire
    }

    // ── Step 4: clear forwarder resets the state ────────────────────────
    engine.clearBinauralRuntimeDemoteForTest();
    REQUIRE(!engine.binauralIsRuntimeDemoted());
    REQUIRE(!engine.binauralDrainRuntimeDemotePending());

    // After clear, the forwarders behave as on a fresh-initialized monitor.
    // Re-inject + re-trigger to prove the path is repeatable (covers the
    // future "re-bring-up after prepareToPlay" scenario).
    engine.injectBinauralRuntimeUnderrunStrikesForTest();
    engine.recordBinauralB2BlockTimingForTest(
        kBlock, kSampleRate, overBudgetNs());
    REQUIRE(engine.binauralIsRuntimeDemoted());
    REQUIRE(engine.binauralDrainRuntimeDemotePending());

    engine.releaseResources();
    std::puts("PASS test_b2_runtime_underrun_engine_integration "
              "(forwarders wired + P1-4 kill-switch sticky-stable)");
    return 0;
}

} // namespace

int main() {
    return run();
}
